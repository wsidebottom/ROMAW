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
