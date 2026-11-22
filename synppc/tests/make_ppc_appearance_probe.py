#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_appearance_probe.py
#
#   Summary:   Original PPC checks for classic theme menu pixels and content callbacks.
#
#   Classes:   None declared.
#
#   Functions: make_appearance_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC checks for classic theme menu pixels and content callbacks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_appearance_probe(destination):
    names = [
        "InitWindows",
        "RegisterAppearanceClient",
        "UnregisterAppearanceClient",
        "NewWindow",
        "SetPort",
        "DisposeWindow",
        "RGBBackColor",
        "EraseRect",
        "PaintRect",
        "GetCPixel",
        "DrawThemeMenuItem",
        "DrawThemeMenuSeparator",
        "GetKeyboardFocus",
        "GetNewControl",
        "DisposeControl",
        "HiliteControl",
        "HideControl",
        "ShowControl",
        "FindControlUnderMouse",
        "GetFontName",
        "FMGetFontFamilyFromName",
        "GetAppFont",
        "SetResLoad",
        "RGBForeColor",
        "TextFont",
        "TextSize",
        "TextMode",
        "MoveTo",
        "DrawText",
        "NewGWorld",
        "GetGWorldPixMap",
        "LockPixels",
        "DisposeGWorld",
        "SetThemeWindowBackground",
        "GetDialogItemAsControl",
        "GetNewDialog",
        "DisposeDialog",
        "SetControlTitle",
        "GetControlTitle",
        "SetControlFontStyle",
        "SetControlData",
        "SetDialogDefaultItem",
        "SetDialogCancelItem",
        "Gestalt",
        "NewMenu",
        "AppendMenu",
        "SetMenuItemModifiers",
        "GetMenuItemModifiers",
        "SetMenuItemKeyGlyph",
        "GetMenuItemKeyGlyph",
        "SetMenuItemRefCon",
        "GetMenuItemRefCon",
        "InsertMenuItem",
        "DeleteMenuItem",
        "DisposeMenu",
        "CreateRootControl",
        "GetRootControl",
        "AutoEmbedControl",
        "GetControlData",
        "TextBox",
        "FindControl",
        "DrawThemePlacard",
        "DrawThemeListBoxFrame",
        "DrawControlInCurrentPort",
        "DrawGrowIcon",
        "GetNextEvent",
        "SetBevelButtonTextPlacement",
        "GetControlFeatures",
        "SetImageWellContentInfo",
        "GetImageWellContentInfo",
        "GetPicture",
        "GetHandleSize",
        "SetBevelButtonTextAlignment",
        "SetBevelButtonContentInfo",
        "GetBevelButtonContentInfo",
        "SetBevelButtonGraphicAlignment",
        "SetBevelButtonMenuValue",
        "GetBevelButtonMenuValue",
        "GetControlValue",
        "SetControlValue",
        "GetSysDirection",
        "SetSysDirection",
        "HandleControlClick",
        "GetMouse",
        "MoveControl",
        "SizeControl",
        "PostEvent",
        "GetSysFont",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0300 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    def pixel(y, value):
        emit(0x3860000A, 0x38800000 | y, 0x38BE01A0)
        call("GetCPixel")
        for offset in [0, 2, 4]:
            emit(0xA87E01A0 + offset)
            check(value)

    def draw(state, kind=0, callback=True):
        emit(
            0x387E0120,
            0x389E0128,
            0x38A00008,
            0x38C0000C,
            0x38E00000 | state,
            0x39000000 | kind,
            0x393E0118 if callback else 0x39200000,
            0x395E0140,
        )
        call("DrawThemeMenuItem")

    def colored_glyph(blue_background=False, font=0, size=12):
        # A bitmap glyph drawn with srcOr into a color port must retain its RGB
        # foreground; black-only text can otherwise look superficially correct.
        emit(0x387E0238 if blue_background else 0x387E0190)
        call("RGBBackColor")
        emit(0x387E0120)
        call("EraseRect")
        emit(0x387E0190)
        call("RGBBackColor")
        emit(0x387E0210)
        call("RGBForeColor")
        emit(0x38600000 | font)
        call("TextFont")
        emit(0x38600000 | size)
        call("TextSize")
        emit(0x38600001)
        call("TextMode")
        emit(0x38600002, 0x38800014)
        call("MoveTo")
        emit(0x387E0220, 0x38800000, 0x38A00001)
        call("DrawText")
        emit(0x38600000, 0x907E0224, 0x907E0228, 0x907E022C)
        glyph_scan = len(code)
        emit(0x807E0224, 0x809E0228, 0x38BE01A0)
        call("GetCPixel")
        emit(0xA87E01A4, 0x2C03FFFF)
        background = len(code)
        emit(0x41820000)
        check(0)
        for offset in (0, 2):
            emit(0xA87E01A0 + offset)
            check(-1)
        emit(0x807E022C, 0x38630001, 0x907E022C)
        next_pixel = len(code)
        emit(0x48000000)
        code[background] |= (len(code) - background) * 4
        for offset in (0, 2):
            emit(0xA87E01A0 + offset)
            check(0 if blue_background else -1)
        code[next_pixel] |= (len(code) - next_pixel) * 4
        emit(0x807E0224, 0x38630001, 0x907E0224, 0x2C030020)
        emit(0x41800000 | (((glyph_scan - len(code)) * 4) & 0xFFFC))
        emit(0x38600000, 0x907E0224, 0x807E0228, 0x38630001, 0x907E0228, 0x2C030020)
        emit(0x41800000 | (((glyph_scan - len(code)) * 4) & 0xFFFC))
        emit(0x807E022C, 0x2C030000)
        failures.append(len(code))
        emit(0x41820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    emit(0x3C607379, 0x60637376, 0x389E01B0)  # Gestalt 'sysv'.
    call("Gestalt")
    check(0)
    emit(0x807E01B0)
    check(0x0810)
    emit(0xA060015A)  # SysVersion low-memory word.
    check(0x0810)
    call("RegisterAppearanceClient")
    check(0)
    for selector, result in [(0x7072, 1), (0x7672, 0x0100)]:
        emit(0x3C606170, 0x60630000 | selector, 0x389E01B0)
        call("Gestalt")
        check(0)
        emit(0x807E01B0)
        check(result)
    call("UnregisterAppearanceClient")
    check(0)
    emit(0x38600080, 0x389E0268)
    call("NewMenu")
    emit(0x907E0260, 0x389E0268)
    call("AppendMenu")
    emit(0x807E0260, 0x38800001, 0x38A00008)
    call("SetMenuItemModifiers")
    check(0)
    emit(0x807E0260, 0x38800001, 0x38A00017)
    call("SetMenuItemKeyGlyph")
    check(0)
    emit(0x807E0260, 0x38800001, 0x3CA01234, 0x60A55678)
    call("SetMenuItemRefCon")
    check(0)
    emit(0x807E0260, 0x389E0268, 0x38A00000)
    call("InsertMenuItem")
    emit(0x807E0260, 0x38800002, 0x38BE01B0)
    call("GetMenuItemModifiers")
    check(0)
    emit(0x887E01B0)
    check(8)
    emit(0x807E0260, 0x38800002, 0x38BE01B0)
    call("GetMenuItemRefCon")
    check(0)
    emit(0x807E01B0, 0x6C631234, 0x68635678)
    check(0)
    emit(0x807E0260, 0x38800001)
    call("DeleteMenuItem")
    emit(0x807E0260, 0x38800001, 0x38BE01B0)
    call("GetMenuItemKeyGlyph")
    check(0)
    emit(0xA07E01B0)
    check(23)
    emit(0x807E0260)
    call("DisposeMenu")
    call("GetAppFont")
    emit(0xA8800984, 0x7C032000)  # Compare the real ApFontID low-memory word.
    failures.append(len(code))
    emit(0x40820000)
    call("GetSysFont")
    emit(0xA8800BA6, 0x7C032000)  # Compare the actual SysFontFam, including signed IDs.
    failures.append(len(code))
    emit(0x40820000)
    emit(0xA0600BA6, 0xB07E02F4)
    for family in (0, 3, -32768, -1):
        emit(0x38600000 | (family & 0xFFFF), 0xB0600BA6)
        call("GetSysFont")
        check(family)
    emit(0xA07E02F4, 0xB0600BA6)
    # Resolve an actual installed classic family through the modern return ABI.
    emit(0x38600003, 0x389E01C0)
    call("GetFontName")
    emit(0x38600000)
    call("SetResLoad")
    emit(0x387E01C0)
    call("FMGetFontFamilyFromName")
    check(3)
    emit(0x88600A5E)  # ResLoad low-memory Boolean.
    check(0)
    emit(0x387E01E0)
    call("FMGetFontFamilyFromName")
    check(-1)
    emit(0x38600000)
    call("FMGetFontFamilyFromName")
    check(-1)
    emit(0x88600A5E)  # ResLoad low-memory Boolean.
    check(0)
    emit(0x38600001)
    call("SetResLoad")
    emit(
        0x38600000,
        0x389E0130,
        0x38BE0138,
        0x38C00001,
        0x38E00000,
        0x3900FFFF,
        0x39200000,
        0x39400000,
    )
    call("NewWindow")
    emit(0x907E0180)
    # Carbon window ports have a real PixMapHandle at CGrafPort offset 2.
    emit(0x80830002, 0x80640000, 0xA0630004, 0x70638000)
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x807E0180)
    emit(0x389E01B0)
    call("GetKeyboardFocus")
    check(0)
    emit(0x807E01B0)
    check(0)
    emit(0x38600081, 0x38800000, 0x38A0FFFF)
    call("GetNewDialog")
    emit(0x907E0260, 0x38800001, 0x38BE01B0)
    call("GetDialogItemAsControl")
    check(0)
    emit(0x807E01B0, 0x907E0264, 0x389E0250)
    call("SetControlFontStyle")
    check(0)
    emit(0x807E0264, 0x389E0268)
    call("SetControlTitle")
    emit(0x807E0264, 0x389E01C0)
    call("GetControlTitle")
    emit(0xA07E01C0)
    check(0x0259)  # Pascal string "Yes".
    emit(0x807E0264, 0x38800000, 0x3CA06466, 0x60A56C74, 0x38C00001, 0x38FE026C)
    call("SetControlData")
    check(0)
    for name in ["SetDialogDefaultItem", "SetDialogCancelItem"]:
        emit(0x807E0260, 0x38800001)
        call(name)
        check(0)
    emit(0x807E0260, 0x38800002, 0x38BE01B0)
    call("GetDialogItemAsControl")
    check(0)  # Appearance clients can update a classic static label as a control.
    emit(0x807E01B0, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x807E0260)
    call("DisposeDialog")
    emit(0x807E0264, 0x389E0250)
    call("SetControlFontStyle")
    check(-50)
    emit(0x807E0180)
    call("SetPort")
    emit(0x807E0180, 0x389E01B0)
    call("GetRootControl")
    check(-30586)
    emit(0x807E0180, 0x389E01B0)
    call("CreateRootControl")
    check(0)
    emit(0x807E01B0, 0x907E0260)
    emit(0x807E0180, 0x389E01B0)
    call("CreateRootControl")
    check(-30587)
    emit(0x807E01B0, 0x809E0260, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x38601234, 0x907E01B0, 0x38600000, 0x389E01B0)
    call("GetKeyboardFocus")
    check(-50)
    emit(0x807E01B0)
    check(0x1234)
    emit(0x807E0180, 0x38800000)
    call("GetKeyboardFocus")
    check(-50)
    emit(0x807E0180)
    call("SetPort")
    emit(0x387E0238)
    call("RGBBackColor")
    emit(0x807E0180, 0x38800001, 0x38A00001)
    call("SetThemeWindowBackground")
    check(0)
    emit(0x387E0120)
    call("EraseRect")
    # The theme dialog brush replaces the previous blue brush with grey.
    emit(0x3860000A, 0x3880000A, 0x38BE01A0)
    call("GetCPixel")
    for offset in [0, 2, 4]:
        emit(0xA87E01A0 + offset)
        check(-8739)
    emit(0x38600000, 0x38800001, 0x38A00000)
    call("SetThemeWindowBackground")
    check(-50)
    emit(0x807E0180, 0x38800001, 0x38BE01B0)
    call("GetDialogItemAsControl")
    check(-50)  # A document window is not a dialog.
    emit(0x807E01B0)
    check(0)
    emit(0x38600080, 0x809E0180)
    call("GetNewControl")
    emit(0x907E01C0, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x807E01C0)
    call("DrawControlInCurrentPort")
    emit(0x807E01C0, 0x809E0180)
    call("AutoEmbedControl")
    check(0)
    # A newer embedded control overlays the older control at the same bounds.
    # Background panes must not replace the foreground hit with their handle.
    emit(0x38600080, 0x809E0180)
    call("GetNewControl")
    emit(0x907E0298, 0x809E0180)
    call("AutoEmbedControl")
    check(0)
    emit(0x3C60000A, 0x6063000A, 0x809E0180, 0x38BE01C4)
    call("FindControlUnderMouse")
    emit(0x809E0298, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x3C60000A, 0x6063000A, 0x809E0180, 0x38BE01B0)
    call("FindControl")
    check(10)
    emit(0x807E01B0, 0x809E0298, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0298)
    call("DisposeControl")
    for disabled in [False, True]:
        if disabled:
            emit(0x807E01C0, 0x388000FF)
            call("HiliteControl")
        emit(0x3C60000A, 0x6063000A, 0x809E0180, 0x38BE01C4)
        call("FindControlUnderMouse")
        emit(0x809E01C0, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        emit(0xA87E01C4)
        check(0 if disabled else 10)
    emit(0x807E01C0)
    call("HideControl")
    emit(0x3C60000A, 0x6063000A, 0x809E0180, 0x38BE01C4)
    call("FindControlUnderMouse")
    check(0)
    emit(0xA87E01C4)
    check(0)
    emit(0x807E01C0)
    call("ShowControl")
    emit(0x3C60000A, 0x6063000A, 0x809E0180, 0x38A00000)
    call("FindControlUnderMouse")
    emit(0x809E01C0, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E01C0)
    call("DisposeControl")
    emit(0x38600082, 0x809E0180)
    call("GetNewControl")
    emit(0x907E0264, 0x38800000, 0x3CA07465, 0x60A57874, 0x38C00002, 0x38FE0269)
    call("SetControlData")
    check(0)
    emit(
        0x807E0264,
        0x38800000,
        0x3CA07465,
        0x60A57874,
        0x38C00002,
        0x38FE01C0,
        0x391E01B0,
    )
    call("GetControlData")
    check(0)
    emit(0x807E01B0)
    check(2)
    emit(0xA07E01C0)
    check(0x5973)
    emit(0x807E0264)
    call("DisposeControl")
    emit(0x38600083, 0x809E0180)
    call("GetNewControl")
    emit(0x907E0264)
    call("ShowControl")
    # The edit interior stays white over the grey theme window background.
    emit(0x38600006, 0x38800006, 0x38BE01A0)
    call("GetCPixel")
    for offset in [0, 2, 4]:
        emit(0xA87E01A0 + offset)
        check(-1)
    emit(0x807E0264)
    call("DisposeControl")
    emit(0x387E0190)
    call("RGBBackColor")
    emit(0x387E0120)
    call("EraseRect")
    draw(1)
    check(0)
    emit(0x807E0140, 0x389E0128, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0144)
    check(8)
    emit(0x807E0150)
    check(1)
    pixel(10, 0)
    pixel(6, -1)
    # Painting outside the temporary menu clip must work after the call.
    emit(0x387E0120)
    call("PaintRect")
    pixel(6, 0)
    draw(0, callback=False)
    check(0)
    pixel(10, -1)
    emit(0x387E0128)
    call("DrawThemeMenuSeparator")
    check(0)
    pixel(10, 0)
    pixel(9, -1)
    emit(0x38600000)
    call("DrawThemeMenuSeparator")
    check(-50)
    draw(0, kind=2)
    check(-4)
    emit(0x807E0150)
    check(1)
    colored_glyph()
    colored_glyph(blue_background=True)
    # Enlarged Times text also exercises the Windows scalable mask path. The
    # scan deliberately clips to the same small region and checks its colors.
    colored_glyph(font=20, size=72)
    colored_glyph(blue_background=True, font=20, size=72)
    # Invalid depths must return cDepthErr without allocating a partial world.
    for depth in (-32768, -1, 3, 15, 31, 33, 32767):
        emit(0x38600000, 0x907E0230)
        emit(
            0x387E0230,
            0x38800000 | (depth & 65535),
            0x38BE0120,
            0x38C00000,
            0x38E00000,
            0x39000000,
        )
        call("NewGWorld")
        check(-157)
        emit(0x807E0230)
        check(0)
    for depth in (8, 16, 24, 32):
        emit(
            0x387E0230,
            0x38800000 | depth,
            0x38BE0120,
            0x38C00000,
            0x38E00000,
            0x39000000,
        )
        call("NewGWorld")
        check(0)
        emit(0x807E0230)
        call("GetGWorldPixMap")
        if depth == 24:
            # 24 color bits use a four-byte RGB pixel, not packed triples.
            emit(0x80630000, 0xA0830004, 0x70843FFF, 0x2C040080)
            failures.append(len(code))
            emit(0x41800000)
            for offset, expected in ((30, 16), (32, 32), (34, 3), (36, 8)):
                emit(0x907E0234, 0xA0630000 | offset)
                check(expected)
                emit(0x807E0234)
            emit(0x807E0230)
            call("GetGWorldPixMap")
        call("LockPixels")
        check(1)
        emit(0x807E0230)
        call("SetPort")
        colored_glyph()
        colored_glyph(blue_background=True)
        colored_glyph(font=20, size=72)
        colored_glyph(blue_background=True, font=20, size=72)
        emit(0x807E0180)
        call("SetPort")
        emit(0x807E0230)
        call("DisposeGWorld")
    # Iconographer expands locked worlds to the next aligned row stride.
    for bounds, depth, stride in [(0x0270, 8, 16), (0x0278, 8, 64), (0x0280, 1, 32)]:
        emit(
            0x387E0230,
            0x38800000 | depth,
            0x38BE0000 | bounds,
            0x38C00000,
            0x38E00000,
            0x39000000,
        )
        call("NewGWorld")
        check(0)
        emit(0x807E0230)
        call("GetGWorldPixMap")
        emit(0x80630000, 0xA0630004, 0x70633FFF)
        check(stride)
        emit(0x807E0230)
        call("DisposeGWorld")
    # A trailing TextEdit line separator must not render a missing-glyph box.
    emit(0x807E0180)
    call("SetPort")
    emit(0x38600003)
    call("TextFont")
    emit(0x38600009)
    call("TextSize")
    emit(0x387E0298)
    call("RGBForeColor")
    for length in [1, 2]:
        emit(0x387E0190)
        call("RGBBackColor")
        emit(0x387E0120)
        call("EraseRect")
        emit(0x387E0290, 0x38800000 | length, 0x38BE0120, 0x38C00000)
        call("TextBox")
        emit(0x38600000, 0x907E0224, 0x907E0228, 0x907E022C)
        scan = len(code)
        emit(0x807E0224, 0x809E0228, 0x38BE01A0)
        call("GetCPixel")
        for offset in [0, 2, 4]:
            emit(0xA01E01A0 + offset, 0x807E022C, 0x7C630214, 0x907E022C)
        emit(0x807E0224, 0x38630001, 0x907E0224, 0x2C030020)
        emit(0x41800000 | (((scan - len(code)) * 4) & 0xFFFC))
        emit(0x38600000, 0x907E0224, 0x807E0228, 0x38630001, 0x907E0228, 0x2C030020)
        emit(0x41800000 | (((scan - len(code)) * 4) & 0xFFFC))
        emit(0x807E022C)
        if length == 1:
            white_sum = 32 * 32 * 3 * 65535
            emit(0x3C800000 | (white_sum >> 16), 0x60840000 | (white_sum & 65535))
            emit(0x7C032000)
            failures.append(len(code))
            emit(0x41820000)
            emit(0x907E0294)
        else:
            emit(0x809E0294, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)
    # Placards supply a grey surface, independent of the caller's white or
    # blue background, and restore that background after drawing.
    emit(0x387E0238)
    call("RGBBackColor")
    emit(0x387E0120, 0x38800001)
    call("DrawThemePlacard")
    check(0)
    for value, erase in [(-8739, False), (0, True)]:
        if erase:
            emit(0x387E0120)
            call("EraseRect")
        emit(0x3860000A, 0x3880000A, 0x38BE01A0)
        call("GetCPixel")
        for offset in [0, 2, 4]:
            emit(0xA87E01A0 + offset)
            check(-1 if erase and offset == 4 else value)
    emit(0x387E0128, 0x38800001)
    call("DrawThemeListBoxFrame")
    check(0)
    emit(0x387E0128, 0x38800002)
    call("DrawThemeListBoxFrame")
    check(-50)
    emit(0x38600084, 0x809E0180)
    call("GetNewControl")
    emit(0x907E02B8)
    # Feature output is a big-endian UInt32 and must not overwrite its guard.
    emit(0x3860FFFF, 0x907E02B0, 0x907E02B4, 0x807E02B8, 0x389E02B0)
    call("GetControlFeatures")
    check(0)
    emit(0x807E02B0)
    check(64)
    emit(0x807E02B4)
    check(-1)
    emit(0x38600000, 0x389E02B0)
    call("GetControlFeatures")
    check(-50)
    emit(0x807E02B8, 0x38800000)
    call("GetControlFeatures")
    check(-50)
    emit(0x807E0260, 0x389E02B0)
    call("GetControlFeatures")
    check(0)
    emit(0x807E02B0)
    check(66)  # Root embedding and data access.
    emit(0x38600085, 0x809E0180)
    call("GetNewControl")
    emit(0x907E02BC, 0x389E02B0)
    call("GetControlFeatures")
    check(0)
    emit(0x807E02B0)
    check(0)  # Tabs do not implement focus/idle messages yet.
    emit(0x807E02BC)
    call("DisposeControl")
    # Classic checkbox definitions must reject the Appearance handshake,
    # rather than asserting on message 27.
    emit(0x38600086, 0x809E0180)
    call("GetNewControl")
    emit(0x907E02BC, 0x3860FFFF, 0x907E02B0, 0x807E02BC, 0x389E02B0)
    call("GetControlFeatures")
    check(-30580)
    emit(0x807E02B0)
    check(0)
    emit(0x807E02BC)
    call("DisposeControl")
    for placement in (-1, 0, 1, 2, 3, 4):
        emit(0x807E02B8, 0x38800000 | (placement & 65535))
        call("SetBevelButtonTextPlacement")
        check(0)
        emit(0x807E02B8, 0x38800000, 0x3CA07470, 0x60A56C63, 0x38C00002, 0x38FE02B2, 0x391E02B4)
        call("GetControlData")
        check(0)
        emit(0xA87E02B2)
        check(placement)
        emit(0x807E02B4)
        check(2)
    emit(0x38600001, 0xB07E02B0)
    emit(0x807E02B8, 0x38800000, 0x3CA07470, 0x60A56C63, 0x38C00002, 0x38FE02B0)
    call("SetControlData")
    check(0)
    emit(0x807E02B8, 0x38800000, 0x3CA07470, 0x60A56C63, 0x38C00001, 0x38FE02B0)
    call("SetControlData")
    check(-50)
    emit(0x807E02B8, 0x38800000, 0x3CA07470, 0x60A56C63, 0x38C00002, 0x38FE02B2, 0x391E02B4)
    call("GetControlData")
    check(0)
    emit(0xA87E02B2)
    check(1)
    emit(0x807E02B8, 0x38800005)
    call("SetBevelButtonTextPlacement")
    check(-50)
    emit(0x38600000, 0x38800000)
    call("SetBevelButtonTextPlacement")
    check(-50)
    emit(0x807E02B8)
    call("DrawControlInCurrentPort")
    emit(0x387E0190)
    call("RGBBackColor")
    emit(0x387E0198)
    call("RGBForeColor")
    emit(0x807E02B8, 0x38800000, 0x3CA07461, 0x60A56C69, 0x38C00002, 0x38FE02B2, 0x391E02B4)
    call("GetControlData")
    check(0)
    emit(0xA87E02B2)
    check(1)  # Existing bevels retain their centered default.
    # Alignment and signed offsets share state with the two-byte data tags.
    for alignment in (-2, -1, 0, 1):
        for offset in (-4, 0, 3):
            emit(0x807E02B8, 0x38800000 | (alignment & 65535), 0x38A00000 | (offset & 65535))
            call("SetBevelButtonTextAlignment")
            check(0)
            for tag, expected in ((0x74616C69, alignment), (0x746F6666, offset)):
                emit(
                    0x807E02B8,
                    0x38800000,
                    0x3CA00000 | (tag >> 16),
                    0x60A50000 | (tag & 65535),
                    0x38C00002,
                    0x38FE02B2,
                    0x391E02B4,
                )
                call("GetControlData")
                check(0)
                emit(0xA87E02B2)
                check(expected)
                emit(0x807E02B4)
                check(2)
    for alignment in (-3, 2):
        emit(0x807E02B8, 0x38800000 | (alignment & 65535), 0x38A00000)
        call("SetBevelButtonTextAlignment")
        check(-50)
    emit(0x38600000, 0x38800001, 0x38A00000)
    call("SetBevelButtonTextAlignment")
    check(-50)
    # Check the rendered glyph's leftmost pixel, excluding the control frame.
    # Positive offsets move left-aligned text right and right-aligned text left;
    # centered text ignores the offset.
    call("GetSysDirection")
    emit(0x907E02C4)
    for case, (alignment, offset, direction) in enumerate(
        ((-2, 0, 0), (-2, 3, 0), (1, 0, 0), (1, 3, 0), (-1, 0, 0), (-1, 3, 0), (0, 0, 0), (0, 0, -1))
    ):
        emit(0x38600000 | (direction & 65535))
        call("SetSysDirection")
        emit(0x807E02B8, 0x38800000 | (alignment & 65535), 0x38A00000 | offset)
        call("SetBevelButtonTextAlignment")
        check(0)
        emit(0x807E02B8)
        call("DrawControlInCurrentPort")
        emit(0x38607FFF, 0x907E02D0, 0x38600007, 0x907E02D4, 0x38600007, 0x907E02D8)
        scan = len(code)
        emit(0x807E02D4, 0x809E02D8, 0x38BE01A0)
        call("GetCPixel")
        emit(0xA87E01A0, 0x2C030000)
        skip = len(code)
        emit(0x40820000)
        emit(0x807E02D4, 0x809E02D0, 0x7C032000)
        skip_min = len(code)
        emit(0x40800000)
        emit(0x907E02D0)
        code[skip] |= (len(code) - skip) * 4
        code[skip_min] |= (len(code) - skip_min) * 4
        emit(0x807E02D4, 0x38630001, 0x907E02D4, 0x2C030019)
        branch = len(code)
        emit(0x41800000 | (((scan - branch) * 4) & 65535))
        emit(0x38600007, 0x907E02D4, 0x807E02D8, 0x38630001, 0x907E02D8, 0x2C030019)
        branch = len(code)
        emit(0x41800000 | (((scan - branch) * 4) & 65535))
        emit(0x807E02D0, 0x907E02E0 | (case * 4), 0x2C037FFF)
        failures.append(len(code))
        emit(0x41820000)
    for newer, baseline, delta in ((740, 736, 3), (748, 744, 0), (756, 752, -3)):
        emit(0x807E0000 | newer, 0x809E0000 | baseline, 0x7C641850)
        check(delta)
    for left, right in ((736, 744), (744, 752)):
        emit(0x807E0000 | left, 0x809E0000 | right, 0x7C032000)
        failures.append(len(code))
        emit(0x40800000)
    for newer, baseline in ((760, 736), (764, 752)):
        emit(0x807E0000 | newer, 0x809E0000 | baseline, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    emit(0x807E02C4)
    call("SetSysDirection")
    # Generic data setters preserve the other field and reject bad sizes.
    for tag, value in ((0x74616C69, -2), (0x746F6666, 2)):
        emit(0x38600000 | (value & 65535), 0xB07E02B0)
        for size, expected in ((1, -50), (2, 0)):
            emit(
                0x807E02B8,
                0x38800000,
                0x3CA00000 | (tag >> 16),
                0x60A50000 | (tag & 65535),
                0x38C00000 | size,
                0x38FE02B0,
            )
            call("SetControlData")
            check(expected)
        for read_tag, expected in ((0x74616C69, -2), (0x746F6666, 0 if tag == 0x74616C69 else 2)):
            emit(
                0x807E02B8,
                0x38800000,
                0x3CA00000 | (read_tag >> 16),
                0x60A50000 | (read_tag & 65535),
                0x38C00002,
                0x38FE02B2,
                0x391E02B4,
            )
            call("GetControlData")
            check(0)
            emit(0xA87E02B2)
            check(expected)
    emit(0x807E02B8, 0x38800001, 0x38A00000)
    call("SetBevelButtonTextAlignment")
    check(0)
    # Image wells need a native CDEF as well as content accessors. Resource
    # and borrowed-picture content render, clear, and survive control disposal.
    emit(0x38600087, 0x809E0180)
    call("GetNewControl")
    emit(0x907E02D4, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x807E02D4, 0x389E02D0)
    call("GetControlFeatures")
    check(0)
    emit(0x807E02D0)
    check(64)
    emit(0x807E02D4, 0x389E02C8)
    call("GetImageWellContentInfo")
    check(0)
    emit(0xA87E02C8)
    check(3)
    emit(0xA87E02CA)
    check(201)
    emit(0x807E02D4)
    call("DrawControlInCurrentPort")
    pixel(10, 0)
    emit(0x38600000, 0x389E02C0)
    call("SetImageWellContentInfo")
    check(-50)
    emit(0x807E02D4, 0x38800000)
    call("GetImageWellContentInfo")
    check(-50)
    emit(0x807E02B8, 0x389E02C0)
    call("SetImageWellContentInfo")
    check(-30581)
    emit(0x807E02D4, 0x389E02C0)
    call("SetImageWellContentInfo")
    check(0)
    emit(0x807E02D4)
    call("DrawControlInCurrentPort")
    pixel(10, -1)
    emit(0x386000C9)
    call("GetPicture")
    emit(0x907E02D8, 0x907E02C2, 0x38600083, 0xB07E02C0)
    emit(0x807E02D4, 0x38800000, 0x3CA0636F, 0x60A56E74, 0x38C00005, 0x38FE02C0)
    call("SetControlData")
    check(-50)
    emit(0x807E02D4, 0x38800000, 0x3CA0636F, 0x60A56E74, 0x38C00006, 0x38FE02C0)
    call("SetControlData")
    check(0)
    emit(0x807E02D4, 0x38800000, 0x3CA0636F, 0x60A56E74, 0x38C00006, 0x38FE02C8, 0x391E02D0)
    call("GetControlData")
    check(0)
    emit(0x807E02D0)
    check(6)
    emit(0xA87E02C8)
    check(131)
    emit(0x807E02CA, 0x809E02D8, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x38607FFF, 0xB07E02C0, 0x807E02D4, 0x389E02C0)
    call("SetImageWellContentInfo")
    check(-4)
    emit(0x807E02D4)
    call("DrawControlInCurrentPort")
    pixel(10, 0)
    emit(0x807E02D4)
    call("DisposeControl")
    emit(0x807E02D8)
    call("GetHandleSize")
    check(24)
    # Bevels use the same content descriptors, with their own typed accessors.
    emit(0x38600088, 0x809E0180)
    call("GetNewControl")
    emit(0x907E02E0, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x807E02E0, 0x389E02C8)
    call("GetBevelButtonContentInfo")
    check(0)
    emit(0xA87E02C8)
    check(3)
    emit(0xA87E02CA)
    check(201)
    # Menu selection is a separate word from the pressed/toggle value.
    for value in (0, 7, 32767):
        emit(0x807E02E0, 0x38800000 | value)
        call("SetBevelButtonMenuValue")
        check(0)
        emit(0x38605AA5, 0xB07E02CA, 0x807E02E0, 0x389E02C8)
        call("GetBevelButtonMenuValue")
        check(0)
        emit(0xA87E02C8)
        check(value)
        emit(0xA87E02CA)
        check(0x5AA5)
        emit(0x807E02E0, 0x38800000, 0x3CA06D76, 0x60A5616C, 0x38C00002, 0x38FE02C8, 0x391E02D0)
        call("GetControlData")
        check(0)
        emit(0xA87E02C8)
        check(value)
        emit(0x807E02D0)
        check(2)
        emit(0x807E02E0)
        call("GetControlValue")
        check(0)
    emit(0x807E02E0, 0x38800001)
    call("SetControlValue")
    emit(0x807E02E0, 0x389E02C8)
    call("GetBevelButtonMenuValue")
    check(0)
    emit(0xA87E02C8)
    check(32767)
    emit(0x807E02E0, 0x38800000)
    call("SetControlValue")
    emit(0x807E02E0, 0x3880FFFF)
    call("SetBevelButtonMenuValue")
    check(-50)
    emit(0x807E02E0, 0x38800000)
    call("GetBevelButtonMenuValue")
    check(-50)
    emit(0x38600000, 0x38800001)
    call("SetBevelButtonMenuValue")
    check(-50)
    emit(0x807E0260, 0x38800001)
    call("SetBevelButtonMenuValue")
    check(-30581)
    # Graphic alignment state and Point offsets use native setter arguments
    # (horizontal, vertical) and big-endian control data (vertical, horizontal).
    for alignment in range(-1, 9):
        emit(0x807E02E0, 0x38800000 | (alignment & 65535), 0x38A00003, 0x38C0FFFC)
        call("SetBevelButtonGraphicAlignment")
        check(0)
        for tag, size in ((0x67616C69, 2), (0x676F6666, 4)):
            emit(
                0x807E02E0,
                0x38800000,
                0x3CA00000 | (tag >> 16),
                0x60A50000 | (tag & 65535),
                0x38C00000 | size,
                0x38FE02C8,
                0x391E02D0,
            )
            call("GetControlData")
            check(0)
            emit(0x807E02D0)
            check(size)
            emit(0xA87E02C8)
            check(alignment if size == 2 else -4)
            if size == 4:
                emit(0xA87E02CA)
                check(3)
    for alignment in (-2, 9):
        emit(0x807E02E0, 0x38800000 | (alignment & 65535), 0x38A00000, 0x38C00000)
        call("SetBevelButtonGraphicAlignment")
        check(-50)
    emit(0x38600000, 0x38800000, 0x38A00000, 0x38C00000)
    call("SetBevelButtonGraphicAlignment")
    check(-50)
    call("GetSysDirection")
    emit(0x907E02F8)
    cases = [
        (a, 0, 0, 0, x, y)
        for a, x, y in (
            (0, 8, 8),
            (1, 7, 8),
            (2, 9, 8),
            (3, 8, 7),
            (4, 8, 9),
            (5, 7, 7),
            (6, 7, 9),
            (7, 9, 7),
            (8, 9, 9),
        )
    ]
    cases += [
        (-1, 0, 0, 0, 7, 8),
        (-1, 0, 0, -1, 9, 8),
        (0, 5, -3, 0, 8, 8),
        (1, 1, 1, 0, 8, 9),
        (2, 1, 1, 0, 8, 9),
        (5, 1, 1, 0, 8, 8),
        (1, -1, 0, 0, 7, 8),
    ]
    for alignment, horizontal, vertical, direction, expected_x, expected_y in cases:
        emit(0x38600000 | (direction & 65535))
        call("SetSysDirection")
        emit(
            0x807E02E0,
            0x38800000 | (alignment & 65535),
            0x38A00000 | (horizontal & 65535),
            0x38C00000 | (vertical & 65535),
        )
        call("SetBevelButtonGraphicAlignment")
        check(0)
        emit(0x807E02E0)
        call("DrawControlInCurrentPort")
        emit(0x38607FFF, 0x907E02E4, 0x907E02E8, 0x38600007, 0x907E02EC, 0x907E02F0)
        scan = len(code)
        emit(0x807E02EC, 0x809E02F0, 0x38BE01A0)
        call("GetCPixel")
        emit(0xA87E01A0, 0x2C030000)
        skip = len(code)
        emit(0x40820000)
        for coordinate, minimum in ((748, 740), (752, 744)):
            emit(0x807E0000 | coordinate, 0x809E0000 | minimum, 0x7C032000)
            skip_min = len(code)
            emit(0x40800000)
            emit(0x907E0000 | minimum)
            code[skip_min] |= (len(code) - skip_min) * 4
        code[skip] |= (len(code) - skip) * 4
        emit(0x807E02EC, 0x38630001, 0x907E02EC, 0x2C030019)
        branch = len(code)
        emit(0x41800000 | (((scan - branch) * 4) & 65535))
        emit(0x38600007, 0x907E02EC, 0x807E02F0, 0x38630001, 0x907E02F0, 0x2C030019)
        branch = len(code)
        emit(0x41800000 | (((scan - branch) * 4) & 65535))
        emit(0x807E02E4)
        check(expected_x)
        emit(0x807E02E8)
        check(expected_y)
    emit(0x38600006, 0x3880000A, 0x38BE01A0)
    call("GetCPixel")
    emit(0xA87E01A0)
    check(-1)  # Negative graphic offsets cannot paint outside the content area.
    emit(0x807E02F8)
    call("SetSysDirection")
    emit(0x38600002, 0xB07E02C0, 0x3860FFF9, 0xB07E02C2)
    for size, expected in ((3, -50), (4, 0)):
        emit(0x807E02E0, 0x38800000, 0x3CA0676F, 0x60A56666, 0x38C00000 | size, 0x38FE02C0)
        call("SetControlData")
        check(expected)
    emit(0x38600001, 0xB07E02C0, 0x807E02E0, 0x38800000, 0x3CA06761, 0x60A56C69, 0x38C00002, 0x38FE02C0)
    call("SetControlData")
    check(0)
    emit(0x807E02E0, 0x38800000, 0x3CA0676F, 0x60A56666, 0x38C00004, 0x38FE02C8, 0x391E02D0)
    call("GetControlData")
    check(0)
    emit(0xA87E02C8)
    check(2)
    emit(0xA87E02CA)
    check(-7)
    emit(0x807E02E0, 0x38800000, 0x38A00000, 0x38C00000)
    call("SetBevelButtonGraphicAlignment")
    check(0)
    emit(0x807E02E0)
    call("DrawControlInCurrentPort")
    pixel(10, 0)
    emit(0x38600000, 0xB07E02C0, 0x807E02E0, 0x389E02C0)
    call("SetBevelButtonContentInfo")
    check(0)
    emit(0x807E02E0)
    call("DrawControlInCurrentPort")
    pixel(10, -1)
    emit(0x38600000, 0x389E02C0)
    call("SetBevelButtonContentInfo")
    check(-50)
    emit(0x807E02E0, 0x38800000)
    call("GetBevelButtonContentInfo")
    check(-50)
    emit(0x807E02E0, 0x389E02C0)
    call("SetImageWellContentInfo")
    check(-30581)
    emit(0x38600083, 0xB07E02C0, 0x807E02D8, 0x907E02C2)
    emit(0x807E02E0, 0x389E02C0)
    call("SetBevelButtonContentInfo")
    check(0)
    emit(0x807E02E0, 0x38800000, 0x3CA0636F, 0x60A56E74, 0x38C00006, 0x38FE02C8, 0x391E02D0)
    call("GetControlData")
    check(0)
    emit(0xA87E02C8)
    check(131)
    emit(0x807E02CA, 0x809E02D8, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E02E0)
    call("DrawControlInCurrentPort")
    pixel(10, 0)
    # Clearing through the generic data tag also removes the borrowed picture.
    emit(0x38600000, 0xB07E02C0, 0x807E02E0, 0x38800000, 0x3CA0636F, 0x60A56E74, 0x38C00006, 0x38FE02C0)
    call("SetControlData")
    check(0)
    emit(0x807E02E0)
    call("DrawControlInCurrentPort")
    pixel(10, -1)
    emit(0x38600083, 0xB07E02C0, 0x807E02E0, 0x389E02C0)
    call("SetBevelButtonContentInfo")
    check(0)
    emit(0x807E02E0)
    call("DisposeControl")
    emit(0x807E02D8)
    call("GetHandleSize")
    check(24)  # A borrowed picture survives replacement and disposal.
    emit(0x807E02B8)
    call("DrawControlInCurrentPort")
    # A labeled bevel button must paint text inside its frame, rather than
    # merely retaining a placement value that the renderer never uses.
    emit(0x38600005, 0x907E0224, 0x907E0228, 0x38600000, 0x907E022C)
    label_scan = len(code)
    emit(0x807E0224, 0x809E0228, 0x38BE01A0)
    call("GetCPixel")
    emit(0xA87E01A0, 0x2C030000)
    label_skip = len(code)
    emit(0x40820000)
    emit(0x807E022C, 0x38630001, 0x907E022C)
    code[label_skip] |= (len(code) - label_skip) * 4
    emit(0x807E0224, 0x38630001, 0x907E0224, 0x2C03001B)
    branch = len(code)
    emit(0x41800000 | (((label_scan - branch) * 4) & 65535))
    emit(0x38600005, 0x907E0224, 0x807E0228, 0x38630001, 0x907E0228, 0x2C03001B)
    branch = len(code)
    emit(0x41800000 | (((label_scan - branch) * 4) & 65535))
    emit(0x807E022C, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    # HandleControlClick takes a packed local Point and returns the hit part.
    # Position the bevel around the current mouse, then queue a release so the
    # tracking test needs no human input or held physical mouse button.
    emit(0x387E02F0)
    call("GetMouse")
    emit(0x807E02B8, 0xA89E02F2, 0xA8BE02F0, 0x3884FFFE, 0x38A5FFFE)
    call("MoveControl")
    emit(0x807E02B8, 0x38800020, 0x38A00020)
    call("SizeControl")
    for control in (0, 0x2B8):
        emit(0x807E0000 | control if control else 0x38600000, 0x3C808000, 0x60848000, 0x38A00000, 0x38C00000)
        call("HandleControlClick")
        check(0)
    emit(0x807E02B8, 0x388000FF)
    call("HiliteControl")
    emit(0x807E02B8, 0x809E02F0, 0x38A00000, 0x38C00000)
    call("HandleControlClick")
    check(0)
    emit(0x807E02B8, 0x38800000)
    call("HiliteControl")
    for action in (0, -1):
        emit(0x38600002, 0x38800000)
        call("PostEvent")
        check(0)
        emit(0x807E02B8, 0x809E02F0, 0x38A00100, 0x38C00000 | (action & 0xFFFF))
        call("HandleControlClick")
        check(10)
    emit(0x807E02B8)
    call("DisposeControl")
    # REALbasic paints the client background after drawing the size box.
    # Finish the requested grow icon when control returns to the event loop.
    emit(0x807E0180)
    call("DrawGrowIcon")
    emit(0x387E0190)
    call("RGBBackColor")
    emit(0x387E0120)
    call("EraseRect")
    emit(0x38600014, 0x38800015, 0x38BE01A0)
    call("GetCPixel")
    emit(0xA87E01A0)
    check(-1)
    emit(0x38600000, 0x389E02A0)
    call("GetNextEvent")
    emit(0x38600014, 0x38800015, 0x38BE01A0)
    call("GetCPixel")
    emit(0xA87E01A0, 0x2C03FFFF)
    failures.append(len(code))
    emit(0x41820000)
    # Deferred corner refresh must not draw horizontal/vertical scrollbar
    # dividers across a window that has no scrollbars.
    for x, y in ((4, 17), (17, 4)):
        emit(0x38600000 | x, 0x38800000 | y, 0x38BE01A0)
        call("GetCPixel")
        emit(0xA87E01A0)
        check(-1)
    emit(0x807E0180)
    call("DisposeWindow")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    callback = len(code) * 4
    emit(
        0x90660000,
        0x90860004,
        0x90A60008,
        0x90C6000C,
        0x80660010,
        0x38630001,
        0x90660010,
        0x4E800020,
    )
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(1120)
    data[656:658] = b"A\r"
    struct.pack_into(">3H", data, 528, 65535, 65535, 0)
    data[544] = ord("X")
    struct.pack_into(">3H", data, 568, 0, 0, 65535)
    struct.pack_into(">6h", data, 592, 7, 3, 9, 0, 0, 0)
    data[616:620] = b"\x02Ys\0"
    data[620] = 1
    struct.pack_into(">4h", data, 624, 0, 0, 16, 8)
    struct.pack_into(">4h", data, 632, 0, 0, 16, 48)
    struct.pack_into(">4h", data, 640, 0, 0, 32, 128)
    data[480:496] = b"\x0fNoSuchFont98765"
    struct.pack_into(">II", data, 280, callback, 0)
    struct.pack_into(">4h", data, 288, 0, 0, 32, 32)
    struct.pack_into(">4h", data, 296, 4, 4, 16, 28)
    struct.pack_into(">4h", data, 304, 40, 40, 72, 72)
    struct.pack_into(">3H", data, 400, 65535, 65535, 65535)
    app = pef(
        code,
        data,
        imports=[
            (
                (
                    "CarbonLib"
                    if name in {"NewWindow", "FMGetFontFamilyFromName"}
                    else (
                        "AppearanceLib"
                        if name.startswith("DrawTheme") or name in {"DrawControlInCurrentPort", "GetControlFeatures"}
                        else "InterfaceLib"
                    )
                ),
                name,
                2,
            )
            for name in names
        ],
        main=0,
        reloc=[0x4600, 0xA000, 768, 0x4A00 | (len(names) - 1), 0xA000, 280, 0x4600],
    )
    control = struct.pack(">4hhBBhhhI", 4, 4, 20, 28, 0, 1, 0, 1, 0, 0, 0) + b"\x02OK"
    text_control = struct.pack(">4hhBBhhhI", 4, 4, 20, 28, 0, 0, 0, 0, 0, 296, 0) + b"\0"
    dialog = struct.pack(">4hhBBBBIh", 60, 60, 140, 220, 0, 0, 0, 0, 0, 0, 129) + b"\0\0"
    items = struct.pack(">hI4hBB", 1, 0, 8, 8, 28, 68, 4, 2) + b"OK"
    items += struct.pack(">I4hBB", 0, 36, 8, 56, 140, 8, 0)
    write_fragment(
        destination,
        "PPC Appearance Probe",
        app,
        1,
        [
            (b"CNTL", 128, control),
            (b"CNTL", 136, struct.pack(">4hhBBhhhI", 4, 4, 28, 28, 0, 1, 0, 201, 3, 32, 0) + b"\0"),
            (b"CNTL", 135, struct.pack(">4hhBBhhhI", 4, 4, 28, 28, 201, 1, 0, 0, 3, 176, 0) + b"\0"),
            (
                b"PICT",
                201,
                struct.pack(">H4h", 24, 0, 0, 16, 16)
                + b"\x11\x01\x31"
                + struct.pack(">4h", 0, 0, 16, 16)
                + b"\xff\0\0",
            ),
            (b"CNTL", 130, text_control),
            (b"CNTL", 134, struct.pack(">4hhBBhhhI", 4, 4, 28, 28, 0, 0, 0, 0, 1, 1, 0) + b"\0"),
            (b"CNTL", 133, struct.pack(">4hhBBhhhI", 4, 4, 28, 28, 0, 0, 0, 0, 0, 128, 0) + b"\0"),
            (b"CNTL", 132, struct.pack(">4hhBBhhhI", 4, 4, 28, 28, 0, 1, 0, 0, 0, 32, 0) + b"\x01X"),
            (
                b"CNTL",
                131,
                struct.pack(">4hhBBhhhI", 4, 4, 20, 28, 0, 0, 0, 0, 0, 272, 0) + b"\0",
            ),
            (b"DLOG", 129, dialog),
            (b"DITL", 129, items),
        ],
    )


if __name__ == "__main__":
    make_appearance_probe(Path(sys.argv[1]))
