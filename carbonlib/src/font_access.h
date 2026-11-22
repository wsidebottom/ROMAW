/*+===================================================================
  File:      font_access.h

  Summary:   Carbon Font Manager access to the classic resource-backed font catalog.

  Classes:   None.

  Functions: Private frame adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 FontManager re-exports, System 9.2.1 FontObjects,
             Universal Interfaces 3.4.2 ATSTypes.h and Fonts.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_FONT_ACCESS_H
#define ROMAW_CARBON_FONT_ACCESS_H

#include "internal.h"
#include "file_manager_entries.h"
#include "rsys/resource.h"
#include "toolbox_vectors.h"

enum {
    font_iteration_complete = -980,
    font_invalid_family = -981,
    font_table_error = -984,
    font_container_error = -985,
    font_iteration_changed = -983,
    family_iterator_size = 64
};

typedef struct {
    uint32_t signature;
    uint32_t generation;
    uint32_t type;
    uint32_t index;
} carbon_family_iterator;

#define CARBON_FAMILY_ITERATOR_SIGNATURE CLC(TICK("FMit"))

static const uint32_t family_resource_types[] = {TICK("FOND"), TICK("FONT"), TICK("NFNT")};

static int font_result(ROMlib_carbon_ppc_frame* call, int32_t status)
{
    call->words[0] = status;
    return 1;
}

static int font_reference(ROMlib_carbon_ppc_frame* call, const char* name, int32_t unavailable)
{
    ROMlib_carbon_reference reference;
    if (ROMlib_carbon_load_reference("FontManager", name, &reference))
        return ROMlib_carbon_invoke_frame(reference.function, call);
    return font_result(call, unavailable);
}

static int font_iterator_empty(const void* memory)
{
    static const unsigned char empty[family_iterator_size];
    return !memcmp(memory, empty, sizeof empty);
}

static int font_initialize_iterator(ROMlib_carbon_ppc_frame* call, uint32_t signature, const char* create,
                                    const char* reset)
{
    void* memory = call->words[3] ? ROMlib_carbon_try_guest_memory(call->words[3], family_iterator_size, 1) : NULL;
    if (!memory)
        return font_result(call, paramErr);
    if (reset) {
        carbon_family_iterator current;
        memcpy(&current, memory, sizeof current);
        if (current.signature != signature) {
            if (font_iterator_empty(memory))
                return font_result(call, paramErr);
            return font_reference(call, reset, paramErr);
        }
    }
    if (call->words[0] || call->words[2]) {
        /* A native iterator owns no allocation. Replace it with a real
           iterator only after the saved constructor accepts the filter. */
        unsigned char snapshot[family_iterator_size];
        memcpy(snapshot, memory, sizeof snapshot);
        int ok = font_reference(call, create, paramErr);
        if (!ok || call->words[0])
            memcpy(memory, snapshot, sizeof snapshot);
        return ok;
    }
    const carbon_family_iterator iterator = {signature, CL(ROMlib_resource_map_generation), 0, 0};
    memset(memory, 0, family_iterator_size);
    memcpy(memory, &iterator, sizeof iterator);
    return font_result(call, noErr);
}

static int carbon_FMCreateFontFamilyIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_iterator(call, CARBON_FAMILY_ITERATOR_SIGNATURE, "FMCreateFontFamilyIterator", NULL);
}

static int carbon_FMResetFontFamilyIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_iterator(call, CARBON_FAMILY_ITERATOR_SIGNATURE, "FMCreateFontFamilyIterator",
                                    "FMResetFontFamilyIterator");
}

static int font_dispose_iterator(ROMlib_carbon_ppc_frame* call, uint32_t signature, const char* original)
{
    void* memory = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], family_iterator_size, 1) : NULL;
    if (!memory)
        return font_result(call, paramErr);
    carbon_family_iterator current;
    memcpy(&current, memory, sizeof current);
    if (font_iterator_empty(memory))
        return font_result(call, paramErr);
    if (current.signature != signature)
        return font_reference(call, original, paramErr);
    memset(memory, 0, family_iterator_size);
    return font_result(call, noErr);
}

static int carbon_FMDisposeFontFamilyIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_dispose_iterator(call, CARBON_FAMILY_ITERATOR_SIGNATURE, "FMDisposeFontFamilyIterator");
}

static int font_resource(uint32_t type, int16_t id, uint32_t* handle)
{
    ROMlib_carbon_ppc_frame arguments = {.words = {type, id}};
    if (!carbon_vector_GetResource(&arguments))
        return 0;
    *handle = arguments.words[0];
    return 1;
}

/* Resolve the same resource as the Font Manager's name lookup. A size-zero
   legacy FONT/NFNT can name a family. Its nonzero-size strikes cannot. */
static int font_family_candidate(unsigned type, uint32_t indexed, int16_t resource_id, int16_t* family)
{
    uint32_t visible;
    if (type && ((uint16_t)resource_id & 127))
        return 0;
    *family = type ? (uint16_t)resource_id >> 7 : resource_id;
    if (*family == -1)
        return 0;

    for (unsigned earlier = 0; earlier < type; ++earlier) {
        const int16_t id = earlier ? resource_id : *family;
        if (!font_resource(family_resource_types[earlier], id, &visible))
            return -1;
        if (visible)
            return 0;
    }
    if (!font_resource(family_resource_types[type], resource_id, &visible))
        return -1;
    return visible == indexed;
}

static int font_next_resource_family(carbon_family_iterator* iterator, int16_t* family, int32_t* status)
{
    unsigned type = CL(iterator->type);
    unsigned index = CL(iterator->index);
    if (type > sizeof family_resource_types / sizeof *family_resource_types || index > INT16_MAX) {
        *status = paramErr;
        return 1;
    }
    for (; type < sizeof family_resource_types / sizeof *family_resource_types; ++type, index = 0) {
        const uint32_t resource_type = family_resource_types[type];
        ROMlib_carbon_ppc_frame arguments = {.words = {resource_type}};
        if (!carbon_vector_CountResources(&arguments))
            return 0;
        const int32_t count = arguments.words[0];
        while (index < (unsigned)(count > 0 ? count : 0)) {
            arguments.words[0] = resource_type;
            arguments.words[1] = ++index;
            if (!carbon_vector_GetIndResource(&arguments))
                return 0;
            const uint32_t indexed = arguments.words[0];
            if (!indexed)
                continue;
            int16_t encoded;
            ROMlib_carbon_ppc_frame info = {.words = {indexed, US_TO_SYN68K(&encoded)}};
            if (!carbon_vector_GetResInfo(&info))
                return 0;
            if (ResErr)
                continue;
            const int candidate = font_family_candidate(type, indexed, CW(encoded), family);
            if (candidate < 0)
                return 0;
            if (candidate) {
                iterator->type = CL(type);
                iterator->index = CL(index);
                *status = noErr;
                return 1;
            }
        }
    }
    iterator->type = CL(type);
    iterator->index = 0;
    *status = font_iteration_complete;
    return 1;
}

static int font_scan_family(carbon_family_iterator* iterator, int16_t* family, int32_t* status)
{
    const BOOLEAN saved_load = ResLoad;
    const INTEGER saved_error = ResErr;
    ResLoad = FALSE;
    const int ok = font_next_resource_family(iterator, family, status);
    ResLoad = saved_load;
    ResErr = saved_error;
    return ok;
}

static int carbon_FMGetNextFontFamily(ROMlib_carbon_ppc_frame* call)
{
    void* memory = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], family_iterator_size, 1) : NULL;
    if (!memory)
        return font_result(call, paramErr);
    carbon_family_iterator iterator;
    memcpy(&iterator, memory, sizeof iterator);
    if (iterator.signature != CARBON_FAMILY_ITERATOR_SIGNATURE)
        return font_reference(call, "FMGetNextFontFamily", paramErr);
    if (CL(iterator.generation) != ROMlib_resource_map_generation)
        return font_result(call, font_iteration_changed);
    void* output = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(int16_t), 1) : NULL;
    if (!output)
        return font_result(call, paramErr);

    int16_t family;
    int32_t status;
    const int ok = font_scan_family(&iterator, &family, &status);
    if (!ok)
        return 0;
    if (CL(iterator.generation) != ROMlib_resource_map_generation)
        return font_result(call, font_iteration_changed);
    memcpy(memory, &iterator, sizeof iterator);
    if (!status) {
        const int16_t encoded = CW(family);
        memcpy(output, &encoded, sizeof encoded);
    }
    return font_result(call, status);
}

static int carbon_FMGetFontFamilyFromName(ROMlib_carbon_ppc_frame* call)
{
    const Byte* name = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], 1, 0) : NULL;
    if (!name || !ROMlib_carbon_try_guest_memory(call->words[0], (size_t)name[0] + 1, 0))
        return font_result(call, -1);
    int16_t family = CWC(-1);
    ROMlib_carbon_ppc_frame arguments = {.words = {call->words[0], US_TO_SYN68K(&family)}};
    const BOOLEAN saved_load = ResLoad;
    const INTEGER saved_error = ResErr;
    const int ok = carbon_vector_GetFNum(&arguments);
    const INTEGER error = ResErr;
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!ok)
        return 0;
    if (error)
        return font_reference(call, "FMGetFontFamilyFromName", -1);
    return font_result(call, CW(family));
}

static int carbon_FMGetFontFamilyName(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[1] || !ROMlib_carbon_try_guest_memory(call->words[1], 1, 1))
        return font_result(call, paramErr);
    Byte name[256] = {0};
    const BOOLEAN saved_load = ResLoad;
    const INTEGER saved_error = ResErr;
    const int16_t family = call->words[0];
    const int16_t requested = family == 0 ? CW(SysFontFam) : family == 1 ? CW(ApFontID) : family;
    uint32_t handle;
    ResLoad = FALSE;
    int ok = font_resource(TICK("FOND"), requested, &handle);
    if (ok && handle) {
        ROMlib_carbon_ppc_frame info = {.words = {handle, 0, 0, US_TO_SYN68K(name)}};
        ok = carbon_vector_GetResInfo(&info);
    }
    else if (ok) {
        ROMlib_carbon_ppc_frame lookup = {.words = {call->words[0], US_TO_SYN68K(name)}};
        ok = carbon_vector_GetFontName(&lookup);
    }
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!ok)
        return 0;
    if (!name[0])
        return font_reference(call, "FMGetFontFamilyName", font_invalid_family);
    const size_t size = (size_t)name[0] + 1;
    void* output = ROMlib_carbon_try_guest_memory(call->words[1], size, 1);
    if (!output)
        return font_result(call, paramErr);
    memcpy(output, name, size);
    return font_result(call, noErr);
}

typedef struct {
    uint32_t signature, generation, family, next_key;
} carbon_font_instance_iterator;

typedef struct {
    const Byte* bytes;
    uint32_t length;
    uint32_t handle;
} carbon_font_resource;

typedef struct {
    uint16_t size, style;
    int16_t resource_id;
} carbon_font_variant;

#define CARBON_FONT_INSTANCE_SIGNATURE CLC(TICK("FMin"))
#define CARBON_FONT_ID UINT32_C(0xe3000000)
#define CARBON_FONT_ID_MASK UINT32_C(0xff000000)
#define CARBON_FONT_OUTLINE 0x80

static uint16_t font_read16(const Byte* bytes)
{
    return (uint16_t)bytes[0] << 8 | bytes[1];
}

static uint32_t font_read32(const Byte* bytes)
{
    return (uint32_t)font_read16(bytes) << 16 | font_read16(bytes + 2);
}

static void font_store16(void* output, uint16_t value)
{
    if (output) {
        const uint16_t word = CW(value);
        memcpy(output, &word, sizeof word);
    }
}

static void font_store32(void* output, uint32_t value)
{
    if (output) {
        const uint32_t word = CL(value);
        memcpy(output, &word, sizeof word);
    }
}

static int16_t font_actual_family(int16_t family)
{
    return family == 0 ? CW(SysFontFam) : family == 1 ? CW(ApFontID) : family;
}

/* Resource data is borrowed until the next Toolbox call. Always take the
   length from the loaded handle so compressed resources work as well. */
static int font_read_resource(uint32_t type, int16_t id, carbon_font_resource* result)
{
    const BOOLEAN saved_load = ResLoad;
    const INTEGER saved_error = ResErr;
    uint32_t handle = 0;
    ResLoad = TRUE;
    int ok = font_resource(type, id, &handle);
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!ok || !handle)
        return 0;
    ROMlib_carbon_ppc_frame query = {.words = {handle}};
    if (!carbon_vector_GetHandleSize(&query) || (int32_t)query.words[0] <= 0)
        return 0;
    const Byte* master = ROMlib_carbon_try_guest_memory(handle, 4, 0);
    const uint32_t address = master ? font_read32(master) : 0;
    if (!address)
        return 0;
    const Byte* contents = ROMlib_carbon_try_guest_memory(address, query.words[0], 0);
    if (!contents)
        return 0;
    *result = (carbon_font_resource){contents, query.words[0], handle};
    return 1;
}

static int font_family_associations(int16_t family, carbon_font_resource* fond, unsigned* count)
{
    if (!font_read_resource(TICK("FOND"), family, fond) || fond->length < sizeof(FamRec) + 2)
        return 0;
    *count = (unsigned)font_read16(fond->bytes + sizeof(FamRec)) + 1;
    return *count <= (fond->length - sizeof(FamRec) - 2) / 6;
}

static carbon_font_variant font_variant(const carbon_font_resource* fond, unsigned at)
{
    const Byte* entry = fond->bytes + sizeof(FamRec) + 2 + at * 6;
    return (carbon_font_variant){font_read16(entry), font_read16(entry + 2), font_read16(entry + 4)};
}

static int font_variant_valid(carbon_font_variant variant)
{
    return variant.size <= INT16_MAX && variant.style < CARBON_FONT_OUTLINE;
}

static uint32_t font_variant_id(int16_t family, carbon_font_variant variant)
{
    return CARBON_FONT_ID | (uint32_t)(uint16_t)family << 8 | variant.style | (variant.size ? 0 : CARBON_FONT_OUTLINE);
}

static int font_is_native(uint32_t font)
{
    return (font & CARBON_FONT_ID_MASK) == CARBON_FONT_ID;
}

static int font_variant_resource(carbon_font_variant variant, carbon_font_resource* resource)
{
    if (!variant.size)
        return font_read_resource(TICK("sfnt"), variant.resource_id, resource);
    return font_read_resource(TICK("NFNT"), variant.resource_id, resource) ||
           font_read_resource(TICK("FONT"), variant.resource_id, resource);
}

static int font_supported_family(int16_t family)
{
    carbon_font_resource fond;
    unsigned count;
    if (!font_family_associations(family, &fond, &count))
        return 0;
    for (unsigned at = 0; at < count; ++at) {
        const carbon_font_variant variant = font_variant(&fond, at);
        if (!font_variant_valid(variant))
            return 0;
        if (!variant.size) {
            /* Size zero also describes PostScript associations. Only claim a
               native outline when its sfnt resource is actually present. */
            carbon_font_resource outline;
            unsigned current_count;
            if (!font_variant_resource(variant, &outline) || outline.length < 12 ||
                !font_family_associations(family, &fond, &current_count) || current_count != count)
                return 0;
        }
    }
    return 1;
}

static int font_find_native(uint32_t font, carbon_font_variant* variant, carbon_font_resource* resource)
{
    const int16_t family = (int16_t)(font >> 8);
    carbon_font_resource fond;
    unsigned count;
    if (!font_family_associations(family, &fond, &count))
        return 0;
    for (unsigned at = 0; at < count; ++at) {
        const carbon_font_variant current = font_variant(&fond, at);
        if (!font_variant_valid(current) || font_variant_id(family, current) != font)
            continue;
        if (font_variant_resource(current, resource)) {
            *variant = current;
            return 1;
        }
        /* A missing bitmap strike need not invalidate another size. Refresh
           the borrowed FOND pointer after the Resource Manager call. */
        if (!font_family_associations(family, &fond, &count))
            return 0;
    }
    return 0;
}

static int font_initialize_instance_iterator(ROMlib_carbon_ppc_frame* call, const char* original, int reset)
{
    void* memory = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], family_iterator_size, 1) : NULL;
    if (!memory)
        return font_result(call, paramErr);
    carbon_font_instance_iterator iterator;
    if (reset) {
        memcpy(&iterator, memory, sizeof iterator);
        if (iterator.signature != CARBON_FONT_INSTANCE_SIGNATURE) {
            if (!iterator.signature)
                return font_result(call, paramErr);
            return font_reference(call, original, paramErr);
        }
    }

    const int16_t family = font_actual_family((int16_t)call->words[0]);
    if (!font_supported_family(family)) {
        /* Preserve a usable native iterator on a failed reset. The original
           reset can only consume its own record, so use its constructor. */
        unsigned char saved[family_iterator_size];
        if (reset)
            memcpy(saved, memory, sizeof saved);
        const int ok = font_reference(call, "FMCreateFontFamilyInstanceIterator", font_invalid_family);
        if (reset && (!ok || call->words[0]))
            memcpy(memory, saved, sizeof saved);
        return ok;
    }
    iterator = (carbon_font_instance_iterator){CARBON_FONT_INSTANCE_SIGNATURE, CL(ROMlib_resource_map_generation),
                                               CL((uint32_t)(uint16_t)family), 0};
    memset(memory, 0, family_iterator_size);
    memcpy(memory, &iterator, sizeof iterator);
    return font_result(call, noErr);
}

static int carbon_FMCreateFontFamilyInstanceIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_instance_iterator(call, "FMCreateFontFamilyInstanceIterator", 0);
}

static int carbon_FMResetFontFamilyInstanceIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_instance_iterator(call, "FMResetFontFamilyInstanceIterator", 1);
}

static int carbon_FMDisposeFontFamilyInstanceIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_dispose_iterator(call, CARBON_FONT_INSTANCE_SIGNATURE, "FMDisposeFontFamilyInstanceIterator");
}

static void* font_output(uint32_t address, size_t size)
{
    return address ? ROMlib_carbon_try_guest_memory(address, size, 1) : NULL;
}

static int carbon_FMGetNextFontFamilyInstance(ROMlib_carbon_ppc_frame* call)
{
    void* memory = font_output(call->words[0], family_iterator_size);
    if (!memory)
        return font_result(call, paramErr);
    carbon_font_instance_iterator iterator;
    memcpy(&iterator, memory, sizeof iterator);
    if (!iterator.signature)
        return font_result(call, paramErr);
    if (iterator.signature != CARBON_FONT_INSTANCE_SIGNATURE)
        return font_reference(call, "FMGetNextFontFamilyInstance", paramErr);
    if (CL(iterator.generation) != ROMlib_resource_map_generation)
        return font_result(call, font_iteration_changed);

    void* font = font_output(call->words[1], 4);
    void* style = font_output(call->words[2], 2);
    void* size = font_output(call->words[3], 2);
    if (!font || (call->words[2] && !style) || (call->words[3] && !size))
        return font_result(call, paramErr);
    const int16_t family = CL(iterator.family);
    carbon_font_resource fond;
    unsigned count;
    if (!font_family_associations(family, &fond, &count))
        return font_result(call, font_invalid_family);

    uint32_t next = UINT32_MAX;
    const uint32_t minimum = CL(iterator.next_key);
    carbon_font_variant selected = {0};
    for (unsigned at = 0; at < count; ++at) {
        const carbon_font_variant current = font_variant(&fond, at);
        const uint32_t key = (uint32_t)current.style << 16 | current.size;
        if (font_variant_valid(current) && key >= minimum && key < next) {
            selected = current;
            next = key;
        }
    }
    if (next == UINT32_MAX)
        return font_result(call, font_iteration_complete);
    iterator.next_key = CL(next + 1);
    memcpy(memory, &iterator, sizeof iterator);
    font_store32(font, font_variant_id(family, selected));
    font_store16(style, selected.style);
    font_store16(size, selected.size);
    return font_result(call, noErr);
}

static int carbon_FMGetFontFromFontFamilyInstance(ROMlib_carbon_ppc_frame* call)
{
    const int16_t family = font_actual_family((int16_t)call->words[0]);
    const uint16_t requested = call->words[1];
    void* font = font_output(call->words[2], 4);
    void* style = font_output(call->words[3], 2);
    if (!font || (call->words[3] && !style))
        return font_result(call, paramErr);
    carbon_font_resource fond;
    unsigned count;
    if (!font_family_associations(family, &fond, &count))
        return font_reference(call, "FMGetFontFromFontFamilyInstance", font_invalid_family);

    /* Select the best intrinsic subset of the requested synthetic style. */
    int best_bits = -1;
    carbon_font_variant selected = {0};
    for (unsigned at = 0; at < count; ++at) {
        const carbon_font_variant current = font_variant(&fond, at);
        if (!font_variant_valid(current) || (current.style & ~requested))
            continue;
        const int bits = __builtin_popcount(current.style);
        if (bits > best_bits || (bits == best_bits && current.size < selected.size)) {
            selected = current;
            best_bits = bits;
        }
    }
    carbon_font_resource resource;
    if (best_bits < 0 || !font_variant_resource(selected, &resource))
        return font_reference(call, "FMGetFontFromFontFamilyInstance", font_invalid_family);
    font_store32(font, font_variant_id(family, selected));
    font_store16(style, selected.style);
    return font_result(call, noErr);
}

static int carbon_FMGetFontFamilyInstanceFromFont(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t font = call->words[0];
    if (!font_is_native(font))
        return font_reference(call, "FMGetFontFamilyInstanceFromFont", paramErr);
    carbon_font_variant variant;
    carbon_font_resource resource;
    if (!font_find_native(font, &variant, &resource))
        return font_result(call, paramErr);
    void* family = font_output(call->words[1], 2);
    void* style = font_output(call->words[2], 2);
    if ((call->words[1] && !family) || (call->words[2] && !style))
        return font_result(call, paramErr);
    font_store16(family, (uint16_t)(font >> 8));
    font_store16(style, variant.style);
    return font_result(call, noErr);
}

static int carbon_FMGetFontFormat(ROMlib_carbon_ppc_frame* call)
{
    if (!font_is_native(call->words[0]))
        return font_reference(call, "FMGetFontFormat", paramErr);
    carbon_font_variant variant;
    carbon_font_resource resource;
    if (!font_find_native(call->words[0], &variant, &resource))
        return font_result(call, paramErr);
    void* format = font_output(call->words[1], 4);
    if (call->words[1] && !format)
        return font_result(call, paramErr);
    if (format) {
        if (!variant.size && resource.length < 4)
            return font_result(call, font_table_error);
        const uint32_t tag = variant.size ? 0 : font_read32(resource.bytes);
        font_store32(format, tag == 0x00010000 ? TICK("true") : tag);
    }
    return font_result(call, noErr);
}

static int carbon_FMGetFontGeneration(ROMlib_carbon_ppc_frame* call)
{
    if (!font_is_native(call->words[0]))
        return font_reference(call, "FMGetFontGeneration", paramErr);
    carbon_font_variant variant;
    carbon_font_resource resource;
    void* generation = font_output(call->words[1], 4);
    if (!generation || !font_find_native(call->words[0], &variant, &resource))
        return font_result(call, paramErr);
    font_store32(generation, ROMlib_resource_map_generation);
    return font_result(call, noErr);
}

static int font_table_query(ROMlib_carbon_ppc_frame* call, int directory)
{
    const char* original = directory ? "FMGetFontTableDirectory" : "FMGetFontTable";
    if (!font_is_native(call->words[0]))
        return font_reference(call, original, paramErr);
    carbon_font_variant variant;
    carbon_font_resource resource;
    if (!font_find_native(call->words[0], &variant, &resource))
        return font_result(call, paramErr);
    if (variant.size || resource.length < 12)
        return font_result(call, font_table_error);

    const unsigned count = font_read16(resource.bytes + 4);
    if (count > (resource.length - 12) / 16)
        return font_result(call, font_table_error);
    uint32_t start = 0, length = 12 + count * 16;
    if (!directory) {
        const uint32_t tag = call->words[1];
        length = 0;
        for (unsigned at = 0; at < count; ++at) {
            const Byte* entry = resource.bytes + 12 + at * 16;
            if (font_read32(entry) == tag) {
                start = font_read32(entry + 8);
                length = font_read32(entry + 12);
                break;
            }
        }
        if (!length || start > resource.length || length > resource.length - start)
            return font_result(call, font_table_error);
    }
    const uint32_t position = directory ? 0 : call->words[2];
    const uint32_t capacity = call->words[directory ? 1 : 3];
    const uint32_t buffer = call->words[directory ? 2 : 4];
    const uint32_t length_address = call->words[directory ? 3 : 5];
    void* actual = font_output(length_address, 4);
    if (length_address && !actual)
        return font_result(call, paramErr);
    if (buffer && capacity && !directory && (position > length || capacity > length - position)) {
        font_store32(actual, length);
        return font_result(call, paramErr);
    }
    const uint32_t copy = buffer ? MIN(capacity, length) : 0;
    void* output = copy ? font_output(buffer, copy) : NULL;
    if (copy && !output)
        return font_result(call, paramErr);
    font_store32(actual, length);
    if (copy)
        memmove(output, resource.bytes + start + position, copy);
    return font_result(call, noErr);
}

static int carbon_FMGetFontTableDirectory(ROMlib_carbon_ppc_frame* call)
{
    return font_table_query(call, 1);
}

static int carbon_FMGetFontTable(ROMlib_carbon_ppc_frame* call)
{
    return font_table_query(call, 0);
}

static int carbon_FMGetFontContainer(ROMlib_carbon_ppc_frame* call)
{
    if (!font_is_native(call->words[0]))
        return font_reference(call, "FMGetFontContainer", paramErr);
    carbon_font_variant variant;
    carbon_font_resource resource;
    FSSpec* output = font_output(call->words[1], sizeof *output);
    if (!output || !font_find_native(call->words[0], &variant, &resource))
        return font_result(call, paramErr);

    ROMlib_carbon_ppc_frame home = {.words = {resource.handle}};
    const INTEGER saved_error = ResErr;
    const int ok = carbon_vector_HomeResFile(&home);
    ResErr = saved_error;
    if (!ok)
        return 0;
    if ((int16_t)home.words[0] == -1)
        return font_result(call, font_container_error);

    _Alignas(128) FCBPBRec block = {0};
    _Alignas(256) Str255 name = {0};
    block.ioRefNum = CW((int16_t)home.words[0]);
    PP_RAW(block.ioNamePtr) = CL(US_TO_SYN68K(name));
    ROMlib_carbon_ppc_frame query = {.words = {US_TO_SYN68K(&block)}};
    if (!carbon_file_PBGetFCBInfoSync(&query))
        return 0;
    if ((int16_t)query.words[0] || name[0] >= sizeof output->name)
        return font_result(call, font_container_error);
    FSSpec spec = {.vRefNum = block.ioFCBVRefNum, .parID = block.ioFCBParID};
    memcpy(spec.name, name, (size_t)name[0] + 1);
    *output = spec;
    return font_result(call, noErr);
}

/* Catalog iteration returns each face once even when the FOND has
   multiple bitmap sizes for that face. The two cursors fit in the SDK's
   opaque 64-byte iterator record, with no allocation to retain. */
typedef struct {
    carbon_family_iterator catalog;
    uint32_t family;
    uint32_t next_face;
} carbon_font_iterator;

#define CARBON_FONT_ITERATOR_SIGNATURE CLC(TICK("FMft"))

static int carbon_FMCreateFontIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_iterator(call, CARBON_FONT_ITERATOR_SIGNATURE, "FMCreateFontIterator", NULL);
}

static int carbon_FMResetFontIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_initialize_iterator(call, CARBON_FONT_ITERATOR_SIGNATURE, "FMCreateFontIterator",
                                    "FMResetFontIterator");
}

static int carbon_FMDisposeFontIterator(ROMlib_carbon_ppc_frame* call)
{
    return font_dispose_iterator(call, CARBON_FONT_ITERATOR_SIGNATURE, "FMDisposeFontIterator");
}

static int font_next_face(int16_t family, uint32_t* cursor, uint32_t* font)
{
    carbon_font_resource fond;
    unsigned count;
    while (font_family_associations(family, &fond, &count)) {
        uint32_t next = UINT32_MAX;
        carbon_font_variant selected = {0};
        for (unsigned i = 0; i < count; ++i) {
            const carbon_font_variant variant = font_variant(&fond, i);
            const uint32_t key = ((uint32_t)variant.style << 1) | (variant.size != 0);
            if (font_variant_valid(variant) && key >= *cursor &&
                (key < next || (key == next && variant.size < selected.size))) {
                next = key;
                selected = variant;
            }
        }
        if (next == UINT32_MAX)
            return 0;
        *cursor = next + 1;
        const uint32_t candidate = font_variant_id(family, selected);
        carbon_font_resource source;
        if (font_find_native(candidate, &selected, &source) && (selected.size || source.length >= 12)) {
            *font = candidate;
            return 1;
        }
    }
    return 0;
}

static int carbon_FMGetNextFont(ROMlib_carbon_ppc_frame* call)
{
    void* memory = font_output(call->words[0], family_iterator_size);
    if (!memory)
        return font_result(call, paramErr);
    carbon_font_iterator iterator;
    memcpy(&iterator, memory, sizeof iterator);
    if (font_iterator_empty(memory))
        return font_result(call, paramErr);
    if (iterator.catalog.signature != CARBON_FONT_ITERATOR_SIGNATURE)
        return font_reference(call, "FMGetNextFont", paramErr);
    const uint32_t generation = CL(iterator.catalog.generation);
    if (generation != ROMlib_resource_map_generation)
        return font_result(call, font_iteration_changed);
    void* output = font_output(call->words[1], 4);
    if (!output)
        return font_result(call, paramErr);

    int16_t family = (int16_t)CL(iterator.family);
    uint32_t cursor = CL(iterator.next_face);
    if (cursor > 256)
        return font_result(call, paramErr);
    uint32_t font;
    int32_t status = noErr;
    while (!cursor || !font_next_face(family, &cursor, &font)) {
        if (!font_scan_family(&iterator.catalog, &family, &status))
            return 0;
        if (status)
            break;
        cursor = 0;
        if (font_next_face(family, &cursor, &font))
            break;
        cursor = 0;
        if (generation != ROMlib_resource_map_generation)
            return font_result(call, font_iteration_changed);
    }
    if (generation != ROMlib_resource_map_generation)
        return font_result(call, font_iteration_changed);
    iterator.family = CL((uint32_t)(uint16_t)family);
    iterator.next_face = status ? 0 : CL(cursor);
    memcpy(memory, &iterator, sizeof iterator);
    if (!status)
        font_store32(output, font);
    return font_result(call, status);
}

static int carbon_FMGetGeneration(ROMlib_carbon_ppc_frame* call)
{
    call->words[0] = ROMlib_resource_map_generation;
    return 1;
}

static int carbon_FMGetFontFamilyGeneration(ROMlib_carbon_ppc_frame* call)
{
    void* output = font_output(call->words[1], 4);
    if (!output)
        return font_result(call, paramErr);
    const int16_t family = font_actual_family((int16_t)call->words[0]);
    const BOOLEAN saved_load = ResLoad;
    const INTEGER saved_error = ResErr;
    ResLoad = FALSE;
    uint32_t handle = 0;
    int ok = font_resource(TICK("FOND"), family, &handle);
    if (ok && !handle && (uint16_t)family < 512) {
        const int16_t legacy = (uint16_t)family << 7;
        ok = font_resource(TICK("FONT"), legacy, &handle);
        if (ok && !handle)
            ok = font_resource(TICK("NFNT"), legacy, &handle);
    }
    ResLoad = saved_load;
    ResErr = saved_error;
    if (!ok)
        return 0;
    if (!handle)
        return font_reference(call, "FMGetFontFamilyGeneration", font_invalid_family);
    font_store32(output, ROMlib_resource_map_generation);
    return font_result(call, noErr);
}

#endif
