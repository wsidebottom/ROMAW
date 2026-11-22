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
