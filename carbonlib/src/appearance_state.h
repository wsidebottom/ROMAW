/*+===================================================================
  File:      appearance_state.h

  Summary:   Implements Appearance Manager client registration and preferences.

  Classes:   None.

  Functions: Private Appearance Manager entry points for the export table.

  Origin:    Mac OS 9 AppearanceLib client table, process-reference workers and preference collection.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_APPEARANCE_STATE_H
#define ROMAW_CARBON_APPEARANCE_STATE_H

#include "MemoryMgr.h"
#include "ResourceMgr.h"
#include "cf_batch.h"
#include "rsys/cquick.h"
#include "rsys/stubify.h"
#include <string.h>

enum appearance_client_operation { appearance_client_register, appearance_client_unregister, appearance_client_query };

enum { appearance_unimplemented = -4 };

static int appearance_client_result(ROMlib_carbon_ppc_frame* call, int32_t result)
{
    call->words[0] = result;
    return 1;
}

static int draw_theme_group(ROMlib_carbon_ppc_frame* call, bool primary)
{
    enum { appearance_vector = 0x17d0, appearance_slot = 0x274 };
    const Rect* source = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], sizeof *source, 0) : NULL;
    if (!source)
        return appearance_client_result(call, paramErr);
    ROMlib_carbon_ppc_frame arguments = *call;
    if (arguments.words[1] == 7 || arguments.words[1] == 8)
        arguments.words[1] = 0;

    uint32_t vector;
    if (!cfp_get(appearance_vector, &vector))
        return 0;
    if (vector && vector != (uint32_t)(uintptr_t)toolstuff[appearance_slot].orig) {
        uint32_t result;
        return ROMlib_carbon_call_toolbox_vector(appearance_vector, 0xfb8, primary ? 0x0c : 0x0d, &arguments,
                                               &result) &&
               appearance_client_result(call, result);
    }

    Rect bounds = *source;
    if (C_EmptyRect(&bounds))
        return appearance_client_result(call, noErr);

    /* Classic fallback draws an inset highlight and an outer frame. */
    const bool inactive = arguments.words[1] == 0;
    RGBColor shadow = {CWC(0x9999), CWC(0x9999), CWC(0x9999)};
    RGBColor highlight = {CWC(0xeeee), CWC(0xeeee), CWC(0xeeee)};
    if (primary && !inactive)
        shadow = (RGBColor){CWC(0x7777), CWC(0x7777), CWC(0x7777)};

    draw_state_t saved;
    draw_state_save(&saved);
    C_PenNormal();
    C_RGBForeColor(&shadow);
    C_FrameRect(&bounds);
    if (primary && RECT_WIDTH(&bounds) > 2 && RECT_HEIGHT(&bounds) > 2) {
        C_InsetRect(&bounds, 1, 1);
        C_RGBForeColor(&highlight);
        C_FrameRect(&bounds);
    }
    draw_state_restore(&saved);
    return appearance_client_result(call, noErr);
}

static int carbon_DrawThemePrimaryGroup(ROMlib_carbon_ppc_frame* call)
{
    return draw_theme_group(call, true);
}

static int carbon_DrawThemeSecondaryGroup(ROMlib_carbon_ppc_frame* call)
{
    return draw_theme_group(call, false);
}

static int appearance_reference_layout(const ROMlib_carbon_reference* reference, uint32_t offset)
{
    return reference->code_offset == offset && reference->code_size == 0x405a4 && reference->toc &&
           reference->toc_offset == 0x8000 && reference->toc_size == 0x4958;
}

static int appearance_client(const char* name, uint32_t offset, enum appearance_client_operation operation,
                             ROMlib_carbon_ppc_frame* call)
{
    enum { already_registered = -30561, not_registered = -30562, process_ref_size = 8, count_size = 2 };

    const int query = operation == appearance_client_query;
    const int add = operation == appearance_client_register;
    if (query && (!call->words[0] || !ROMlib_carbon_try_guest_memory(call->words[0], process_ref_size, 0)))
        return appearance_client_result(call, 0);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return query ? appearance_client_result(call, 0) : ROMlib_carbon_call_romaw_service(name, call);
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    /* Keep the saved AppearanceLib client table authoritative for mixed callers. */
    _Alignas(8) Byte process[process_ref_size] = {0};
    const uint32_t process_address = US_TO_SYN68K(process);
    if (query) {
        uint32_t converted;
        if (!cfp_private(&reference, 0x3bec8, call->words, 1, &converted))
            return 0;
        const void* stored = converted ? ROMlib_carbon_try_guest_memory(converted, sizeof process, 0) : NULL;
        if (!stored)
            return appearance_client_result(call, 0);
        memcpy(process, stored, sizeof process);
    }
    else if (!cfp_private(&reference, 0x3bdf0, &process_address, 1, NULL))
        return 0;

    const uint32_t slot = US_TO_SYN68K(reference.toc) - 0x75b8;
    uint32_t table, extent = 0;
    if (!cfp_get(slot, &table))
        return 0;
    Byte* entries = NULL;
    uint16_t encoded_count = 0;
    if (table) {
        entries = cfp_handle_data(table, &extent, !query);
        if (!entries || extent < count_size)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        memcpy(&encoded_count, entries, count_size);
    }
    const unsigned count = CW(encoded_count);
    if (count > INT16_MAX || (table && count > (extent - count_size) / process_ref_size))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    unsigned index = 0;
    for (; index < count; ++index) {
        const uint32_t args[] = {US_TO_SYN68K(entries + count_size + index * process_ref_size), process_address};
        uint32_t same;
        if (!cfp_private(&reference, 0x3be20, args, NELEM(args), &same))
            return 0;
        if ((uint8_t)same)
            break;
    }
    if (query)
        return appearance_client_result(call, index != count);
    if (add && index != count)
        return appearance_client_result(call, already_registered);
    if (!add && index == count)
        return appearance_client_result(call, not_registered);

    if (add) {
        if (count == INT16_MAX)
            return appearance_client_result(call, memFullErr);
        const Size size = count_size + (count + 1) * process_ref_size;
        Handle handle = table ? (Handle)SYN68K_TO_US(table) : NewHandleSys(size);
        if (table && extent < (uint32_t)size)
            SetHandleSize(handle, size);
        const OSErr status = MemError();
        if (!handle || (extent < (uint32_t)size && status))
            return appearance_client_result(call, status ? status : memFullErr);
        entries = (Byte*)STARH(handle);
        memcpy(entries + count_size + count * process_ref_size, process, sizeof process);
        encoded_count = CW(count + 1);
        memcpy(entries, &encoded_count, count_size);
        if (!table && !cfp_write(slot, US_TO_SYN68K(handle))) {
            DisposHandle(handle);
            return 0;
        }
    }
    else {
        Byte* record = entries + count_size + index * process_ref_size;
        memmove(record, record + process_ref_size, (count - index - 1) * process_ref_size);
        encoded_count = CW(count - 1);
        memcpy(entries, &encoded_count, count_size);
        SetHandleSize((Handle)SYN68K_TO_US(table), count_size + (count - 1) * process_ref_size);
    }
    return appearance_client_result(call, noErr);
}

static int carbon_RegisterAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("RegisterAppearanceClient", 0x6d08, appearance_client_register, call);
}

static int carbon_UnregisterAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("UnregisterAppearanceClient", 0x6e1c, appearance_client_unregister, call);
}

static int carbon_IsAppearanceClient(ROMlib_carbon_ppc_frame* call)
{
    return appearance_client("IsAppearanceClient", 0x6f34, appearance_client_query, call);
}

static int appearance_get_preference(const char* name, uint32_t offset, int32_t field, unsigned width,
                                     ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return appearance_client_result(call, width == 1 ? 0 : appearance_unimplemented);
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const Byte* stored = ROMlib_carbon_guest_memory(US_TO_SYN68K(reference.toc) + field, width, 0);
    if (!stored)
        return 0;
    if (width == 1)
        return appearance_client_result(call, *stored);
    void* output = call->words[0] ? ROMlib_carbon_try_guest_memory(call->words[0], width, 1) : NULL;
    if (!output)
        return appearance_client_result(call, paramErr);
    memcpy(output, stored, width);
    return appearance_client_result(call, noErr);
}

static int appearance_set_preference(const char* name, uint32_t offset, int32_t field, uint32_t tag,
                                     uint32_t style_limit, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &reference))
        return appearance_client_result(call, appearance_unimplemented);
    if (!appearance_reference_layout(&reference, offset))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const unsigned width = style_limit ? sizeof(uint16_t) : sizeof(uint32_t);
    const uint32_t address = US_TO_SYN68K(reference.toc) + field;
    Byte* stored = ROMlib_carbon_guest_memory(address, width, 1);
    if (!stored)
        return 0;
    if (style_limit) {
        const uint16_t style = call->words[0];
        if (style > style_limit)
            return appearance_client_result(call, paramErr);
        const uint16_t encoded = CW(style);
        memcpy(stored, &encoded, width);
    }
    else {
        const uint32_t encoded = CL(call->words[0]);
        memcpy(stored, &encoded, width);
    }
    if (tag) {
        const uint32_t arguments[] = {1, tag, 0, width, address};
        if (!cfp_private(&reference, 0x5eb4, arguments, NELEM(arguments), NULL))
            return 0;
    }
    if (style_limit && (uint8_t)call->words[1]) {
        const uint32_t arguments[] = {0, cfp_read(0x9ee)};
        if (!cfp_private(&reference, 0x3c1b0, arguments, NELEM(arguments), NULL))
            return 0;
    }
    return appearance_client_result(call, noErr);
}

enum {
    sound_mask_field = 0x4464,
    sound_playlist_field = 0x449c,
    sound_file_field = 0x459c,
    sounds_enabled_field = 0x45a2,
    sound_ready_field = 0x45a3,
};

static int appearance_publish_sound_setting(const ROMlib_carbon_reference* ref, uint32_t tag, uint32_t length,
                                            uint32_t data)
{
    const uint32_t arguments[] = {1, tag, 0, length, data};
    return cfp_private(ref, 0x5eb4, arguments, NELEM(arguments), NULL);
}

static int appearance_get_playlist(const ROMlib_carbon_reference* ref, uint32_t target, uint32_t* result)
{
    const uint32_t toc = US_TO_SYN68K(ref->toc);
    const Byte* stored = ROMlib_carbon_guest_memory(toc + sound_playlist_field, sizeof(Str255), 0);
    Byte* output = target && stored ? ROMlib_carbon_try_guest_memory(target, stored[0] + 1, 1) : NULL;
    if (!stored)
        return 0;
    if (!output) {
        *result = paramErr;
        return 1;
    }
    memmove(output, stored, stored[0] + 1);
    if (!output[0]) {
        *result = -30569; /* No sound set selected. */
        return 1;
    }

    _Alignas(128) struct {
        int16_t volume;
        uint32_t directory;
        FSSpec spec;
        FInfo info;
    } file;

    const uint32_t folder_args[] = {(uint32_t)-32768, TICK("snds"), 1, US_TO_SYN68K(&file.volume),
                                    US_TO_SYN68K(&file.directory)};
    if (!cfp_private(ref, 0x3bf58, folder_args, NELEM(folder_args), result))
        return 0;
    if (!(int16_t)*result) {
        const uint32_t spec_args[] = {CW(file.volume), CL(file.directory), target, US_TO_SYN68K(&file.spec)};
        if (!cfp_private(ref, 0x3c1e0, spec_args, NELEM(spec_args), result))
            return 0;
        if (!(int16_t)*result) {
            const uint32_t info_args[] = {US_TO_SYN68K(&file.spec), US_TO_SYN68K(&file.info)};
            if (!cfp_private(ref, 0x3bf88, info_args, NELEM(info_args), result))
                return 0;
            if (!(int16_t)*result && CL(file.info.fdType) != TICK("tsnd"))
                *result = -30570; /* The selected file is of the wrong type. */
        }
    }
    *result = (int16_t)*result;
    return 1;
}

static int carbon_GetThemePlaylist(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "GetThemePlaylist", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x16ef4))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    return appearance_get_playlist(&ref, call->words[0], &call->words[0]);
}

static int carbon_SetThemePlaylist(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "SetThemePlaylist", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x16e58))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t input = call->words[0];
    const Byte* first = input ? ROMlib_carbon_try_guest_memory(input, 1, 0) : NULL;
    const uint32_t length = first ? *first + 1 : 0;
    const Byte* source = length ? ROMlib_carbon_try_guest_memory(input, length, 0) : NULL;
    if (!source)
        return appearance_client_result(call, paramErr);

    const uint32_t toc = US_TO_SYN68K(ref.toc);
    uint32_t result;
    const Byte* enabled = ROMlib_carbon_guest_memory(toc + sounds_enabled_field, 1, 0);
    uint32_t mask;
    if (!enabled || !cfp_private(&ref, 0x16bd0, &input, 1, &result) || !cfp_get(toc + sound_mask_field, &mask))
        return 0;
    if ((!*enabled || !mask) && !cfp_private(&ref, 0x169f8, NULL, 0, NULL))
        return 0;
    if (!result && (!appearance_publish_sound_setting(&ref, TICK("tsnd"), length, input) ||
                    !cfp_move(toc + sound_playlist_field, input, length)))
        return 0;
    return appearance_client_result(call, result);
}

static int appearance_update_sound(const ROMlib_carbon_reference* ref, uint32_t toc, bool start)
{
    if (!start)
        return cfp_private(ref, 0x1639c, NULL, 0, NULL) && cfp_private(ref, 0x169f8, NULL, 0, NULL) &&
               cfp_private(ref, 0x165e0, NULL, 0, NULL);
    const Byte* ready = ROMlib_carbon_guest_memory(toc + sound_ready_field, 1, 0);
    if (!ready || (!*ready && !cfp_private(ref, 0x16564, NULL, 0, NULL)))
        return 0;
    _Alignas(256) Str255 playlist;
    const uint32_t address = US_TO_SYN68K(playlist);
    uint32_t ignored;
    if (!appearance_get_playlist(ref, address, &ignored))
        return 0;
    const int16_t* file = ROMlib_carbon_guest_memory(toc + sound_file_field, sizeof *file, 0);
    return file && (CW(*file) != -1 || !playlist[0] || cfp_private(ref, 0x16bd0, &address, 1, NULL));
}

static int appearance_set_sound(const char* name, uint32_t offset, bool toggle, ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, offset))
        return ROMlib_carbon_invoke_frame(ref.function, call);
    const uint32_t toc = US_TO_SYN68K(ref.toc);
    Byte* enabled = ROMlib_carbon_guest_memory(toc + sounds_enabled_field, 1, toggle);
    if (!enabled)
        return 0;

    const uint32_t setting = call->words[0];
    if (toggle) {
        *enabled = setting;
        if (!appearance_update_sound(&ref, toc, *enabled != 0) ||
            !appearance_publish_sound_setting(&ref, TICK("snd?"), 1, toc + sounds_enabled_field))
            return 0;
    }
    else {
        const int16_t* file = ROMlib_carbon_guest_memory(toc + sound_file_field, sizeof *file, 0);
        if (!file || ((setting != 0) == (CW(*file) == -1) && !appearance_update_sound(&ref, toc, setting && *enabled)))
            return 0;
        if (!cfp_write(toc + sound_mask_field, setting) ||
            !appearance_publish_sound_setting(&ref, TICK("smsk"), 4, toc + sound_mask_field))
            return 0;
    }
    return appearance_client_result(call, noErr);
}

static int carbon_SetThemeSoundsEnabled(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_sound("SetThemeSoundsEnabled", 0x1682c, true, call);
}

static int carbon_SetThemeSoundMask(ROMlib_carbon_ppc_frame* call)
{
    return appearance_set_sound("SetThemeSoundMask", 0x166f8, false, call);
}

static int carbon_IsValidAppearanceFileType(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "IsValidAppearanceFileType", &ref))
        return 0;
    if (!appearance_reference_layout(&ref, 0x38bc))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    /* The active Appearance engine registers FourCCs in a linked list. */
    uint32_t address = cfp_toc(&ref, 0x45ac);
    for (unsigned i = 0; address && i < 4096; ++i) {
        const uint32_t* record = ROMlib_carbon_try_guest_memory(address, 12, 0);
        if (!record)
            break;
        if (CL(record[0]) == call->words[0])
            return appearance_client_result(call, record[1] != 0);
        address = CL(record[2]);
    }
    return appearance_client_result(call, 0);
}

enum appearance_variant_lookup { appearance_variant_count, appearance_variant_index, appearance_variant_name };

enum { variant_table_header_size = 6, variant_fields_size = 8, variant_name_size = 256 };

static int appearance_read_variant(const ROMlib_carbon_reference* ref, enum appearance_variant_lookup lookup,
                                   ROMlib_carbon_ppc_frame* call)
{
    enum { variant_not_found = -30572 };

    const bool by_name = lookup == appearance_variant_name;
    const uint32_t address = call->words[0];
    const uint32_t target = call->words[lookup == appearance_variant_count ? 1 : 2];
    const size_t length = lookup == appearance_variant_count ? 2 : variant_name_size + variant_fields_size;
    Byte* output = target ? ROMlib_carbon_try_guest_memory(target, length, 1) : NULL;
    uint32_t extent;
    const Byte* data = cfp_handle_data(address, &extent, 0);
    if (!output || !data || extent < variant_table_header_size)
        return appearance_client_result(call, paramErr);
    if (lookup == appearance_variant_count) {
        memcpy(output, data + 4, 2);
        return appearance_client_result(call, noErr);
    }

    const int16_t count = (int16_t)((data[4] << 8) | data[5]);
    const int16_t index = call->words[1];
    if (!by_name && (index < 1 || index > count))
        return appearance_client_result(call, variant_not_found);
    if (by_name) {
        const uint32_t name = call->words[1];
        const Byte* first = name ? ROMlib_carbon_try_guest_memory(name, 1, 0) : NULL;
        if (!first || !ROMlib_carbon_try_guest_memory(name, *first + 1, 0))
            return appearance_client_result(call, paramErr);
    }

    Handle handle = (Handle)MR(address);
    const SignedByte state = by_name ? HGetState(handle) : 0;
    if (by_name)
        HLock(handle);
    uint32_t offset = variant_table_header_size;
    OSErr result = variant_not_found;
    int ok = 1;
    for (int i = 1; i <= count; ++i) {
        if (offset >= extent || data[offset] > 127 ||
            (uint32_t)data[offset] + 1 + variant_fields_size > extent - offset) {
            result = paramErr;
            break;
        }
        const size_t name_size = data[offset] + 1;
        uint32_t found = i == index;
        if (by_name) {
            const uint32_t args[] = {US_TO_SYN68K(data + offset), call->words[1], 0, 1};
            ok = cfp_private(ref, 0x3c318, args, NELEM(args), &found);
            if (!ok)
                break;
        }
        if ((uint8_t)found) {
            memmove(output + variant_name_size, data + offset + name_size, variant_fields_size);
            memmove(output, data + offset, name_size);
            result = noErr;
            break;
        }
        offset += name_size + variant_fields_size;
    }
    if (by_name)
        HSetState(handle, state);
    return ok && appearance_client_result(call, result);
}

#define APPEARANCE_VARIANT(name, offset, lookup)                         \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)              \
    {                                                                    \
        ROMlib_carbon_reference ref;                                     \
        if (!ROMlib_carbon_load_reference("AppearanceLib", #name, &ref)) \
            return 0;                                                    \
        if (!appearance_reference_layout(&ref, offset))                  \
            return ROMlib_carbon_invoke_frame(ref.function, call);       \
        return appearance_read_variant(&ref, lookup, call);              \
    }

APPEARANCE_VARIANT(CountThemeVariants, 0x9ae4, appearance_variant_count)
APPEARANCE_VARIANT(GetIndThemeVariant, 0x9b30, appearance_variant_index)
APPEARANCE_VARIANT(GetThemeVariantByName, 0x9c54, appearance_variant_name)

#undef APPEARANCE_VARIANT

static int appearance_visit_variants(ROMlib_carbon_ppc_frame* call, bool iterate)
{
    const char* name = iterate ? "IterateThemeVariants" : "GetThemeVariantList";
    ROMlib_carbon_reference ref;
    if (!ROMlib_carbon_load_reference("AppearanceLib", name, &ref))
        return 0;
    if (!appearance_reference_layout(&ref, iterate ? 0x9988 : 0x987c))
        return ROMlib_carbon_invoke_frame(ref.function, call);

    const uint32_t target = call->words[1];
    uint32_t* output = !iterate && target ? ROMlib_carbon_try_guest_memory(target, sizeof *output, 1) : NULL;
    if (!target || (!iterate && !output))
        return appearance_client_result(call, paramErr);

    _Alignas(8) FSSpec file;
    const uint32_t file_address = US_TO_SYN68K(&file);
    const uint32_t locate[] = {call->words[0], file_address};
    uint32_t result;
    if (!cfp_private(&ref, 0x7c68, locate, NELEM(locate), &result))
        return 0;
    if ((int16_t)result)
        return appearance_client_result(call, (int16_t)result);

    const uint32_t open[] = {file_address, fsRdPerm};
    uint32_t resource_file;
    if (!cfp_private(&ref, 0x3c330, NULL, 0, NULL) || !cfp_private(&ref, 0x3c348, open, NELEM(open), &resource_file))
        return 0;
    resource_file = (int16_t)resource_file;
    if ((int32_t)resource_file == -1)
        return cfp_private(&ref, 0x3c2e8, NULL, 0, &result) && appearance_client_result(call, (int16_t)result);

    const uint32_t find[] = {TICK("tvar"), 128};
    uint32_t handle;
    OSErr status = resNotFound;
    int ok = cfp_private(&ref, 0x3c360, find, NELEM(find), &handle);
    if (ok && handle && !iterate) {
        ok = cfp_private(&ref, 0x3c4f8, &handle, 1, NULL);
        if (ok) {
            const uint32_t encoded = CL(handle);
            memcpy(output, &encoded, sizeof encoded);
            status = noErr;
        }
    }
    else if (ok && handle) {
        uint32_t extent;
        const Byte* data = cfp_handle_data(handle, &extent, 0);
        if (data && extent >= variant_table_header_size) {
            const int16_t count = (int16_t)((data[4] << 8) | data[5]);
            _Alignas(8) Byte variant[variant_name_size + variant_fields_size] = {0};
            const uint32_t address = US_TO_SYN68K(variant);
            status = noErr;
            for (int index = 1; index <= count; ++index) {
                ROMlib_carbon_ppc_frame lookup = {.words = {handle, index, address}};
                ok = appearance_read_variant(&ref, appearance_variant_index, &lookup);
                status = (OSErr)lookup.words[0];
                if (!ok || status)
                    break;
                const uint32_t args[] = {target, 0xf90, index, address, call->words[2]};
                ok = cfp_private(&ref, 0x3c780, args, NELEM(args), &result);
                if (!ok || !result)
                    break;
            }
        }
    }
    const int closed = cfp_private(&ref, 0x3c3a8, &resource_file, 1, NULL);
    return ok && closed && appearance_client_result(call, status);
}

static int carbon_GetThemeVariantList(ROMlib_carbon_ppc_frame* call)
{
    return appearance_visit_variants(call, false);
}

static int carbon_IterateThemeVariants(ROMlib_carbon_ppc_frame* call)
{
    return appearance_visit_variants(call, true);
}

#define APPEARANCE_GET(name, offset, field, width)                           \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                  \
    {                                                                        \
        return appearance_get_preference(#name, offset, field, width, call); \
    }

#define APPEARANCE_SET(name, offset, field, tag, style_limit)                           \
    static int carbon_##name(ROMlib_carbon_ppc_frame* call)                             \
    {                                                                                   \
        return appearance_set_preference(#name, offset, field, tag, style_limit, call); \
    }

APPEARANCE_GET(GetThemeScrollBarThumbStyle, 0x9734, -0x75ae, 2)
APPEARANCE_GET(GetThemeScrollBarArrowStyle, 0x97e0, -0x75b0, 2)
APPEARANCE_GET(GetThemeCheckBoxStyle, 0x9858, -0x75b2, 2)
APPEARANCE_GET(GetThemeSoundMask, 0x167f4, 0x4464, 4)
APPEARANCE_GET(AreThemeSoundsEnabled, 0x168ec, 0x45a2, 1)
APPEARANCE_GET(GetThemeSoundPanningSpread, 0x169e0, 0x4468, 4)

APPEARANCE_SET(SetThemeScrollBarThumbStyle, 0x96b4, -0x75ae, TICK("tsty"), 1)
APPEARANCE_SET(SetThemeScrollBarArrowStyle, 0x9758, -0x75b0, TICK("asty"), 2)
APPEARANCE_SET(SetThemeCheckBoxStyle, 0x9804, -0x75b2, 0, 1)
APPEARANCE_SET(SetThemeSoundPanningSpread, 0x16990, 0x4468, TICK("panp"), 0)

#undef APPEARANCE_GET
#undef APPEARANCE_SET

#endif
