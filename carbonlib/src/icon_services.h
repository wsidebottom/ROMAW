/*+===================================================================
  File:      icon_services.h

  Summary:   Native Icon Services family data and reference ownership.

  Classes:   None.

  Functions: ROMlib_carbon_icon_services_reset; private Icon Services adapters.

  Origin:    System 9.2.1 IconServicesLib, its 'isrv' icon mapping resource, and
             Universal Interfaces 3.4.2 Icons.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_ICON_SERVICES_H
#define ROMAW_CARBON_ICON_SERVICES_H

#include "AliasMgr.h"
#include "cf_batch.h"
#include "FileMgr.h"
#include "Iconutil.h"
#include "icon_rgb.h"
#include "MemoryMgr.h"
#include "rsys/cquick.h"
#include "rsys/file.h"
#include "rsys/fileicon.h"
#include "rsys/hfs.h"
#include "../../finder-metadata/include/finder_metadata/icons.h"
#include "toolbox_vectors.h"
#include <stdlib.h>

enum { invalid_icon_ref = -2580, no_such_icon = -2581, icon_family_header_size = 8 };

typedef enum { icon_memory, icon_system, icon_catalog, icon_data_fork, icon_resource_fork } IconSource;

typedef struct IconReference {
    struct IconReference* next;
    Handle identity;
    uint32 creator, type;
    uint16 owners;
    boolean_t registered;
    IconSource source;
    FSSpec source_file;
    int16 resource_id;
    Byte* bytes;
    uint32 size;
    Byte* override_bytes;
    uint32 override_size;
} IconReference;

typedef struct {
    const Byte* bytes;
    uint32 size;
} IconFamilyView;

static IconReference* icon_references;
static boolean_t icon_deep_icons_enabled = TRUE;

static OSErr icon_reload(IconReference* icon);

static OSErr icon_family_view(IconReference* icon, IconFamilyView* family)
{
    if (icon->override_bytes) {
        *family = (IconFamilyView){icon->override_bytes, icon->override_size};
        return noErr;
    }
    if (!icon->bytes) {
        OSErr error = icon_reload(icon);
        if (error)
            return error;
    }
    *family = (IconFamilyView){icon->bytes, icon->size};
    return noErr;
}

static const struct {
    uint32 type;
    uint32 selector;
    uint16 size;
} classic_icon_elements[] = {
    {large1BitMask, svLarge1Bit, 256}, {large4BitData, svLarge4Bit, 512}, {large8BitData, svLarge8Bit, 1024},
    {small1BitMask, svSmall1Bit, 64},  {small4BitData, svSmall4Bit, 128}, {small8BitData, svSmall8Bit, 256},
};

enum { classic_icon_family_capacity = 8 + 6 * 8 + 256 + 512 + 1024 + 64 + 128 + 256 };

static uint32 icon_read_word(const void* address)
{
    uint32 value;
    memcpy(&value, address, 4);
    return CL(value);
}

static void icon_write_word(void* address, uint32 value)
{
    value = CL(value);
    memcpy(address, &value, 4);
}

static int icon_status(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

static int icon_saved_reference(const char* name, ROMlib_carbon_reference* saved)
{
    if (!ROMlib_carbon_probe_reference("IconServicesLib", name, saved))
        return 0;
    /* System 9.2.1's fragment forwards to the installed IconDispatch vector. */
    uint32 dispatch;
    return saved->code_size != 0x16fd0 || (cfp_get(0x17d4, &dispatch) && dispatch != 0);
}

static int icon_fallback(ROMlib_carbon_ppc_frame* call, const char* name, int32_t unavailable)
{
    ROMlib_carbon_reference saved;
    if (icon_saved_reference(name, &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    return icon_status(call, unavailable);
}

static VCBExtra* icon_mounted_volume(int16 reference)
{
    return (VCBExtra*)(reference < 0 ? ROMlib_vcbbyvrn(reference) : ROMlib_findvcb(reference, NULL, NULL, TRUE));
}

static bool icon_custom_enabled(int16 reference)
{
    const VCBExtra* volume = icon_mounted_volume(reference);
    return !volume || !volume->custom_icons_disabled;
}

static int carbon_GetCustomIconsEnabled(ROMlib_carbon_ppc_frame* call)
{
    Byte* enabled = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 1, 1) : NULL;
    if (!enabled)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("GetCustomIconsEnabled", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    const VCBExtra* volume = icon_mounted_volume((int16)call->words[0]);
    if (!volume)
        return icon_status(call, nsvErr);
    *enabled = !volume->custom_icons_disabled;
    return icon_status(call, noErr);
}

static int carbon_SetCustomIconsEnabled(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("SetCustomIconsEnabled", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    VCBExtra* volume = icon_mounted_volume((int16)call->words[0]);
    if (!volume)
        return icon_status(call, nsvErr);
    const boolean_t disabled = (uint8)call->words[1] == 0;
    if (volume->custom_icons_disabled != disabled) {
        volume->custom_icons_disabled = disabled;
        for (IconReference* icon = icon_references; icon; icon = icon->next)
            if (icon->source == icon_catalog && icon_mounted_volume(CW(icon->source_file.vRefNum)) == volume) {
                free(icon->bytes);
                icon->bytes = NULL;
                icon->size = 0;
            }
    }
    return icon_status(call, noErr);
}

static int carbon_GetDeepIconsEnabled(ROMlib_carbon_ppc_frame* call)
{
    Byte* enabled = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 1, 1) : NULL;
    if (!enabled)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("GetDeepIconsEnabled", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    *enabled = icon_deep_icons_enabled;
    return icon_status(call, noErr);
}

static int carbon_SetDeepIconsEnabled(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("SetDeepIconsEnabled", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_deep_icons_enabled = (uint8)call->words[0] != 0;
    return icon_status(call, noErr);
}

static const Byte* icon_handle_data(uint32 address, uint32* length)
{
    uint32 data;
    if (!address || !cfp_get(address, &data) || data < 12 || !ROMlib_carbon_try_guest_memory(data - 12, 12, 0))
        return NULL;
    Size size = GetHandleSize(SYN68K_TO_US(address));
    const Byte* bytes = size >= 0 ? ROMlib_carbon_try_guest_memory(data, size ? size : 1, 0) : NULL;
    if (bytes)
        *length = size;
    return bytes;
}

static boolean_t icon_family_valid(const Byte* bytes, uint32 size)
{
    if (!bytes || size < icon_family_header_size || icon_read_word(bytes) != TICK("icns") ||
        icon_read_word(bytes + 4) != size)
        return FALSE;
    for (uint32 offset = icon_family_header_size; offset < size;) {
        uint32 remaining = size - offset;
        if (remaining < icon_family_header_size)
            return FALSE;
        uint32 chunk = icon_read_word(bytes + offset + 4);
        if (chunk < icon_family_header_size || chunk > remaining)
            return FALSE;
        offset += chunk;
    }
    return TRUE;
}

static uint32 icon_family_element(const Byte* bytes, uint32 size, uint32 type)
{
    for (uint32 offset = icon_family_header_size; offset < size; offset += icon_read_word(bytes + offset + 4))
        if (icon_read_word(bytes + offset) == type)
            return offset;
    return size;
}

static IconReference* icon_find(uint32 address)
{
    for (IconReference* icon = icon_references; icon; icon = icon->next)
        if (US_TO_SYN68K(icon->identity) == address)
            return icon;
    return NULL;
}

static IconReference* icon_find_key(uint32 creator, uint32 type)
{
    for (IconReference* icon = icon_references; icon; icon = icon->next)
        if (icon->registered && icon->creator == creator && icon->type == type)
            return icon;
    return NULL;
}

static void icon_destroy(IconReference* icon)
{
    IconReference** link = &icon_references;
    while (*link != icon)
        link = &(*link)->next;
    *link = icon->next;
    DisposHandle(icon->identity);
    free(icon->bytes);
    free(icon->override_bytes);
    free(icon);
}

void ROMlib_carbon_icon_services_reset(void)
{
    while (icon_references)
        icon_destroy(icon_references);
}

static int icon_return_reference(ROMlib_carbon_ppc_frame* call, IconReference* icon, void* output)
{
    if (icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);
    ++icon->owners;
    icon_write_word(output, US_TO_SYN68K(icon->identity));
    return icon_status(call, noErr);
}

/* The Registry and the caller each have their own lifetime. */
static IconReference* icon_register_owned(uint32 creator, uint32 type, Byte* bytes, uint32 size)
{
    IconReference* icon = icon_find_key(creator, type);
    if (!icon) {
        icon = calloc(1, sizeof *icon);
        if (!icon || !(icon->identity = NewHandleSys(0))) {
            free(icon);
            free(bytes);
            return NULL;
        }
        icon->creator = creator;
        icon->type = type;
        icon->registered = TRUE;
        icon->next = icon_references;
        icon_references = icon;
    }
    free(icon->bytes);
    icon->bytes = bytes;
    icon->size = size;
    icon->source = icon_memory;
    return icon;
}

static IconReference* icon_register(uint32 creator, uint32 type, const Byte* bytes, uint32 size)
{
    Byte* copy = malloc(size);
    if (!copy)
        return NULL;
    memcpy(copy, bytes, size);
    return icon_register_owned(creator, type, copy, size);
}

static void icon_set_override(IconReference* icon, Byte* bytes, uint32 size)
{
    free(icon->override_bytes);
    icon->override_bytes = bytes;
    icon->override_size = size;
}

static int carbon_OverrideIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* target = icon_find(call->words[0]);
    IconReference* source = icon_find(call->words[1]);
    if (!target || !source)
        return icon_fallback(call, "OverrideIconRef", invalid_icon_ref);

    IconFamilyView family;
    OSErr status = icon_family_view(source, &family);
    if (status)
        return icon_status(call, status);
    Byte* copy = malloc(family.size);
    if (!copy)
        return icon_status(call, memFullErr);
    memcpy(copy, family.bytes, family.size);
    icon_set_override(target, copy, family.size);
    return icon_status(call, noErr);
}

static int carbon_RemoveIconRefOverride(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "RemoveIconRefOverride", invalid_icon_ref);
    icon_set_override(icon, NULL, 0);
    return icon_status(call, noErr);
}

static int carbon_RegisterIconRefFromIconFamily(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], 4, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    icon_write_word(output, 0);
    IconReference* icon = icon_find_key(call->words[0], call->words[1]);
    ROMlib_carbon_reference saved;
    if (!icon && icon_saved_reference("RegisterIconRefFromIconFamily", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    uint32 size = 0;
    const Byte* bytes = icon_handle_data(call->words[2], &size);
    if (!icon_family_valid(bytes, size))
        return icon_status(call, paramErr);
    if (icon && icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);
    icon = icon_register(call->words[0], call->words[1], bytes, size);
    return icon ? icon_return_reference(call, icon, output) : icon_status(call, memFullErr);
}

static int carbon_UnregisterIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find_key(call->words[0], call->words[1]);
    if (!icon)
        return icon_fallback(call, "UnregisterIconRef", no_such_icon);
    icon->registered = FALSE;
    if (!icon->owners)
        icon_destroy(icon);
    return icon_status(call, noErr);
}

/* Default ROMAW resources predate the 'isrv' resource. */
static int icon_resource_id(const Byte* map, uint32 length, uint32 type, int16* id)
{
    for (uint32 offset = 0; offset + 6 <= length; offset += 6)
        if (icon_read_word(map + offset) == type) {
            uint16 value;
            memcpy(&value, map + offset + 4, 2);
            *id = CW(value);
            return 1;
        }

    static const struct {
        uint32 type;
        int16 id;
    } defaults[] = {{TICK("asnd"), -20790}, {TICK("dsnd"), -20791}, {TICK("docu"), -4000}, {TICK("fldr"), -3999},
                    {TICK("hdsk"), -3995},  {TICK("APPL"), -3996},  {TICK("ofld"), -3997}, {TICK("trsh"), -3993},
                    {TICK("ftrh"), -3984},  {TICK("macs"), -3983}};

    for (unsigned i = 0; i < NELEM(defaults); ++i)
        if (defaults[i].type == type) {
            *id = defaults[i].id;
            return 1;
        }
    return 0;
}

static IconReference* icon_register_system(uint32 type, const Byte* bytes, uint32 size)
{
    IconReference* icon = icon_register(TICK("macs"), type, bytes, size);
    if (icon)
        icon->source = icon_system;
    return icon;
}

static const Byte* icon_map_family(int16 id, Byte* packed, uint32* size)
{
    Handle family = C_Get1Resource(TICK("icns"), id);
    *size = family ? GetHandleSize(family) : 0;
    const Byte* bytes = family ? (const Byte*)STARH(family) : NULL;
    if (icon_family_valid(bytes, *size))
        return bytes;

    uint32 used = icon_family_header_size;
    for (unsigned i = 0; i < NELEM(classic_icon_elements); ++i) {
        const uint32 type = classic_icon_elements[i].type;
        const uint32 length = classic_icon_elements[i].size;
        Handle data = C_Get1Resource(type, id);
        if (data && STARH(data) && GetHandleSize(data) == length) {
            icon_write_word(packed + used, type);
            icon_write_word(packed + used + 4, length + icon_family_header_size);
            memcpy(packed + used + icon_family_header_size, STARH(data), length);
            used += length + icon_family_header_size;
        }
    }
    if (used == icon_family_header_size)
        return NULL;
    icon_write_word(packed, TICK("icns"));
    icon_write_word(packed + 4, used);
    *size = used;
    return packed;
}

static IconReference* icon_load_system_family(uint32 type, int16 id)
{
    Byte packed[classic_icon_family_capacity];
    uint32 size;
    const Byte* bytes = icon_map_family(id, packed, &size);
    return bytes ? icon_register_system(type, bytes, size) : NULL;
}

static const FSSpec* icon_file_spec(uint32 address)
{
    const FSSpec* spec = address ? ROMlib_carbon_try_guest_memory(address, sizeof *spec, 0) : NULL;
    return spec && spec->name[0] <= 63 ? spec : NULL;
}

static OSErr icon_read_disk_family(const FSSpec* spec, Handle* family)
{
    FSSpec file = *spec;
    INTEGER reference;
    OSErr status = C_FSpOpenDF(&file, fsRdPerm, &reference);
    if (status)
        return status;
    reference = CW(reference);
    LONGINT size;
    Handle data = NULL;
    status = GetEOF(reference, &size);
    if (!status) {
        if (size < icon_family_header_size)
            status = paramErr;
        else if (!(data = NewHandle(size)))
            status = memFullErr;
    }
    if (!status) {
        HLock(data);
        LONGINT actual = size;
        status = FSRead(reference, &actual, STARH(data));
        if (!status)
            status = actual != size ? eofErr : icon_family_valid((const Byte*)STARH(data), size) ? noErr : paramErr;
        HUnlock(data);
    }
    OSErr closed = FSClose(reference);
    if (!status)
        status = closed;
    if (status && data)
        DisposHandle(data);
    if (!status)
        *family = data;
    return status;
}

static int carbon_ReadIconFile(ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* spec = icon_file_spec(call->words[0]);
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 4, 1) : NULL;
    if (!spec || !output)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("ReadIconFile", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_write_word(output, 0);
    Handle family;
    OSErr status = icon_read_disk_family(spec, &family);
    if (!status)
        icon_write_word(output, US_TO_SYN68K(family));
    return icon_status(call, status);
}

static int carbon_WriteIconFile(ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* spec = icon_file_spec(call->words[1]);
    if (!spec)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("WriteIconFile", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    uint32 size = 0;
    const Byte* bytes = icon_handle_data(call->words[0], &size);
    if (!icon_family_valid(bytes, size))
        return icon_status(call, paramErr);

    FSSpec file = *spec;
    Handle family = SYN68K_TO_US(call->words[0]);
    SignedByte state = HGetState(family);
    HLock(family);
    OSErr status = C_FSpCreate(&file, TICK("????"), TICK("icns"), 0);
    const boolean_t created = status == noErr;
    if (created || status == dupFNErr) {
        INTEGER reference;
        status = C_FSpOpenDF(&file, fsWrPerm, &reference);
        if (!status) {
            reference = CW(reference);
            LONGINT actual = size;
            status = FSWrite(reference, &actual, STARH(family));
            if (!status && actual != size)
                status = ioErr;
            if (!status)
                status = SetEOF(reference, size);
            OSErr closed = FSClose(reference);
            if (!status)
                status = closed;
        }
    }
    HSetState(family, state);
    if (status && created)
        C_FSpDelete(&file);
    return icon_status(call, status);
}

static int carbon_RegisterIconRefFromIconFile(ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* spec = icon_file_spec(call->words[2]);
    void* output = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], 4, 1) : NULL;
    if (!spec || !output)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    IconReference* icon = icon_find_key(call->words[0], call->words[1]);
    if (!icon && icon_saved_reference("RegisterIconRefFromIconFile", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_write_word(output, 0);
    if (icon && icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);
    Handle family;
    OSErr status = icon_read_disk_family(spec, &family);
    if (status)
        return icon_status(call, status);
    icon = icon_register(call->words[0], call->words[1], (const Byte*)STARH(family), GetHandleSize(family));
    DisposHandle(family);
    if (icon) {
        icon->source = icon_data_fork;
        icon->source_file = *spec;
    }
    return icon ? icon_return_reference(call, icon, output) : icon_status(call, memFullErr);
}

static OSErr icon_read_resource_family(const FSSpec* spec, int16 id, Byte** output, uint32* size)
{
    FSSpec source = *spec;
    const INTEGER previous = C_CurResFile();
    const BOOLEAN loading = ResLoad;
    Handle previous_top = MR(TopMapHndl);
    INTEGER file = C_FSpOpenResFile(&source, fsRdPerm);
    if (file == -1)
        return C_ResError();
    C_UseResFile(file);
    C_SetResLoad(TRUE);
    Byte packed[classic_icon_family_capacity];
    const Byte* bytes = icon_map_family(id, packed, size);
    Byte* copy = bytes ? malloc(*size) : NULL;
    if (copy)
        memcpy(copy, bytes, *size);
    const OSErr status = copy ? noErr : bytes ? memFullErr : resNotFound;
    if (MR(TopMapHndl) != previous_top)
        C_CloseResFile(file);
    C_UseResFile(previous);
    C_SetResLoad(loading);
    *output = copy;
    return status;
}

static int carbon_RegisterIconRefFromResource(ROMlib_carbon_ppc_frame* call)
{
    const FSSpec* spec = icon_file_spec(call->words[2]);
    void* output = call->words[4] ? ROMlib_carbon_try_guest_memory(call->words[4], 4, 1) : NULL;
    if (!spec || !output)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    IconReference* icon = icon_find_key(call->words[0], call->words[1]);
    if (!icon && icon_saved_reference("RegisterIconRefFromResource", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_write_word(output, 0);
    if (icon && icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);

    Byte* bytes;
    uint32 size;
    OSErr status = icon_read_resource_family(spec, (int16)call->words[3], &bytes, &size);
    if (status)
        return icon_status(call, status);
    icon = icon_register_owned(call->words[0], call->words[1], bytes, size);
    if (icon) {
        icon->source = icon_resource_fork;
        icon->source_file = *spec;
        icon->resource_id = (int16)call->words[3];
    }
    return icon ? icon_return_reference(call, icon, output) : icon_status(call, memFullErr);
}

static int carbon_OverrideIconRefFromResource(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "OverrideIconRefFromResource", invalid_icon_ref);
    const FSSpec* spec = icon_file_spec(call->words[1]);
    if (!spec)
        return icon_status(call, paramErr);

    Byte* bytes;
    uint32 size;
    OSErr status = icon_read_resource_family(spec, (int16)call->words[2], &bytes, &size);
    if (!status)
        icon_set_override(icon, bytes, size);
    return icon_status(call, status);
}

static IconReference* icon_load_system_companion(uint32 type, int16 id)
{
    int16 volume;
    int32 directory;
    Str255 name = "\020System Resources";
    FSSpec spec;
    if (C_FindFolder(kOnSystemDisk, kSystemFolderType, FALSE, &volume, &directory) ||
        C_FSMakeFSSpec(CW(volume), CL(directory), name, &spec))
        return NULL;
    Handle previous_top = MR(TopMapHndl);
    INTEGER file = C_FSpOpenResFile(&spec, fsRdPerm);
    if (file == -1)
        return NULL;
    C_UseResFile(file);
    IconReference* icon = icon_load_system_family(type, id);
    if (MR(TopMapHndl) != previous_top)
        C_CloseResFile(file);
    return icon;
}

static IconReference* icon_get_system(uint32 type)
{
    const INTEGER previous = C_CurResFile();
    const INTEGER error = ResErr;
    const BOOLEAN loading = ResLoad;
    C_UseResFile(CW(SysMap));
    C_SetResLoad(TRUE);
    Handle map = C_Get1Resource(TICK("isrv"), 128);
    const uint32 length = map ? GetHandleSize(map) : 0;
    int16 id;
    IconReference* icon = NULL;
    if (icon_resource_id(map && STARH(map) ? (const Byte*)STARH(map) : NULL, map && STARH(map) ? length : 0, type,
                         &id)) {
        icon = icon_load_system_family(type, id);
        if (!icon)
            icon = icon_load_system_companion(type, id);
    }
    C_UseResFile(previous);
    C_SetResLoad(loading);
    ResErr = error;
    return icon;
}

static int carbon_GetIconRef(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], 4, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    icon_write_word(output, 0);
    uint32 creator = call->words[1], type = call->words[2];
    IconReference* icon = icon_find_key(creator, type);
    ROMlib_carbon_reference saved;
    if (!icon && icon_saved_reference("GetIconRef", &saved)) {
        if (!ROMlib_carbon_invoke_frame(saved.function, call))
            return 0;
        if ((int16)call->words[0] != no_such_icon || creator != TICK("macs"))
            return 1;
    }
    if (!icon && creator == TICK("macs"))
        icon = icon_get_system(type);
    return icon ? icon_return_reference(call, icon, output) : icon_status(call, no_such_icon);
}

static int carbon_GetIconRefFromFolder(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("GetIconRefFromFolder", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);

    /* The native file system exposes ordinary folders and volume roots. The
       saved dispatcher handles the additional file-sharing folder variants. */
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[1] = TICK("macs");
    arguments.words[2] = call->words[2] == 2 && call->words[1] == 1 ? TICK("hdsk") : TICK("fldr");
    arguments.words[3] = call->words[5];
    const int status = carbon_GetIconRef(&arguments);
    call->words[0] = arguments.words[0];
    return status;
}

/* File families retain their real source for UpdateIconRef. Labels are queried
   separately and never participate in the reference identity. */
static IconReference* icon_file_family(const FSSpec* spec, const FinderIcon* source, uint32 creator, uint32 type)
{
    const Byte* planes[] = {source->mono,   source->small,  source->color4,
                            source->color8, source->small4, source->small8};
    Byte packed[classic_icon_family_capacity];
    uint32 used = icon_family_header_size;
    for (unsigned i = 0; i < 6; ++i)
        if (source->present & (1U << i)) {
            const uint32 length = finder_icon_sizes[i];
            icon_write_word(packed + used, finder_icon_types[i]);
            icon_write_word(packed + used + 4, length + icon_family_header_size);
            memcpy(packed + used + icon_family_header_size, planes[i], length);
            used += length + icon_family_header_size;
        }
    icon_write_word(packed, TICK("icns"));
    icon_write_word(packed + 4, used);
    for (IconReference* icon = icon_references; icon; icon = icon->next)
        if (icon->source == icon_catalog && icon->source_file.vRefNum == spec->vRefNum &&
            icon->source_file.parID == spec->parID &&
            EqualString(icon->source_file.name, (StringPtr)spec->name, FALSE, TRUE)) {
            if (icon->size != used || memcmp(icon->bytes, packed, used)) {
                Byte* copy = malloc(used);
                if (!copy)
                    return NULL;
                memcpy(copy, packed, used);
                free(icon->bytes);
                icon->bytes = copy;
                icon->size = used;
            }
            icon->creator = creator;
            icon->type = type;
            return icon;
        }
    IconReference* icon = calloc(1, sizeof *icon);
    Byte* copy = malloc(used);
    Handle identity = NewHandleSys(0);
    if (!icon || !copy || !identity) {
        free(icon);
        free(copy);
        if (identity)
            DisposHandle(identity);
        return NULL;
    }
    memcpy(copy, packed, used);
    icon->identity = identity;
    icon->source = icon_catalog;
    icon->source_file = *spec;
    icon->creator = creator;
    icon->type = type;
    icon->bytes = copy;
    icon->size = used;
    icon->next = icon_references;
    icon_references = icon;
    return icon;
}

static OSErr icon_file_catalog(const FSSpec* spec, CInfoPBRec* info)
{
    Str63 name;
    memcpy(name, spec->name, spec->name[0] + 1);
    memset(info, 0, sizeof *info);
    PP_ASSIGN(info->hFileInfo.ioNamePtr, (Ptr)name);
    info->hFileInfo.ioVRefNum = spec->vRefNum;
    info->hFileInfo.ioDirID = spec->parID;
    return PBGetCatInfo(info, FALSE);
}

static boolean_t icon_file_artwork(const FSSpec* spec, const CInfoPBRec* info, FinderIcon* family)
{
    const boolean_t folder = (info->hFileInfo.ioFlAttrib & 0x10) != 0;
    const uint16 flags = CW(folder ? info->dirInfo.ioDrUsrWds.frFlags : info->hFileInfo.ioFlFndrInfo.fdFlags);
    const boolean_t custom = (flags & 0x400) != 0 && icon_custom_enabled(CW(spec->vRefNum));
    if (folder && !custom)
        return FALSE;
    const uint32 creator = folder ? 0 : CL(info->hFileInfo.ioFlFndrInfo.fdCreator);
    const uint32 type = folder ? TICK("fldr") : CL(info->hFileInfo.ioFlFndrInfo.fdType);
    FSSpec artwork = *spec;
    if (folder) {
        artwork.parID = info->dirInfo.ioDrDirID;
        memcpy(artwork.name, "\005Icon\r", 6);
    }
    return ROMlib_read_file_icon(&artwork, creator, type, custom, family) ||
           (!folder && ROMlib_read_icon_data(&artwork, family, NULL));
}

static IconReference* icon_file_default(const CInfoPBRec* info)
{
    const boolean_t folder = (info->hFileInfo.ioFlAttrib & 0x10) != 0;
    const uint32 type = CL(info->hFileInfo.ioFlFndrInfo.fdType);
    const uint32 generic = folder                 ? (CL(info->dirInfo.ioDrDirID) == 2 ? TICK("hdsk") : TICK("fldr"))
                           : type == TICK("APPL") ? TICK("APPL")
                                                  : TICK("docu");
    IconReference* icon = icon_find_key(TICK("macs"), generic);
    return icon ? icon : icon_get_system(generic);
}

static OSErr icon_replace_family(IconReference* icon, const Byte* bytes, uint32 size)
{
    Byte* copy = malloc(size);
    if (!copy)
        return memFullErr;
    memcpy(copy, bytes, size);
    free(icon->bytes);
    icon->bytes = copy;
    icon->size = size;
    return noErr;
}

static OSErr icon_reload(IconReference* icon)
{
    switch (icon->source) {
    case icon_memory:
        return noErr;
    case icon_system: {
        const IconReference* loaded = icon_get_system(icon->type);
        if (!loaded)
            return no_such_icon;
        return loaded == icon ? noErr : icon_replace_family(icon, loaded->bytes, loaded->size);
    }
    case icon_catalog: {
        CInfoPBRec info;
        OSErr error = icon_file_catalog(&icon->source_file, &info);
        if (error)
            return error;
        const boolean_t folder = (info.hFileInfo.ioFlAttrib & 0x10) != 0;
        const uint32 creator = folder ? 0 : CL(info.hFileInfo.ioFlFndrInfo.fdCreator);
        const uint32 type = folder ? TICK("fldr") : CL(info.hFileInfo.ioFlFndrInfo.fdType);
        FinderIcon family;
        if (icon_file_artwork(&icon->source_file, &info, &family))
            return icon_file_family(&icon->source_file, &family, creator, type) ? noErr : memFullErr;
        IconReference* generic = icon_file_default(&info);
        if (!generic)
            return no_such_icon;
        error = generic->bytes ? noErr : icon_reload(generic);
        return error ? error : icon_replace_family(icon, generic->bytes, generic->size);
    }
    case icon_data_fork: {
        Handle image;
        OSErr error = icon_read_disk_family(&icon->source_file, &image);
        if (error)
            return error;
        error = icon_replace_family(icon, (const Byte*)STARH(image), GetHandleSize(image));
        DisposHandle(image);
        return error;
    }
    case icon_resource_fork: {
        Byte* bytes;
        uint32 size;
        OSErr error = icon_read_resource_family(&icon->source_file, icon->resource_id, &bytes, &size);
        if (error)
            return error;
        free(icon->bytes);
        icon->bytes = bytes;
        icon->size = size;
        return noErr;
    }
    }
    return invalid_icon_ref;
}

static int carbon_UpdateIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    return icon ? icon_status(call, icon_reload(icon)) : icon_fallback(call, "UpdateIconRef", invalid_icon_ref);
}

static void icon_flush(IconReference* icon)
{
    free(icon->bytes);
    icon->bytes = NULL;
    icon->size = 0;
}

static int carbon_FlushIconRefs(ROMlib_carbon_ppc_frame* call)
{
    const uint32 creator = call->words[0], type = call->words[1];
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("FlushIconRefs", &saved)) {
        if (!ROMlib_carbon_invoke_frame(saved.function, call))
            return 0;
        if (call->words[0])
            return 1;
    }
    const uint32 wildcard = TICK("****");
    for (IconReference* icon = icon_references; icon; icon = icon->next) {
        if (icon->source != icon_memory && (creator == wildcard || creator == icon->creator) &&
            (type == wildcard || type == icon->type))
            icon_flush(icon);
    }
    return icon_status(call, noErr);
}

static OSErr icon_volume(int16 reference, int16* volume)
{
    if (reference == kOnSystemDisk) {
        int16 encoded;
        int32 folder;
        OSErr error = C_FindFolder(kOnSystemDisk, kSystemFolderType, FALSE, &encoded, &folder);
        if (!error)
            *volume = CW(encoded);
        return error;
    }
    HParamBlockRec block = {0};
    block.volumeParam.ioVRefNum = CW(reference);
    OSErr error = PBHGetVInfo(&block, FALSE);
    if (!error)
        *volume = CW(block.volumeParam.ioVRefNum);
    return error;
}

static int carbon_GetIconDevice(ROMlib_carbon_ppc_frame* call)
{
    uint32_t ignored;
    if (call->words[0] && !ROMlib_carbon_guest_memory(call->words[0], sizeof(uint32_t), 1))
        return 0;
    return ROMlib_carbon_call_toolbox_vector(0x1d24, 0x388, 0x220, call, &ignored);
}

static int carbon_SetIconDevice(ROMlib_carbon_ppc_frame* call)
{
    uint32_t ignored;
    return ROMlib_carbon_call_toolbox_vector(0x1d24, 0x388, 0x218, call, &ignored);
}

static int carbon_FlushIconRefsByVolume(ROMlib_carbon_ppc_frame* call)
{
    const int16 reference = (int16)call->words[0];
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("FlushIconRefsByVolume", &saved)) {
        if (!ROMlib_carbon_invoke_frame(saved.function, call))
            return 0;
        if (call->words[0])
            return 1;
    }
    int16 volume;
    OSErr error = icon_volume(reference, &volume);
    if (error)
        return icon_status(call, error);
    for (IconReference* icon = icon_references; icon; icon = icon->next) {
        if (icon->source == icon_memory)
            continue;
        const int16 origin = icon->source == icon_system ? kOnSystemDisk : CW(icon->source_file.vRefNum);
        int16 origin_volume;
        if (!icon_volume(origin, &origin_volume) && volume == origin_volume)
            icon_flush(icon);
    }
    return icon_status(call, noErr);
}

static int carbon_GetIconRefFromFile(ROMlib_carbon_ppc_frame* call)
{
    FSSpec* spec = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *spec, 0) : NULL;
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 4, 1) : NULL;
    void* label = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 2, 1) : NULL;
    if (!spec || spec->name[0] > 63 || !output || !label)
        return icon_status(call, paramErr);
    icon_write_word(output, 0);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("GetIconRefFromFile", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    const FSSpec file = *spec;
    CInfoPBRec info;
    OSErr error = icon_file_catalog(&file, &info);
    if (error)
        return icon_status(call, error);
    const boolean_t folder = (info.hFileInfo.ioFlAttrib & 0x10) != 0;
    const uint16 flags = CW(folder ? info.dirInfo.ioDrUsrWds.frFlags : info.hFileInfo.ioFlFndrInfo.fdFlags);
    const boolean_t custom = (flags & 0x400) != 0 && icon_custom_enabled(CW(file.vRefNum));
    const uint32 creator = folder ? 0 : CL(info.hFileInfo.ioFlFndrInfo.fdCreator);
    const uint32 type = folder ? TICK("fldr") : CL(info.hFileInfo.ioFlFndrInfo.fdType);
    IconReference* icon = custom ? NULL : icon_find_key(creator, type);
    FinderIcon family;
    if (!icon && icon_file_artwork(&file, &info, &family)) {
        icon = icon_file_family(&file, &family, creator, type);
        if (!icon)
            return icon_status(call, memFullErr);
    }
    if (!icon)
        icon = icon_file_default(&info);
    if (!icon)
        return icon_status(call, no_such_icon);
    if (icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);
    const uint16 value = CW((flags >> 1) & 7);
    memcpy(label, &value, sizeof value);
    return icon_return_reference(call, icon, output);
}

static int carbon_IsValidIconRef(ROMlib_carbon_ppc_frame* call)
{
    if (icon_find(call->words[0]))
        return icon_status(call, TRUE);
    return call->words[0] ? icon_fallback(call, "IsValidIconRef", FALSE) : icon_status(call, FALSE);
}

static int carbon_AcquireIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "AcquireIconRef", invalid_icon_ref);
    if (icon->owners == UINT16_MAX)
        return icon_status(call, memFullErr);
    ++icon->owners;
    return icon_status(call, noErr);
}

static int carbon_ReleaseIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "ReleaseIconRef", invalid_icon_ref);
    if (!icon->owners)
        return icon_status(call, invalid_icon_ref);
    if (!--icon->owners && !icon->registered)
        icon_destroy(icon);
    return icon_status(call, noErr);
}

static int carbon_GetIconRefOwners(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "GetIconRefOwners", invalid_icon_ref);
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], 2, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    uint16 owners = CW(icon->owners);
    memcpy(output, &owners, 2);
    return icon_status(call, noErr);
}

static int carbon_GetIconFamilyData(ROMlib_carbon_ppc_frame* call)
{
    uint32 size = 0;
    const Byte* bytes = icon_handle_data(call->words[0], &size);
    uint32 ignored;
    if (!icon_family_valid(bytes, size) || !icon_handle_data(call->words[2], &ignored))
        return icon_status(call, paramErr);
    uint32 offset = icon_family_element(bytes, size, call->words[1]);
    const uint32 stored_length = offset == size ? 0 : icon_read_word(bytes + offset + 4) - icon_family_header_size;
    const unsigned rgb_width = stored_length ? icon_rgb_width(call->words[1]) : 0;
    const uint32 length = rgb_width ? 4 * rgb_width * rgb_width : stored_length;
    Byte* copy = length ? malloc(length) : NULL;
    if (length && !copy)
        return icon_status(call, memFullErr);
    if (rgb_width) {
        if (!icon_rgb_decode(bytes + offset + icon_family_header_size, stored_length, rgb_width, copy)) {
            free(copy);
            return icon_status(call, paramErr);
        }
    }
    else if (length)
        memcpy(copy, bytes + offset + icon_family_header_size, length);
    Handle output = SYN68K_TO_US(call->words[2]);
    SetHandleSize(output, length);
    OSErr status = MemError();
    if (!status && length)
        memcpy(STARH(output), copy, length);
    free(copy);
    return icon_status(call, status);
}

static int carbon_SetIconFamilyData(ROMlib_carbon_ppc_frame* call)
{
    uint32 size = 0, length = 0;
    const Byte* bytes = icon_handle_data(call->words[0], &size);
    const Byte* data = icon_handle_data(call->words[2], &length);
    if (!bytes || !data || (size && !icon_family_valid(bytes, size)))
        return icon_status(call, paramErr);
    Byte empty[icon_family_header_size];
    if (!size) {
        size = sizeof empty;
        icon_write_word(empty, TICK("icns"));
        icon_write_word(empty + 4, size);
        bytes = empty;
    }
    uint32 offset = icon_family_element(bytes, size, call->words[1]);
    uint32 old_chunk = offset < size ? icon_read_word(bytes + offset + 4) : 0;
    const unsigned rgb_width = length ? icon_rgb_width(call->words[1]) : 0;
    Byte* encoded = NULL;
    if (rgb_width) {
        if (length != 4 * rgb_width * rgb_width)
            return icon_status(call, paramErr);
        encoded = malloc(3 * rgb_width * rgb_width + (rgb_width == 128 ? 4 : 0));
        if (!encoded)
            return icon_status(call, memFullErr);
        length = icon_rgb_encode(data, rgb_width, encoded);
        data = encoded;
    }
    if ((uint64)size - old_chunk + length + icon_family_header_size > INT32_MAX) {
        free(encoded);
        return icon_status(call, memFullErr);
    }
    uint32 new_chunk = length + icon_family_header_size;
    uint32 full_size = size - old_chunk + new_chunk;
    Byte* copy = malloc(full_size);
    if (!copy) {
        free(encoded);
        return icon_status(call, memFullErr);
    }
    memcpy(copy, bytes, offset);
    icon_write_word(copy + offset, call->words[1]);
    icon_write_word(copy + offset + 4, new_chunk);
    memcpy(copy + offset + icon_family_header_size, data, length);
    free(encoded);
    memcpy(copy + offset + new_chunk, bytes + offset + old_chunk, size - offset - old_chunk);
    icon_write_word(copy + 4, full_size);
    Handle family = SYN68K_TO_US(call->words[0]);
    SetHandleSize(family, full_size);
    OSErr status = MemError();
    if (!status)
        memcpy(STARH(family), copy, full_size);
    free(copy);
    return icon_status(call, status);
}

static uint32 icon_family_selector(uint32 type)
{
    static const uint32 types[][5] = {
        {large1BitMask, large4BitData, large8BitData, TICK("il32"), TICK("l8mk")},
        {small1BitMask, small4BitData, small8BitData, TICK("is32"), TICK("s8mk")},
        {mini1BitMask, mini4BitData, mini8BitData, 0, 0},
        {TICK("ich#"), TICK("ich4"), TICK("ich8"), TICK("ih32"), TICK("h8mk")},
    };
    for (unsigned group = 0; group < NELEM(types); ++group)
        for (unsigned channel = 0; channel < NELEM(types[group]); ++channel)
            if (type && types[group][channel] == type)
                return 1u << (group * 8 + channel);
    return 0;
}

static int carbon_IconRefToIconFamily(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 4, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    icon_write_word(output, 0);
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "IconRefToIconFamily", invalid_icon_ref);
    /* The IconSelectorValue filters chunks of the stored family. */
    IconFamilyView view;
    OSErr status = icon_family_view(icon, &view);
    if (status)
        return icon_status(call, status);
    uint32 selector = call->words[1], used = icon_family_header_size;
    Byte* bytes = malloc(view.size);
    if (!bytes)
        return icon_status(call, memFullErr);
    for (uint32 offset = icon_family_header_size; offset < view.size;) {
        const Byte* chunk = view.bytes + offset;
        uint32 size = icon_read_word(chunk + 4);
        if (selector == UINT32_MAX || (icon_family_selector(icon_read_word(chunk)) & selector)) {
            memcpy(bytes + used, chunk, size);
            used += size;
        }
        offset += size;
    }
    icon_write_word(bytes, TICK("icns"));
    icon_write_word(bytes + 4, used);
    Handle family = NewHandle(used);
    if (family) {
        memcpy(STARH(family), bytes, used);
        icon_write_word(output, US_TO_SYN68K(family));
    }
    free(bytes);
    return icon_status(call, family ? noErr : memFullErr);
}

static OSErr icon_suite_from_family(const Byte* bytes, uint32 size, uint32 selector, Handle* output)
{
    uint32 encoded = 0;
    OSErr error = C_NewIconSuite((Handle*)&encoded);
    if (error)
        return error;
    Handle suite = SYN68K_TO_US(CL(encoded));
    for (unsigned i = 0; i < NELEM(classic_icon_elements); ++i) {
        uint32 type = classic_icon_elements[i].type;
        if (!(selector & classic_icon_elements[i].selector))
            continue;
        uint32 offset = icon_family_element(bytes, size, type);
        if (offset == size)
            continue;
        const uint32 length = classic_icon_elements[i].size;
        if (icon_read_word(bytes + offset + 4) != length + icon_family_header_size) {
            error = paramErr;
            break;
        }
        Handle element = NewHandle(length);
        if (!element) {
            error = memFullErr;
            break;
        }
        memcpy(STARH(element), bytes + offset + icon_family_header_size, length);
        error = C_AddIconToSuite(element, suite, type);
        if (error) {
            DisposHandle(element);
            break;
        }
    }
    if (error)
        C_DisposeIconSuite(suite, TRUE);
    else
        *output = suite;
    return error;
}

static int carbon_IconFamilyToIconSuite(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 4, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("IconFamilyToIconSuite", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_write_word(output, 0);
    uint32 size = 0;
    const Byte* bytes = icon_handle_data(call->words[0], &size);
    if (!icon_family_valid(bytes, size))
        return icon_status(call, paramErr);
    Byte* copy = malloc(size);
    if (!copy)
        return icon_status(call, memFullErr);
    memcpy(copy, bytes, size);
    Handle suite;
    OSErr result = icon_suite_from_family(copy, size, call->words[1], &suite);
    free(copy);
    if (!result)
        icon_write_word(output, US_TO_SYN68K(suite));
    return icon_status(call, result);
}

static int carbon_IconSuiteToIconFamily(ROMlib_carbon_ppc_frame* call)
{
    void* output = call->words[2] ? ROMlib_carbon_try_guest_memory(call->words[2], 4, 1) : NULL;
    if (!output)
        return icon_status(call, paramErr);
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("IconSuiteToIconFamily", &saved))
        return ROMlib_carbon_invoke_frame(saved.function, call);
    icon_write_word(output, 0);
    uint32 length;
    if (!icon_handle_data(call->words[0], &length) || length < NELEM(classic_icon_elements) * sizeof(Handle))
        return icon_status(call, paramErr);
    Handle suite = SYN68K_TO_US(call->words[0]);
    Byte bytes[classic_icon_family_capacity];
    uint32 used = icon_family_header_size;
    for (unsigned i = 0; i < NELEM(classic_icon_elements); ++i) {
        uint32 encoded;
        uint32 type = classic_icon_elements[i].type;
        if (!(call->words[1] & classic_icon_elements[i].selector) || C_GetIconFromSuite((Handle*)&encoded, suite, type))
            continue;
        const Byte* data = icon_handle_data(CL(encoded), &length);
        if (!data || length != classic_icon_elements[i].size)
            return icon_status(call, paramErr);
        icon_write_word(bytes + used, type);
        icon_write_word(bytes + used + 4, length + icon_family_header_size);
        memcpy(bytes + used + icon_family_header_size, data, length);
        used += length + icon_family_header_size;
    }
    Handle family = NewHandle(used);
    if (!family)
        return icon_status(call, memFullErr);
    icon_write_word(bytes, TICK("icns"));
    icon_write_word(bytes + 4, used);
    memcpy(STARH(family), bytes, used);
    icon_write_word(output, US_TO_SYN68K(family));
    return icon_status(call, noErr);
}

typedef enum { icon_draw, icon_region, icon_point, icon_rect } IconOperation;

#include "icon_rgb_draw.h"

static int icon_saved_rgb_operation(ROMlib_carbon_ppc_frame* call, IconOperation operation, const Rect* rect,
                                    int16 alignment, void* argument, const char* name)
{
    ROMlib_carbon_reference saved;
    if (icon_saved_reference("IconRefToIconFamily", &saved)) {
        _Alignas(4) uint32 packed_handle = 0;
        ROMlib_carbon_ppc_frame copy = *call;
        copy.words[0] = call->words[4];
        copy.words[1] = UINT32_MAX;
        copy.words[2] = US_TO_SYN68K(&packed_handle);
        if (!ROMlib_carbon_invoke_frame(saved.function, &copy))
            return 0;
        if (packed_handle) {
            const uint32 address = CL(packed_handle);
            Handle family = ROMlib_carbon_try_guest_memory(address, sizeof *family, 0);
            if (family) {
                HLock(family);
                uint32 size;
                const Byte* bytes = icon_handle_data(address, &size);
                IconRGBImage image;
                const OSErr status = icon_family_valid(bytes, size)
                                         ? icon_rgb_find_image((IconFamilyView){bytes, size}, rect, &image)
                                         : paramErr;
                if (!copy.words[0] && !status) {
                    const int32 result = icon_rgb_operation(operation, &image, *rect, alignment, argument);
                    DisposHandle(family);
                    return icon_status(call, result);
                }
                DisposHandle(family);
            }
        }
    }
    return icon_fallback(call, name, operation == icon_point || operation == icon_rect ? FALSE : invalid_icon_ref);
}

static int icon_ref_operation(ROMlib_carbon_ppc_frame* call, IconOperation operation, const char* name)
{
    const boolean_t drawing = operation == icon_draw;
    const boolean_t hit = operation == icon_point || operation == icon_rect;
    IconReference* icon = icon_find(call->words[4]);
    uint32 address = call->words[drawing ? 0 : 1];
    const Rect* bounds = address ? ROMlib_carbon_try_guest_memory(address, sizeof *bounds, 0) : NULL;
    uint32 input = call->words[0];
    unsigned input_size = operation == icon_point ? sizeof(Point) : operation == icon_rect ? sizeof(Rect) : 4;
    void* argument = !drawing && input ? ROMlib_carbon_try_guest_memory(input, input_size, 0) : NULL;
    const boolean_t rgb = !drawing || (icon_deep_icons_enabled && (int16)call->words[2] == ttNone);
    const int16 alignment = call->words[drawing ? 1 : 2];
    if (!icon) {
        if (rgb && bounds && (drawing || argument)) {
            const Rect rect = *bounds;
            return icon_saved_rgb_operation(call, operation, &rect, alignment, argument, name);
        }
        return icon_fallback(call, name, hit ? FALSE : invalid_icon_ref);
    }
    if (!bounds || (!drawing && !argument))
        return icon_status(call, hit ? FALSE : paramErr);
    const Rect rect = *bounds;
    IconFamilyView view;
    OSErr load_status = icon_family_view(icon, &view);
    if (load_status)
        return icon_status(call, hit ? FALSE : load_status);
    if (rgb) {
        IconRGBImage image;
        OSErr status = icon_rgb_find_image(view, &rect, &image);
        if (!status)
            return icon_status(call, icon_rgb_operation(operation, &image, rect, alignment, argument));
        if (status != no_such_icon)
            return icon_status(call, hit ? FALSE : status);
    }
    Handle suite;
    OSErr error = icon_suite_from_family(view.bytes, view.size, UINT32_MAX, &suite);
    if (error)
        return icon_status(call, hit ? FALSE : error);
    int32 result = paramErr;
    switch (operation) {
    case icon_draw:
        result = C_PlotIconSuite(&rect, alignment, (int16)call->words[2], suite);
        break;
    case icon_region:
        result = C_IconSuiteToRgn(argument, &rect, alignment, suite);
        break;
    case icon_point: {
        const Point* guest = argument;
        const Point point = {CW(guest->v), CW(guest->h)};
        result = C_PtInIconSuite(point, &rect, alignment, suite);
        break;
    }
    case icon_rect:
        result = C_RectInIconSuite(argument, &rect, alignment, suite);
        break;
    }
    C_DisposeIconSuite(suite, TRUE);
    return icon_status(call, result);
}

static int carbon_PlotIconRef(ROMlib_carbon_ppc_frame* call)
{
    return icon_ref_operation(call, icon_draw, "PlotIconRef");
}

static int carbon_PlotIconRefInPort(ROMlib_carbon_ppc_frame* call)
{
    uint32 output_port = call->words[0], address = call->words[1];
    const Rect* rect = address ? ROMlib_carbon_try_guest_memory(address, sizeof *rect, 0) : NULL;
    if (!rect)
        return icon_status(call, paramErr);
    if (RECT_WIDTH(rect) <= 0 || RECT_HEIGHT(rect) <= 0)
        return icon_status(call, noErr);
    GrafPtr target = output_port ? ROMlib_carbon_try_guest_memory(output_port, sizeof(GrafPort), 0) : NULL;
    if (output_port && !target)
        return icon_status(call, paramErr);
    GrafPtr previous = thePort;
    ROMlib_carbon_ppc_frame plot = *call;
    plot.words[0] = call->words[1];
    plot.words[1] = call->words[2];
    plot.words[2] = call->words[3];
    plot.words[3] = call->words[5];
    plot.words[4] = call->words[6];
    if (target)
        C_SetPort(target);
    int result = carbon_PlotIconRef(&plot);
    if (target)
        C_SetPort(previous);
    call->words[0] = plot.words[0];
    return result;
}

static int carbon_PtInIconRef(ROMlib_carbon_ppc_frame* call)
{
    return icon_ref_operation(call, icon_point, "PtInIconRef");
}

static int carbon_RectInIconRef(ROMlib_carbon_ppc_frame* call)
{
    return icon_ref_operation(call, icon_rect, "RectInIconRef");
}

static int carbon_IconRefToRgn(ROMlib_carbon_ppc_frame* call)
{
    return icon_ref_operation(call, icon_region, "IconRefToRgn");
}

static int carbon_GetIconSizesFromIconRef(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[3]);
    if (!icon)
        return icon_fallback(call, "GetIconSizesFromIconRef", invalid_icon_ref);
    IconFamilyView view;
    OSErr status = icon_family_view(icon, &view);
    if (status)
        return icon_status(call, status);
    uint32 available = 0;
    for (uint32 offset = icon_family_header_size; offset < view.size; offset += icon_read_word(view.bytes + offset + 4))
        available |= icon_family_selector(icon_read_word(view.bytes + offset));
    return icon_status(call,
                       call->words[1] && cfp_write(call->words[1], available & call->words[0]) ? noErr : paramErr);
}

static int carbon_IsIconRefMaskEmpty(ROMlib_carbon_ppc_frame* call)
{
    IconReference* icon = icon_find(call->words[0]);
    if (!icon)
        return icon_fallback(call, "IsIconRefMaskEmpty", TRUE);
    IconFamilyView view;
    if (icon_family_view(icon, &view))
        return icon_status(call, TRUE);
    for (uint32 offset = icon_family_header_size; offset < view.size;) {
        const Byte* element = view.bytes + offset;
        uint32 selector = icon_family_selector(icon_read_word(element));
        uint32 chunk = icon_read_word(element + 4), length = chunk - icon_family_header_size;
        if (selector & 0x11111111) {
            uint32 start = selector & 0x01010101 ? length / 2 : 0;
            for (uint32 i = start; i < length; ++i)
                if (element[icon_family_header_size + i])
                    return icon_status(call, FALSE);
        }
        offset += chunk;
    }
    return icon_status(call, TRUE);
}

#endif
