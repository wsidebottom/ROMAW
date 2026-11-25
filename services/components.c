/*+===================================================================
  File:      components.c

  Summary:   Classic Component Manager registration, search order and instance lifetime.

  Classes:   None.

  Functions: ROMlib_component_call, ROMlib_component_can_dispatch, ROMlib_component_copy_name,
             ROMlib_component_reset, ROMlib_component_selector, ROMlib_component_service,
             ROMlib_component_signature, ROMlib_component_text_services

  Origin:    Existing repository source in executor/src/services; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Classic Component Manager registration and instance lifetime support. */
#include "rsys/common.h"
#include "OSUtil.h"
#include "rsys/everything.h"
#include "rsys/component.h"
#include "rsys/mman.h"
#include "rsys/resource.h"
#include "rsys/system_resources.h"
#include "rsys/ctopflags.h"
#include "rsys/stubify.h"

typedef struct PACKED {
    OSType type, subtype, manufacturer;
    uint32 flags, mask;
} component_description;

typedef struct component_type_seed {
    struct component_type_seed* next;
    OSType type;
    uint32 seed;
} component_type_seed;

typedef struct component_entry {
    struct component_entry* next;
    uint32 id, refcon, registration_flags, version, instances;
    component_description description;
    component_type_seed* type_seed;
    Handle code, name, info, icon;
    FSSpec spec;
    ProcPtr entry;
    uint32 saved_a5;
    unsigned loading;
    unsigned platform;
    INTEGER ref;
} component_entry;

typedef struct component_instance {
    struct component_instance* next;
    component_entry* component;
    uint32 id, saved_a5;
    Handle storage;
    INTEGER error;
    unsigned busy, closing;
} component_instance;

typedef struct component_resource_file {
    struct component_resource_file* next;
    resmaphand map;
    INTEGER reference;
    unsigned opens;
    Boolean owned;
} component_resource_file;

static void* component_pointer(uint32 address)
{
    return address ? SYN68K_TO_US(address) : NULL;
}

static component_entry* components;
static component_instance* instances;
static component_type_seed* type_seeds;
static component_resource_file* resource_files;
static uint32 next_id = 0x10000, modification_seed;
static int shutting_down;

OSErr ROMlib_component_copy_name(OSType type, OSType subtype, StringPtr name)
{
    for (component_entry* component = components; component; component = component->next) {
        if (component->description.type != CL(type) || component->description.subtype != CL(subtype) ||
            !component->name)
            continue;
        const Byte* source = (const Byte*)STARH(component->name);
        const Size length = GetHandleSize(component->name);
        if (source && length > 0 && source[0] && length > source[0]) {
            memcpy(name, source, 1 + source[0]);
            return noErr;
        }
    }
    return resNotFound;
}

static OSType text_service_type(const OSType* types, unsigned index)
{
    OSType type;
    memcpy(&type, (const Byte*)types + index * sizeof type, sizeof type);
    return type;
}

OSErr ROMlib_component_text_services(INTEGER count, const OSType* types, HIDDEN_Handle* output, LONGINT* seed)
{
    if (count <= 0 || !types || !output || !seed)
        return paramErr;
    unsigned matches = 0;
    for (unsigned i = 0; i < (unsigned)count; ++i) {
        const OSType type = text_service_type(types, i);
        if (type != CLC(T('t', 's', 'v', 'c')) && type != CLC(T('u', 't', 's', 'v')))
            return -2518; /* tsmUnsupInterfaceErr */
        for (const component_entry* component = components; component; component = component->next)
            if (component->description.type == type && ++matches > INT16_MAX)
                return memFullErr;
    }

    enum { header_size = 2, entry_size = 260, name_size = 256 };

    const Size needed = header_size + MAX(matches, 1) * entry_size;
    Byte* snapshot = calloc(1, needed);
    if (!snapshot)
        return memFullErr;
    /* Guest heap growth can call back into the component manager. Capture
       the rows and their seed before allocating or resizing a guest handle. */
    Byte* entry = snapshot + header_size;
    for (unsigned i = 0; i < (unsigned)count; ++i) {
        const OSType type = text_service_type(types, i);
        for (const component_entry* component = components; component; component = component->next) {
            if (component->description.type != type)
                continue;
            const uint32 token = CL(component->id);
            memcpy(entry, &token, sizeof token);
            const Size length = component->name ? GetHandleSize(component->name) : 0;
            if (length > 0)
                memcpy(entry + sizeof token, STARH(component->name), MIN(length, name_size));
            entry += entry_size;
        }
    }
    const uint16 result = CW(matches);
    memcpy(snapshot, &result, sizeof result);
    const uint32 generation = CL(modification_seed);
    Handle list = HIDDEN_NATIVE(*output);
    if (!list)
        list = NewHandle(needed);
    else if (GetHandleSize(list) < needed)
        SetHandleSize(list, needed);
    if (!list || MemError() != noErr) {
        free(snapshot);
        return MemError();
    }
    memcpy(STARH(list), snapshot, needed);
    free(snapshot);
    memcpy(seed, &generation, sizeof generation);
    HIDDEN_RAW_SET(*output, RM(list));
    return noErr;
}

static component_entry* component_find(uint32 id)
{
    component_entry* p;
    for (p = components; p; p = p->next)
        if (p->id == id)
            return p;
    return NULL;
}

static component_instance* instance_find(uint32 id)
{
    component_instance* p;
    for (p = instances; p; p = p->next)
        if (p->id == id)
            return p;
    return NULL;
}

static uint32 token(void)
{
    if (next_id == UINT32_MAX)
        return 0;
    return ++next_id;
}

static Handle copy_handle(Handle source)
{
    Handle result;
    Size size;
    if (!source || (size = GetHandleSize(source)) < 0)
        return NULL;
    result = NewHandleSys(size);
    if (result && size)
        memcpy(STARH(result), STARH(source), size);
    return result;
}

static int description_matches(const component_description* candidate, const component_description* wanted)
{
    return wanted && (!wanted->type || wanted->type == candidate->type) &&
           (!wanted->subtype || wanted->subtype == candidate->subtype) &&
           (!wanted->manufacturer || wanted->manufacturer == candidate->manufacturer) &&
           !((candidate->flags ^ wanted->flags) & wanted->mask);
}

enum {
    default_any_flags = 1,
    default_any_manufacturer = 2,
    default_any_subtype = 4,
    register_no_duplicates = 2,
    register_after_existing = 4,
};

static int same_component_group(const component_description* candidate, const component_description* wanted,
                                unsigned flags)
{
    return candidate->type == wanted->type &&
           ((flags & default_any_subtype) || candidate->subtype == wanted->subtype) &&
           ((flags & default_any_manufacturer) || candidate->manufacturer == wanted->manufacturer) &&
           ((flags & default_any_flags) || candidate->flags == wanted->flags);
}

static component_type_seed* component_find_type_seed(OSType type, boolean_t create)
{
    for (component_type_seed* record = type_seeds; record; record = record->next)
        if (record->type == type)
            return record;
    if (!create)
        return NULL;
    component_type_seed* record = calloc(1, sizeof *record);
    if (record) {
        record->type = type;
        record->next = type_seeds;
        type_seeds = record;
    }
    return record;
}

static void component_modified(component_entry* component)
{
    component->type_seed->seed = ++modification_seed;
}

static int16 set_default_component(uint32 id, uint16 flags)
{
    component_entry** current = &components;
    while (*current && (*current)->id != id)
        current = &(*current)->next;
    component_entry* selected = *current;
    if (!selected)
        return -3000;
    if (flags & ~(default_any_flags | default_any_manufacturer | default_any_subtype))
        return paramErr;

    const component_description* wanted = &selected->description;
    for (component_entry** first = &components; first != current; first = &(*first)->next) {
        if (same_component_group(&(*first)->description, wanted, flags)) {
            *current = selected->next;
            selected->next = *first;
            *first = selected;
            component_modified(selected);
            break;
        }
    }
    return noErr;
}

static int component_load(component_entry* p)
{
    if (p->entry)
        return 1;
    if (!p->code || GetHandleSize(p->code) < 2)
        return 0;
    HLock(p->code);
    if (p->platform == 1) {
        p->entry = (ProcPtr)STARH(p->code);
        return 1;
    }
#if defined(CYGWIN32) && defined(SDL) && defined(SYN68K)
    if (p->platform == 2) {
        int ok;
        if (p->loading)
            return 0;
        p->loading = 1;
        ok = ROMlib_ppc_component_entry(p->code, &p->spec, &p->entry);
        p->loading = 0;
        if (ok)
            return 1;
    }
#endif
    HUnlock(p->code);
    return 0;
}

static int32 invoke(component_instance* instance, const void* parameters)
{
    uint32 saved_a5 = EM_A5;
    int32 result;
    if (!component_load(instance->component))
        return -3000;
    if (instance->saved_a5)
        EM_A5 = instance->saved_a5;
    ++instance->busy;
    result = CToPascalCall(instance->component->entry, 4 | (5 << 3) | (5 << 6), parameters, instance->storage);
    --instance->busy;
    EM_A5 = saved_a5;
    return result;
}

static int32 control(component_instance* instance, int16 selector, int self_argument)
{
    struct PACKED {
        uint8 flags, size;
        int16 what;
        uint32 self;
    } parameters;

    memset(&parameters, 0, sizeof parameters);
    parameters.size = self_argument ? 4 : 0;
    parameters.what = CW(selector);
    parameters.self = CL(instance->id);
    return invoke(instance, &parameters);
}

static uint32 component_open(component_entry* p)
{
    component_instance* instance;
    if (shutting_down || !p || !component_load(p))
        return 0;
    instance = calloc(1, sizeof *instance);
    if (!instance)
        return 0;
    instance->id = token();
    if (!instance->id) {
        free(instance);
        return 0;
    }
    instance->component = p;
    instance->saved_a5 = p->saved_a5;
    instance->next = instances;
    instances = instance;
    ++p->instances;
    if (control(instance, -1, 1)) {
        component_instance** link;
        for (link = &instances; *link && *link != instance; link = &(*link)->next)
            ;
        if (*link)
            *link = instance->next;
        --p->instances;
        free(instance);
        return 0;
    }
    return instance->id;
}

static int32 component_close(uint32 id)
{
    component_instance **link, *instance;
    int32 result;
    for (link = &instances; *link && (*link)->id != id; link = &(*link)->next)
        ;
    if (!(instance = *link) || instance->closing || instance->busy)
        return -3000;
    instance->closing = 1;
    result = control(instance, -2, 1);
    /* A close callback can open/close other instances, so locate this link anew. */
    for (link = &instances; *link && *link != instance; link = &(*link)->next)
        ;
    if (*link)
        *link = instance->next;
    --instance->component->instances;
    free(instance);
    return result;
}

static int32 component_unregister(uint32 id)
{
    component_entry **link, *p;
    for (link = &components; *link && (*link)->id != id; link = &(*link)->next)
        ;
    if (!(p = *link) || p->instances || p->loading)
        return -3000;
    *link = p->next;
    component_modified(p);
    if (p->code)
        DisposHandle(p->code);
    if (p->name)
        DisposHandle(p->name);
    if (p->info)
        DisposHandle(p->info);
    if (p->icon)
        DisposHandle(p->icon);
    free(p);
    return noErr;
}

static uint32 component_register(const component_description* description, ProcPtr entry, Handle code, Handle name,
                                 Handle info, Handle icon, const FSSpec* spec, INTEGER ref, unsigned platform,
                                 unsigned options, uint32 registration_flags, uint32 version)
{
    component_entry* p;
    uint32 instance;
    int32 result;
    if (shutting_down || !description || (!entry && !code))
        return 0;
    p = calloc(1, sizeof *p);
    if (!p)
        return 0;
    p->description = *description;
    p->description.mask = 0;
    p->id = token();
    p->code = copy_handle(code);
    p->name = copy_handle(name);
    p->info = copy_handle(info);
    p->icon = copy_handle(icon);
    component_entry** position = &components;
    if (options & (register_no_duplicates | register_after_existing))
        for (component_entry* existing = components; existing; existing = existing->next) {
            if ((options & register_no_duplicates) &&
                same_component_group(&existing->description, &p->description, 0)) {
                p->id = 0;
                break;
            }
            if ((options & register_after_existing) && existing->description.type == p->description.type)
                position = &existing->next;
        }
    if (!p->id || (code && !p->code) || (name && !p->name) || (info && !p->info) || (icon && !p->icon) ||
        !(p->type_seed = component_find_type_seed(description->type, TRUE))) {
        if (p->code)
            DisposHandle(p->code);
        if (p->name)
            DisposHandle(p->name);
        if (p->info)
            DisposHandle(p->info);
        if (p->icon)
            DisposHandle(p->icon);
        free(p);
        return 0;
    }
    p->entry = entry;
    p->saved_a5 = EM_A5;
    p->platform = platform;
    p->registration_flags = registration_flags;
    p->version = version;
    p->ref = ref;
    if (spec)
        p->spec = *spec;
    p->next = *position;
    *position = p;
    component_modified(p);
    if (CL(p->description.flags) & UINT32_C(0x80000000)) {
        instance = component_open(p);
        if (!instance) {
            component_unregister(p->id);
            return 0;
        }
        result = control(instance_find(instance), -5, 0);
        component_close(instance);
        if (result) {
            component_unregister(p->id);
            return 0;
        }
    }
    return p->id;
}

static Handle resource_spec(const unsigned char* spec)
{
    uint32 type;
    int16 id;
    memcpy(&type, spec, 4);
    memcpy(&id, spec + 4, 2);
    return type ? Get1Resource(CL(type), CW(id)) : NULL;
}

static uint32 register_resource(Handle resource, unsigned global)
{
    unsigned char bytes[58], platform_bytes[12];
    unsigned char* data;
    Size size;
    component_description description;
    Handle code, name, info, icon;
    INTEGER saved = CurResFile(), ref;
    FCBPBRec pb;
    FSSpec spec;
    Str255 filename;
    uint32 flags = 0, version = 0, count = 0, result = 0;
    unsigned platform = 1, i;
    if (!resource || (size = GetHandleSize(resource)) < 44)
        return 0;
    ref = HomeResFile(resource);
    if (ref < 0)
        return 0;
    UseResFile(ref);
    memset(bytes, 0, sizeof bytes);
    memcpy(bytes, STARH(resource), MIN((Size)sizeof bytes, size));
    memcpy(&description, bytes, sizeof description);
    if (size >= 54) {
        memcpy(&version, bytes + 44, 4);
        version = CL(version);
        memcpy(&flags, bytes + 48, 4);
        flags = CL(flags);
    }
    if (flags & 8) {
        int found = 0;
        if (size < 58)
            goto done;
        memcpy(&count, bytes + 54, 4);
        count = CL(count);
        if (count > ((uint32)size - 58) / 12)
            goto done;
        for (i = 0; i < count; ++i) {
            uint16 arch;
            data = (unsigned char*)STARH(resource) + 58 + 12 * i;
            memcpy(platform_bytes, data, sizeof platform_bytes);
            memcpy(&arch, platform_bytes + 10, 2);
            arch = CW(arch);
            if (arch == 1 || arch == 2) {
                memcpy(&description.flags, platform_bytes, 4);
                memcpy(bytes + 20, platform_bytes + 4, 6);
                platform = arch;
                found = 1;
                if (arch == 2)
                    break;
            }
        }
        if (!found)
            goto done;
    }
    memset(&pb, 0, sizeof pb);
    memset(&spec, 0, sizeof spec);
    pb.ioRefNum = CW(ref);
    FIELD_ASSIGN_RAW(pb.ioNamePtr, RM((StringPtr)filename));
    if (PBGetFCBInfo(&pb, FALSE) != noErr)
        goto done;
    if (C_FSMakeFSSpec(CW(pb.ioFCBVRefNum), CL(pb.ioFCBParID), filename, &spec) != noErr)
        goto done;
    code = resource_spec(bytes + 20);
    name = resource_spec(bytes + 26);
    info = resource_spec(bytes + 32);
    icon = resource_spec(bytes + 38);
    if (code)
        result = component_register(&description, NULL, code, name, info, icon, &spec, ref, platform, global, flags,
                                    version);
done:
    UseResFile(saved);
    return result;
}

static OSErr component_open_resource_file(uint32 id, INTEGER* reference)
{
    const component_instance* instance = instance_find(id);
    const component_entry* component = instance ? instance->component : component_find(id);
    if (!component)
        return -3000; /* invalidComponentID */
    if (!component->spec.name[0] || component->spec.name[0] > 63)
        return resFNotFound;

    const FSSpec spec = component->spec;
    component_resource_file* lease = calloc(1, sizeof *lease);
    if (!lease)
        return memFullErr;
    INTEGER previous_ref;
    const int already_open = ROMlib_resource_file_already_open(&spec, NULL, &previous_ref);
    Str255 name;
    memcpy(name, spec.name, spec.name[0] + 1);
    const INTEGER opened = HOpenResFile(CW(spec.vRefNum), CL(spec.parID), name, fsRdPerm);
    if (opened < 0) {
        const OSErr error = ResError();
        free(lease);
        return error ? error : resFNotFound;
    }
    resmaphand map = ROMlib_rntohandl(opened, NULL);
    if (!map) {
        free(lease);
        return resFNotFound;
    }

    for (component_resource_file* current = resource_files; current; current = current->next) {
        if (current->reference == opened && current->map == map) {
            free(lease);
            if (current->opens == UINT_MAX)
                return memFullErr;
            ++current->opens;
            *reference = opened;
            return noErr;
        }
    }
    lease->map = map;
    lease->reference = opened;
    lease->opens = 1;
    lease->owned = !already_open || opened != CW(previous_ref);
    lease->next = resource_files;
    resource_files = lease;
    *reference = opened;
    return noErr;
}

static OSErr component_close_resource_file(INTEGER reference)
{
    component_resource_file** next = &resource_files;
    while (*next && (*next)->reference != reference)
        next = &(*next)->next;
    component_resource_file* lease = *next;
    if (!lease)
        return resFNotFound;

    OSErr status = noErr;
    if (lease->map != ROMlib_rntohandl(reference, NULL))
        status = resFNotFound;
    else if (--lease->opens)
        return noErr;
    *next = lease->next;
    const Boolean close = !status && lease->owned;
    free(lease);
    if (close) {
        CloseResFile(reference);
        status = ResError();
    }
    return status;
}

static OSErr component_get_resource(uint32 id, OSType type, INTEGER number, Handle* output)
{
    const INTEGER saved = CurResFile();
    INTEGER reference;
    OSErr status = component_open_resource_file(id, &reference);
    if (status)
        return status;

    UseResFile(reference);
    Handle resource = Get1Resource(type, number);
    status = ResError();
    Handle copy = NULL;
    if (!status && resource) {
        LoadResource(resource);
        status = ResError();
        if (!status && STARH(resource)) {
            const SignedByte state = HGetState(resource);
            HLock(resource);
            copy = resource;
            status = HandToHand(&copy);
            HSetState(resource, state);
            if (status)
                copy = NULL;
        }
    }
    if (!status && !copy)
        status = resNotFound;

    const OSErr close_error = component_close_resource_file(reference);
    UseResFile(saved);
    if (!status && close_error) {
        DisposHandle(copy);
        copy = NULL;
        status = close_error;
    }
    *output = copy;
    return status;
}

static OSErr component_get_string(uint32 id, INTEGER list_id, INTEGER index, StringPtr output)
{
    if (!output)
        return paramErr;
    output[0] = 0;
    if (index <= 0)
        return paramErr;

    Handle copy = NULL;
    OSErr status = component_get_resource(id, TICK("STR#"), list_id, &copy);
    if (status)
        return status;
    const Size size = GetHandleSize(copy);
    const Byte* data = (const Byte*)STARH(copy);
    uint16 count;
    status = resNotFound;
    if (size >= (Size)sizeof count && data) {
        memcpy(&count, data, sizeof count);
        if (index <= CW(count)) {
            const Byte* entry = data + sizeof count;
            const Byte* end = data + size;
            for (int current = 1; current <= index && entry < end; ++current) {
                const size_t length = *entry + 1;
                if (length > (size_t)(end - entry))
                    break;
                if (current == index) {
                    memcpy(output, entry, length);
                    status = noErr;
                    break;
                }
                entry += length;
            }
        }
    }
    DisposHandle(copy);
    return status;
}

PUBLIC int ROMlib_component_can_dispatch(uint32 id)
{
    enum { component_dispatch_trap_index = 0x2a };

    return tooltraptable[component_dispatch_trap_index] == toolstuff[component_dispatch_trap_index].orig &&
           (instance_find(id) || component_find(id));
}

PUBLIC int32 ROMlib_component_call(uint32 id, const void* parameters)
{
    component_instance* instance = instance_find(id);
    uint32 temporary = 0;
    int32 result;
    if (!parameters)
        return paramErr;
    if (!instance) {
        temporary = component_open(component_find(id));
        instance = instance_find(temporary);
    }
    if (!instance)
        return -3000;
    result = invoke(instance, parameters);
    if (temporary)
        component_close(temporary);
    return result;
}

PUBLIC void ROMlib_component_reset(void)
{
    shutting_down = 1;
    while (instances)
        component_close(instances->id);
    while (components)
        component_unregister(components->id);
    while (resource_files) {
        resource_files->opens = 1;
        component_close_resource_file(resource_files->reference);
    }
    while (type_seeds) {
        component_type_seed* record = type_seeds;
        type_seeds = record->next;
        free(record);
    }
    shutting_down = 0;
}

PUBLIC int ROMlib_component_service(unsigned selector, const uint32* a, uint32* result)
{
    component_entry* p;
    component_instance* instance;
    uint32 count = 0;
    *result = 0;
    switch (selector) {
    case 1:
        *result = component_register(component_pointer(a[0]), component_pointer(a[1]), NULL, component_pointer(a[3]),
                                     component_pointer(a[4]), component_pointer(a[5]), NULL, 0, 1, a[2], 0, 0);
        break;
    case 2:
        *result = component_unregister(a[0]);
        break;
    case 3:
    case 4:
        p = selector == 4 && a[0] ? component_find(a[0]) : NULL;
        if (selector == 4 && a[0] && !p)
            break;
        for (p = p ? p->next : components; p; p = p->next)
            if (description_matches(&p->description, component_pointer(a[selector == 4 ? 1 : 0]))) {
                if (selector == 4) {
                    count = p->id;
                    break;
                }
                ++count;
            }
        *result = count;
        break;
    case 5:
        p = component_find(a[0]);
        if (!p && (instance = instance_find(a[0])))
            p = instance->component;
        *result = -3000;
        if (p) {
            Handle source[3] = {p->name, p->info, p->icon};
            unsigned i;
            if (a[1])
                memcpy(component_pointer(a[1]), &p->description, sizeof p->description);
            *result = noErr;
            for (i = 0; i < 3; ++i)
                if (a[i + 2]) {
                    Handle destination = component_pointer(a[i + 2]);
                    Size size = source[i] ? GetHandleSize(source[i]) : 0;
                    SetHandleSize(destination, size);
                    if (MemError() != noErr) {
                        *result = MemError();
                        break;
                    }
                    if (size)
                        memcpy(STARH(destination), STARH(source[i]), size);
                }
        }
        break;
    case 6:
        *result = modification_seed;
        break;
    case 7:
        *result = component_open(component_find(a[0]));
        break;
    case 8:
        *result = component_close(a[0]);
        break;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        instance = instance_find(a[0]);
        if (!instance) {
            *result = -3000;
            break;
        }
        if (selector == 10) {
            *result = instance->error;
            instance->error = 0;
        }
        if (selector == 11)
            instance->error = a[1];
        if (selector == 12)
            *result = instance->storage ? US_TO_SYN68K(instance->storage) : 0;
        if (selector == 13)
            instance->storage = component_pointer(a[1]);
        if (selector == 14)
            *result = instance->saved_a5;
        if (selector == 15)
            instance->saved_a5 = a[1];
        break;
    case 16:
    case 17:
        p = component_find(a[0]);
        if (!p) {
            *result = -3000;
            break;
        }
        if (selector == 16)
            *result = p->refcon;
        else
            p->refcon = a[1];
        break;
    case 18:
        *result = register_resource(component_pointer(a[0]), a[1]);
        break;
    case 19:
        for (instance = instances; instance; instance = instance->next)
            if (!a[0] || instance->component->id == a[0])
                ++count;
        *result = count;
        break;
    case 20: {
        INTEGER saved = CurResFile(), total, i;
        UseResFile(a[0]);
        total = Count1Resources(T('t', 'h', 'n', 'g'));
        for (i = 1; i <= total; ++i)
            if (register_resource(Get1IndResource(T('t', 'h', 'n', 'g'), i), a[1]))
                ++count;
        UseResFile(saved);
        *result = count;
    } break;
    case 21: /* OpenComponentResFile */
    case 47: { /* OpenAComponentResFile */
        INTEGER* output = selector == 47 ? component_pointer(a[1]) : NULL;
        INTEGER reference;
        if (selector == 47 && !output) {
            *result = paramErr;
            break;
        }
        const OSErr status = component_open_resource_file(a[0], &reference);
        if (output) {
            const INTEGER encoded = CW(status ? -1 : reference);
            memcpy(output, &encoded, sizeof encoded);
        }
        *result = status ? status : selector == 21 ? reference : noErr;
        break;
    }
    case 24:
        *result = component_close_resource_file((INTEGER)a[0]);
        break;
    case 53: { /* GetComponentResource */
        HIDDEN_Handle* output = component_pointer(a[3]);
        if (!output) {
            *result = paramErr;
            break;
        }
        Handle resource = NULL;
        *result = component_get_resource(a[0], a[1], (INTEGER)a[2], &resource);
        HIDDEN_RAW_SET(*output, resource ? RM(resource) : 0);
        break;
    }
    case 54: /* GetComponentIndString */
        *result = component_get_string(a[0], (INTEGER)a[2], (INTEGER)a[3], component_pointer(a[1]));
        break;
    case 25:
        *result = 3;
        break;
    case 30:
        *result = set_default_component(a[0], a[1]);
        break;
    case 44: { /* GetComponentTypeModSeed */
        const component_type_seed* record = component_find_type_seed(CL(a[0]), FALSE);
        *result = record ? record->seed : 0;
        return 1;
    }
    case 45: {
        uint32* output = component_pointer(a[1]);
        if (!output) {
            *result = paramErr;
            break;
        }
        uint32 instance_id = component_open(component_find(a[0]));
        *output = CL(instance_id);
        *result = instance_id ? noErr : -3000;
        break;
    }
    case 33:
    case 46: {
        uint32* output = selector == 46 ? component_pointer(a[2]) : NULL;
        if (selector == 46 && !output) {
            *result = paramErr;
            break;
        }
        component_description wanted;
        memset(&wanted, 0, sizeof wanted);
        wanted.type = CL(a[0]);
        wanted.subtype = CL(a[1]);
        for (p = components; p; p = p->next)
            if (description_matches(&p->description, &wanted) && (*result = component_open(p)))
                break;
        if (output) {
            *output = CL(*result);
            *result = *result ? noErr : -3000;
        }
    } break;
    default:
        return 0;
    }
    return 1;
}

static const struct {
    unsigned selector;
    const char *name, *signature;
} component_apis[] = {{1, "RegisterComponent", "4442444"},
                      {2, "UnregisterComponent", "24"},
                      {3, "CountComponents", "44"},
                      {4, "FindNextComponent", "444"},
                      {5, "GetComponentInfo", "244444"},
                      {6, "GetComponentListModSeed", "4"},
                      {7, "OpenComponent", "44"},
                      {8, "CloseComponent", "24"},
                      {10, "GetComponentInstanceError", "24"},
                      {11, "SetComponentInstanceError", "042"},
                      {12, "GetComponentInstanceStorage", "44"},
                      {13, "SetComponentInstanceStorage", "044"},
                      {14, "GetComponentInstanceA5", "44"},
                      {15, "SetComponentInstanceA5", "044"},
                      {16, "GetComponentRefcon", "44"},
                      {17, "SetComponentRefcon", "044"},
                      {18, "RegisterComponentResource", "442"},
                      {19, "CountComponentInstances", "44"},
                      {20, "RegisterComponentResourceFile", "422"},
                      {21, "OpenComponentResFile", "24"},
                      {24, "CloseComponentResFile", "22"},
                      {25, "ComponentManagerVersion", "4"},
                      {30, "SetDefaultComponent", "242"},
                      {33, "OpenDefaultComponent", "444"},
                      {44, "GetComponentTypeModSeed", "44"},
                      {45, "OpenAComponent", "244"},
                      {46, "OpenADefaultComponent", "2444"},
                      {47, "OpenAComponentResFile", "244"},
                      {53, "GetComponentResource", "24424"},
                      {54, "GetComponentIndString", "24422"}};

PUBLIC const char* ROMlib_component_signature(unsigned selector)
{
    unsigned i;
    for (i = 0; i < NELEM(component_apis); ++i)
        if (component_apis[i].selector == selector)
            return component_apis[i].signature;
    return NULL;
}

PUBLIC int ROMlib_component_selector(const char* name)
{
    unsigned i;
    for (i = 0; i < NELEM(component_apis); ++i)
        if (!strcmp(component_apis[i].name, name))
            return component_apis[i].selector;
    return -1;
}
