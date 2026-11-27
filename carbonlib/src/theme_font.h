/*+===================================================================
  File:      theme_font.h

  Summary:   Resolve Appearance theme fonts through the Script and Font Managers.

  Classes:   None.

  Functions: Private Appearance font adapters for the Carbon export registry.

  Origin:    CarbonLib 1.4 GetThemeFont and UseThemeFont, System 9.2.1
             AppearanceLib, and Universal Interfaces 3.4.2 Appearance.h.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef ROMAW_CARBON_THEME_FONT_H
#define ROMAW_CARBON_THEME_FONT_H

#include "ScriptMgr.h"
#include "appearance_state.h"
#include "rsys/cquick.h"
#include "toolbox_vectors.h"

/* Most later Carbon role IDs resolve to an older Appearance role. */
static int theme_font_role(uint16_t role)
{
    switch (role) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 103:
    case 200:
        return role;
    case 4:
    case 6:
    case 100:
    case 101:
    case 102:
    case 104:
    case 105:
    case 107:
    case 1001:
    case 1002:
        return 0;
    case 106:
    case 1003:
        return 1;
    case 1000:
        return 3;
    default:
        return -1;
    }
}

static int theme_font_result(ROMlib_carbon_ppc_frame* call, int32_t status)
{
    call->words[0] = status;
    return 1;
}

static int carbon_GetSystemFont(ROMlib_carbon_ppc_frame* call)
{
    if (!call->words[1] || !ROMlib_carbon_try_guest_memory(call->words[1], sizeof(Str255), 1))
        return theme_font_result(call, paramErr);
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[1] = smScriptSysFond;
    if (!carbon_vector_GetScriptVariable(&arguments))
        return 0;
    arguments.words[1] = call->words[1];
    return carbon_vector_GetFontName(&arguments) && theme_font_result(call, noErr);
}

static int carbon_GetThemeFont(ROMlib_carbon_ppc_frame* call)
{
    const int role = theme_font_role((uint16_t)call->words[0]);
    if (role < 0)
        return theme_font_result(call, paramErr);
    const uint32_t name_address = call->words[2], size_address = call->words[3], face_address = call->words[4];
    void* size_output = size_address ? ROMlib_carbon_try_guest_memory(size_address, 2, 1) : NULL;
    Byte* face_output = face_address ? ROMlib_carbon_try_guest_memory(face_address, 1, 1) : NULL;
    if ((size_address && !size_output) || (face_address && !face_output))
        return theme_font_result(call, paramErr);

    ROMlib_carbon_ppc_frame arguments = *call;
    _Alignas(256) Str255 name = {0};
    int16_t family = 0, size = 0;
    Byte face = 0;
    if (role == 200) {
        GrafPtr port = thePort;
        if (!port)
            return theme_font_result(call, paramErr);
        family = PORT_TX_FONT(port);
        size = PORT_TX_SIZE(port);
        face = PORT_TX_FACE(port);
    }
    else {
        static const uint16_t selectors[] = {smScriptSysFondSize, smScriptSmallFondSize, smScriptSmallFondSize,
                                             smScriptAppFondSize};
        const int old_role = role == 103 ? 0 : role;
        ROMlib_carbon_reference saved;
        if (old_role < 4 && ROMlib_carbon_probe_reference("AppearanceLib", "GetThemeFont", &saved)) {
            arguments.words[0] = old_role;
            if (role == 103)
                arguments.words[2] = 0;
            if (!ROMlib_carbon_invoke_frame(saved.function, &arguments))
                return 0;
            if (arguments.words[0] || role != 103)
                return theme_font_result(call, (int32_t)arguments.words[0]);
            size_output = NULL;
            face_output = NULL;
        }
        else {
            int16_t script = (int16_t)call->words[1];
            if (script == -1) {
                arguments.words[0] = smSysScript;
                if (!carbon_vector_GetScriptManagerVariable(&arguments))
                    return 0;
                script = (int16_t)arguments.words[0];
            }
            arguments.words[0] = script;
            arguments.words[1] = old_role == 5 ? smScriptAppFondSize : selectors[old_role];
            if (!carbon_vector_GetScriptVariable(&arguments))
                return 0;
            family = arguments.words[0] >> 16;
            size = arguments.words[0] & (old_role == 5 ? 0xffff : 0x0fff);
            face = old_role == 2;
        }
    }
    if (name_address) {
        if (role == 103)
            memcpy(name, "\011.Keyboard", 10);
        else {
            arguments.words[0] = family;
            arguments.words[1] = US_TO_SYN68K(name);
            if (!carbon_vector_GetFontName(&arguments))
                return 0;
        }
        void* output = ROMlib_carbon_try_guest_memory(name_address, name[0] + 1, 1);
        if (!output)
            return theme_font_result(call, paramErr);
        memcpy(output, name, name[0] + 1);
    }
    if (size_output) {
        const int16_t encoded_size = CW(size);
        memcpy(size_output, &encoded_size, 2);
    }
    if (face_output)
        *face_output = face;
    return theme_font_result(call, noErr);
}

static int theme_collection_font(ROMlib_carbon_ppc_frame* call, const char* name, uint32_t entry, uint16_t role,
                                 uint32_t slot, int with_size)
{
    enum { font_lookup_fallback = -30566 };

    const uint32_t name_address = call->words[1];
    const uint32_t size_address = with_size ? call->words[2] : 0;
    if (!name_address || (with_size && !size_address))
        return theme_font_result(call, paramErr);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_probe_reference("AppearanceLib", name, &reference)) {
        ROMlib_carbon_ppc_frame arguments = *call;
        arguments.words[0] = role;
        arguments.words[1] = call->words[0];
        arguments.words[2] = name_address;
        arguments.words[3] = size_address;
        arguments.words[4] = 0;
        return carbon_GetThemeFont(&arguments) && theme_font_result(call, arguments.words[0]);
    }
    if (!appearance_reference_layout(&reference, entry))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    int32_t script = (int16_t)call->words[0];
    if (script == -1) {
        ROMlib_carbon_ppc_frame arguments = *call;
        arguments.words[0] = smSysScript;
        if (!carbon_vector_GetScriptManagerVariable(&arguments))
            return 0;
        script = (int16_t)arguments.words[0];
    }
    uint32_t collection;
    if (!cfp_get(US_TO_SYN68K(reference.toc) - slot, &collection))
        return 0;
    if (!collection)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    _Alignas(512) struct {
        uint16_t size, face;
        Str255 name;
    } font = {0};

    _Alignas(8) uint32_t length = CL(sizeof font);
    uint32_t lookup[] = {collection, TICK("font"), script, US_TO_SYN68K(&length), US_TO_SYN68K(&font)};
    uint32_t result;
    if (!cfp_private(&reference, 0x3c408, lookup, NELEM(lookup), &result))
        return 0;
    int16_t status = (int16_t)result;
    if (status || !font.name[0]) {
        memset(&font, 0, sizeof font);
        length = CL(sizeof font);
        lookup[2] = 0;
        if (!cfp_private(&reference, 0x3c408, lookup, NELEM(lookup), &result))
            return 0;
        if ((int16_t)result)
            return theme_font_result(call, (int16_t)result);
        status = font_lookup_fallback;
    }
    const size_t name_length = font.name[0] + 1;
    if (CL(length) < 4 + name_length)
        return theme_font_result(call, paramErr);
    void* name_output = ROMlib_carbon_try_guest_memory(name_address, name_length, 1);
    void* size_output = with_size ? ROMlib_carbon_try_guest_memory(size_address, sizeof font.size, 1) : NULL;
    if (!name_output || (with_size && !size_output))
        return theme_font_result(call, paramErr);
    memcpy(name_output, font.name, name_length);
    if (with_size)
        memcpy(size_output, &font.size, sizeof font.size);
    return theme_font_result(call, status);
}

static int carbon_GetSmallSystemFont(ROMlib_carbon_ppc_frame* call)
{
    return theme_collection_font(call, "GetSmallSystemFont", 0x7c4, 1, 0x75d4, 0);
}

static int carbon_GetViewsFont(ROMlib_carbon_ppc_frame* call)
{
    return theme_collection_font(call, "GetViewsFont", 0x99c, 3, 0x75dc, 1);
}

typedef struct {
    uint16_t size, face;
    Str255 name;
} theme_font_record;

_Static_assert(sizeof(theme_font_record) == 0x104, "Appearance font collection record");

static int theme_font_family(uint32_t name, int16_t* family)
{
    ROMlib_carbon_ppc_frame arguments = {.words = {name, US_TO_SYN68K(family)}};
    return carbon_vector_GetFNum(&arguments);
}

// The font list resources have a signed count followed by size, face and Str127 for each entry.
static int theme_find_font(uint32_t type, int16_t script, const uint8_t* name, unsigned index, theme_font_record* font)
{
    ROMlib_carbon_ppc_frame arguments = {.words = {type, (uint32_t)script}};
    if (!carbon_vector_GetResource(&arguments))
        return -1;
    const uint32_t resource = arguments.words[0];
    if (!resource)
        return 0;
    if (!carbon_vector_GetHandleSize(&arguments))
        return -1;
    const int32_t size = (int32_t)arguments.words[0];
    uint32_t address;
    if (size < 2 || !cfp_get(resource, &address) || !address)
        return 0;
    const uint8_t* bytes = ROMlib_carbon_try_guest_memory(address, size, 0);
    if (!bytes)
        return 0;

    const int16_t count = (uint16_t)bytes[0] << 8 | bytes[1];
    size_t remaining = size - 2;
    bytes += 2;
    for (int i = 1; i <= count && remaining >= 5; ++i) {
        const size_t length = bytes[4];
        const size_t record_size = 5 + length;
        if (length > INT8_MAX || record_size > remaining)
            break;
        if (name ? length == name[0] && !memcmp(bytes + 5, name + 1, length) : (unsigned)i == index) {
            memcpy(font, bytes, record_size);
            return i;
        }
        bytes += record_size;
        remaining -= record_size;
    }
    return 0;
}

static int theme_write_system_font(const ROMlib_carbon_reference* reference, int16_t script, int16_t system_script,
                                   int16_t family, int16_t size)
{
    ROMlib_carbon_ppc_frame arguments = {.words = {(uint32_t)script, smScriptSysFond, (uint32_t)family}};
    if (!carbon_vector_SetScriptVariable(&arguments))
        return 0;
    arguments = (ROMlib_carbon_ppc_frame){
        .words = {(uint32_t)script, smScriptSysFondSize, (uint32_t)(uint16_t)family << 16 | (uint16_t)size}};
    if (!carbon_vector_SetScriptVariable(&arguments))
        return 0;
    arguments = (ROMlib_carbon_ppc_frame){.words = {TICK("itlb"), (uint32_t)script}};
    if (!carbon_vector_GetResource(&arguments))
        return 0;
    const uint32_t handle = arguments.words[0];
    if (handle) {
        if (!carbon_vector_GetHandleSize(&arguments))
            return 0;
        uint32_t address;
        if ((int32_t)arguments.words[0] >= 0x28 && cfp_get(handle, &address) && address &&
            address <= UINT32_MAX - 0x28) {
            const uint16_t fields[] = {CW(family), CW(size)};
            void* output = ROMlib_carbon_try_guest_memory(address + 0x24, sizeof fields, 1);
            if (output && memcmp(output, fields, sizeof fields)) {
                memcpy(output, fields, sizeof fields);
                arguments = (ROMlib_carbon_ppc_frame){.words = {handle}};
                if (!carbon_vector_ChangedResource(&arguments))
                    return 0;
                arguments = (ROMlib_carbon_ppc_frame){.words = {handle}};
                if (!carbon_vector_WriteResource(&arguments))
                    return 0;
            }
        }
    }
    if (script != system_script)
        return 1;
    const uint32_t update[] = {(uint32_t)family, (uint32_t)size};
    return cfp_private(reference, 0x1020, update, NELEM(update), NULL) &&
           cfp_private(reference, 0x3c690, NULL, 0, NULL) && cfp_private(reference, 0x3c078, NULL, 0, NULL) &&
           cfp_private(reference, 0x3c0a8, NULL, 0, NULL);
}

static int theme_set_system_font(ROMlib_carbon_ppc_frame* call, int small)
{
    enum { font_not_found = -30574 };

    const uint32_t name_address = call->words[0];
    const uint8_t* name = name_address ? ROMlib_carbon_try_guest_memory(name_address, 1, 0) : NULL;
    if (!name || !ROMlib_carbon_try_guest_memory(name_address, (size_t)*name + 1, 0))
        return theme_font_result(call, font_not_found);

    _Alignas(256) Str255 requested = {0};
    memcpy(requested, name, (size_t)*name + 1);
    const char* entry = small ? "SetSmallSystemFont" : "SetSystemFont";
    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", entry, &reference))
        return 0;
    if (!appearance_reference_layout(&reference, small ? 0x5d0 : 0x458) || (uint8_t)call->words[2])
        return ROMlib_carbon_invoke_frame(reference.function, call);

    _Alignas(4) int16_t family = 0;
    if (!theme_font_family(US_TO_SYN68K(requested), &family))
        return 0;
    if (!family)
        return theme_font_result(call, font_not_found);

    ROMlib_carbon_ppc_frame arguments = {.words = {smSysScript}};
    if (!carbon_vector_GetScriptManagerVariable(&arguments))
        return 0;
    const int16_t system_script = (int16_t)arguments.words[0];
    const int16_t script = (int16_t)call->words[1] == -1 ? system_script : (int16_t)call->words[1];
    _Alignas(512) theme_font_record font = {0};
    const int index = theme_find_font(small ? TICK("sfs#") : TICK("sfn#"), script, requested, 0, &font);
    if (index < 0)
        return 0;
    if (!index)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32_t subject = small ? TICK("ssfn") : TICK("sysf");
    const uint32_t preference = small ? TICK("smsf") : TICK("sysf");
    uint32_t small_collection = 0, emphasized_collection = 0;
    _Alignas(512) theme_font_record emphasized = {0};
    if (small) {
        const uint32_t toc = US_TO_SYN68K(reference.toc);
        if (!cfp_get(toc - 0x75d4, &small_collection) || !small_collection ||
            !cfp_get(toc - 0x75d8, &emphasized_collection) || !emphasized_collection)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        const int emphasized_index = theme_find_font(TICK("sfe#"), script, NULL, index, &emphasized);
        if (emphasized_index < 0)
            return 0;
        if (!emphasized_index)
            return ROMlib_carbon_invoke_frame(reference.function, call);
        _Alignas(4) int16_t emphasized_family = 0;
        if (!theme_font_family(US_TO_SYN68K(emphasized.name), &emphasized_family))
            return 0;
        if (!emphasized_family)
            return theme_font_result(call, TICK("noEm"));
    }
    else if (!theme_font_family(US_TO_SYN68K(font.name), &family))
        return 0;

    _Alignas(4) uint32_t preferences = 0;
    const uint32_t acquire[] = {1, US_TO_SYN68K(&preferences)};
    uint32_t status;
    if (!cfp_private(&reference, 0x5be4, acquire, NELEM(acquire), &status))
        return 0;
    if (status || !preferences)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32_t add_small[] = {small_collection, TICK("font"), (uint32_t)script, sizeof font, US_TO_SYN68K(&font)};
    const uint32_t add_emphasized[] = {emphasized_collection, TICK("font"), (uint32_t)script, sizeof emphasized,
                                       US_TO_SYN68K(&emphasized)};
    const uint32_t save[] = {CL(preferences), preference, (uint32_t)script, 1 + font.name[0], US_TO_SYN68K(font.name)};
    int invoked;
    if (small)
        invoked = cfp_private(&reference, 0x3c570, add_small, NELEM(add_small), NULL);
    else
        invoked = !family || theme_write_system_font(&reference, script, system_script, CW(family),
                                                     font.size ? CW(font.size) : 12);
    invoked = invoked && cfp_private(&reference, 0x3c2a0, save, NELEM(save), NULL);
    if (invoked && script == system_script) {
        const uint32_t message[] = {subject, 0};
        invoked = cfp_private(&reference, 0x6ff0, message, 1, NULL) &&
                  cfp_private(&reference, 0x3c198, message, NELEM(message), NULL);
    }
    const uint32_t release[] = {CL(preferences)};
    invoked = cfp_private(&reference, 0x3c4b0, release, NELEM(release), NULL) && invoked;
    if (invoked && small) {
        preferences = 0;
        if (!cfp_private(&reference, 0x5be4, acquire, NELEM(acquire), &status))
            return 0;
        const uint32_t release_emphasized[] = {CL(preferences)};
        invoked = cfp_private(&reference, 0x3c570, add_emphasized, NELEM(add_emphasized), NULL);
        if (!status && preferences)
            invoked = cfp_private(&reference, 0x3c4b0, release_emphasized, NELEM(release_emphasized), NULL) && invoked;
    }
    return invoked && theme_font_result(call, noErr);
}

static int carbon_SetSystemFont(ROMlib_carbon_ppc_frame* call)
{
    return theme_set_system_font(call, 0);
}

static int carbon_SetSmallSystemFont(ROMlib_carbon_ppc_frame* call)
{
    return theme_set_system_font(call, 1);
}

static int carbon_SetViewsFont(ROMlib_carbon_ppc_frame* call)
{
    const uint32_t name_address = call->words[0];
    const int16_t size = (int16_t)call->words[1];
    if (!name_address || size < 9)
        return theme_font_result(call, TICK("noEm"));

    const uint8_t* name = ROMlib_carbon_try_guest_memory(name_address, 1, 0);
    if (!name)
        return theme_font_result(call, paramErr);
    const size_t name_size = 1 + *name;
    name = ROMlib_carbon_try_guest_memory(name_address, name_size, 0);
    if (!name)
        return theme_font_result(call, paramErr);

    _Alignas(512) theme_font_record font = {.size = CW(size)};

    memcpy(font.name, name, name_size);

    ROMlib_carbon_reference reference;
    if (!ROMlib_carbon_load_reference("AppearanceLib", "SetViewsFont", &reference))
        return 0;
    if (!appearance_reference_layout(&reference, 0x860) || (uint8_t)call->words[3])
        return ROMlib_carbon_invoke_frame(reference.function, call);

    uint32_t collection;
    if (!cfp_get(US_TO_SYN68K(reference.toc) - 0x75dc, &collection) || !collection)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    ROMlib_carbon_ppc_frame arguments = {.words = {smSysScript}};
    if (!carbon_vector_GetScriptManagerVariable(&arguments))
        return 0;
    const int16_t system_script = (int16_t)arguments.words[0];
    int16_t script = (int16_t)call->words[2];
    if (script == -1)
        script = system_script;

    _Alignas(4) int16_t family = 0;
    arguments = (ROMlib_carbon_ppc_frame){.words = {name_address, US_TO_SYN68K(&family)}};
    if (!carbon_vector_GetFNum(&arguments))
        return 0;
    if (!family)
        return theme_font_result(call, -30574);

    // The saved font collection and preference store must see the same update.
    _Alignas(4) uint32_t preferences = 0;
    const uint32_t acquire[] = {1, US_TO_SYN68K(&preferences)};
    uint32_t status;
    if (!cfp_private(&reference, 0x5be4, acquire, NELEM(acquire), &status))
        return 0;
    if (status || !preferences)
        return ROMlib_carbon_invoke_frame(reference.function, call);

    const uint32_t font_address = US_TO_SYN68K(&font);
    const uint32_t insert[] = {collection, TICK("font"), (uint32_t)script, sizeof font, font_address};
    const uint32_t save_name[] = {CL(preferences), TICK("vfnt"), (uint32_t)script, name_size, US_TO_SYN68K(font.name)};
    const uint32_t save_size[] = {CL(preferences), TICK("vfsz"), (uint32_t)script, sizeof font.size, font_address};
    int invoked = cfp_private(&reference, 0x3c570, insert, NELEM(insert), NULL) &&
                  cfp_private(&reference, 0x3c2a0, save_name, NELEM(save_name), NULL) &&
                  cfp_private(&reference, 0x3c2a0, save_size, NELEM(save_size), NULL);
    if (invoked && script == system_script) {
        const uint32_t message[] = {TICK("vfnt"), 0};
        invoked = cfp_private(&reference, 0x6ff0, message, 1, NULL) &&
                  cfp_private(&reference, 0x3c198, message, NELEM(message), NULL);
    }
    const uint32_t release[] = {CL(preferences)};
    return cfp_private(&reference, 0x3c4b0, release, NELEM(release), NULL) && invoked && theme_font_result(call, noErr);
}

static int theme_antialias_state(const ROMlib_carbon_reference* reference, int16* size, uint32_t* enabled)
{
    if (reference) {
        const uint32_t args[] = {US_TO_SYN68K(size)};
        return cfp_private(reference, 0x3c6f0, args, NELEM(args), enabled);
    }
    *enabled = C_IsAntiAliasedTextEnabled(size);
    return 1;
}

static int carbon_GetThemeFontSmoothing(ROMlib_carbon_ppc_frame* call)
{
    /* Appearance reads the threshold as part of the enabled-state query. */
    if (!call->words[0])
        return theme_font_result(call, call->words[1] ? noErr : paramErr);

    uint8_t* enabled = ROMlib_carbon_try_guest_memory(call->words[0], 1, 1);
    void* output_size = call->words[1] ? ROMlib_carbon_try_guest_memory(call->words[1], sizeof(int16), 1) : NULL;
    if (!enabled || (call->words[1] && !output_size))
        return theme_font_result(call, paramErr);

    ROMlib_carbon_reference reference;
    const bool saved = ROMlib_carbon_load_reference("AppearanceLib", "GetThemeFontSmoothing", &reference);
    if (saved && !appearance_reference_layout(&reference, 0xb50))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    _Alignas(8) int16 size = 0;
    uint32_t state;
    if (!theme_antialias_state(saved ? &reference : NULL, &size, &state))
        return 0;
    *enabled = state;
    if (output_size)
        memcpy(output_size, &size, sizeof size);
    return theme_font_result(call, noErr);
}

static int carbon_SetThemeFontSmoothing(ROMlib_carbon_ppc_frame* call)
{
    ROMlib_carbon_reference reference;
    const bool saved = ROMlib_carbon_load_reference("AppearanceLib", "SetThemeFontSmoothing", &reference);
    if (saved && !appearance_reference_layout(&reference, 0xa5c))
        return ROMlib_carbon_invoke_frame(reference.function, call);

    _Alignas(8) int16 size = CW((uint16_t)call->words[1]);
    if (!size) {
        uint32_t state;
        if (!theme_antialias_state(saved ? &reference : NULL, &size, &state))
            return 0;
    }
    const uint16_t threshold = CW((uint16_t)size);
    if (threshold < 12 || threshold > 24)
        return theme_font_result(call, paramErr);

    _Alignas(8) uint8_t enabled = call->words[0];
    if (saved) {
        const uint32_t font_args[] = {enabled, threshold};
        const uint32_t flag_pref[] = {1, TICK("smoo"), 0, sizeof enabled, US_TO_SYN68K(&enabled)};
        const uint32_t size_pref[] = {1, TICK("smos"), 0, sizeof size, US_TO_SYN68K(&size)};
        const uint32_t message[] = {TICK("anti"), 0};
        if (!cfp_private(&reference, 0x3c708, font_args, NELEM(font_args), NULL) ||
            !cfp_private(&reference, 0x5eb4, flag_pref, NELEM(flag_pref), NULL) ||
            !cfp_private(&reference, 0x5eb4, size_pref, NELEM(size_pref), NULL) ||
            !cfp_private(&reference, 0x3c198, message, NELEM(message), NULL))
            return 0;
        if ((uint8_t)call->words[2]) {
            const uint32_t paint_args[] = {0, cfp_read(0x9ee)};
            if (!cfp_private(&reference, 0x3c1b0, paint_args, NELEM(paint_args), NULL) ||
                !cfp_private(&reference, 0x3c1c8, NULL, 0, NULL))
                return 0;
        }
    }
    else {
        C_SetAntiAliasedTextEnabled(enabled, threshold);
        if ((uint8_t)call->words[2] && GrayRgn) {
            PaintBehind(NULL, MR(GrayRgn));
            DrawMenuBar();
        }
    }
    return theme_font_result(call, noErr);
}

static int carbon_UseThemeFont(ROMlib_carbon_ppc_frame* call)
{
    _Alignas(256) Str255 name;
    _Alignas(16) int16_t size, family;
    _Alignas(16) Byte face;
    ROMlib_carbon_ppc_frame arguments = *call;
    arguments.words[2] = US_TO_SYN68K(name);
    arguments.words[3] = US_TO_SYN68K(&size);
    arguments.words[4] = US_TO_SYN68K(&face);
    if (!carbon_GetThemeFont(&arguments))
        return 0;
    if (arguments.words[0])
        return theme_font_result(call, (int32_t)arguments.words[0]);
    arguments.words[0] = US_TO_SYN68K(name);
    arguments.words[1] = US_TO_SYN68K(&family);
    if (!carbon_vector_GetFNum(&arguments))
        return 0;
    arguments.words[0] = (int16_t)CW(family);
    if (!carbon_vector_TextFont(&arguments))
        return 0;
    arguments.words[0] = (int16_t)CW(size);
    if (!carbon_vector_TextSize(&arguments))
        return 0;
    arguments.words[0] = face;
    return carbon_vector_TextFace(&arguments) && theme_font_result(call, noErr);
}

#endif
