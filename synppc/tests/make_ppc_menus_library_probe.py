#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_menus_library_probe.py
#
#   Summary:   Generate original PPC checks for Classic MenusLib bindings and native menu state.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original fixture; Classic MenusLib argument/error contract checked against local System 9 reference.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    p = Probe(
        [
            ("MenusLib", name, 2)
            for name in (
                "SetSubmenuParentChoosable",
                "GetSubmenuParentChoosable",
                "ShowMenuBar",
                "HideMenuBar",
                "DisableMenuItem",
                "IsMenuItemEnabled",
            )
        ]
        + [
            ("InterfaceLib", name, 2)
            for name in (
                "InitWindows",
                "InitMenus",
                "NewMenu",
                "InsertMenuItem",
                "DeleteMenuItem",
                "AppendMenu",
                "InsertMenu",
                "DeleteMenu",
                "DisposeMenu",
                "EnableItem",
                "HMSetMenuResID",
                "LMGetMBarHook",
                "LMSetMBarHook",
                "LMGetPaintWhite",
                "LMSetPaintWhite",
                "LMGetCurApName",
                "LMGetSdVolume",
                "LaunchApplication",
                "DebugStr",
            )
        ]
        + [("CarbonLib", "IsMenuBarVisible", 2)]
    )
    for offset, value in ((0x280, b"Native Menu"), (0x2C0, b"First;Second"), (0x2E0, b"Inserted")):
        p.data[offset : offset + 1 + len(value)] = bytes([len(value)]) + value
    p.call("InitWindows")
    p.call("InitMenus")
    for argument in (0x38600000, 0x387E0340):
        p.emit(argument)
        p.call("LaunchApplication")
        p.expect(-50, "Classic launch rejects a null or nonextended block")
    p.call("LMGetCurApName")
    p.expect(0x910, "Classic callers receive the live low-memory application name")
    p.call("LMGetSdVolume")
    p.emit(0x88800260)
    p.equal("Classic sound volume reads the actual unsigned low-memory byte")
    p.call("LMGetMBarHook")
    p.emit(0x907E01D0, 0x387E0340)
    p.call("LMSetMBarHook")
    p.call("LMGetMBarHook")
    p.emit(0x389E0340)
    p.equal("menu-hook pointer round-trips through the PPC accessors")
    p.emit(0x80600A2C, 0x389E0340)
    p.equal("the hook is stored in the actual guest low-memory slot")
    p.emit(0x807E01D0)
    p.call("LMSetMBarHook")
    p.call("LMGetPaintWhite")
    p.emit(0x907E01D4, 0x38601234)
    p.call("LMSetPaintWhite")
    p.call("LMGetPaintWhite")
    p.expect(0x1234, "PaintWhite retains the full classic word")
    p.emit(0xA06009DC)
    p.expect(0x1234, "the window manager sees the guest-endian PaintWhite word")
    p.emit(0x807E01D4)
    p.call("LMSetPaintWhite")
    p.emit(0x3860251C, 0x389E0280)  # Menu ID 9500.
    p.call("NewMenu")
    p.nonzero("create an actual Classic menu")
    p.emit(0x907E0180, 0x389E02C0)
    p.call("AppendMenu")
    p.emit(0x807E0180, 0x38800000)
    p.call("InsertMenu")
    p.call("ShowMenuBar")
    p.call("IsMenuBarVisible")
    p.expect(1, "MenusLib shows the native menu bar")
    p.call("HideMenuBar")
    p.call("IsMenuBarVisible")
    p.expect(0, "MenusLib hides the native menu bar")
    p.call("ShowMenuBar")
    p.call("IsMenuBarVisible")
    p.expect(1, "menu bar visibility can be restored")
    p.emit(0x807E0180, 0x38800002)
    p.call("IsMenuItemEnabled")
    p.expect(1, "new menu item is enabled")
    p.emit(0x807E0180, 0x38800002)
    p.call("DisableMenuItem")
    p.emit(0x807E0180, 0x38800002)
    p.call("IsMenuItemEnabled")
    p.expect(0, "MenusLib changes actual item enable state")
    p.emit(0x807E0180, 0x38800002)
    p.call("EnableItem")
    p.emit(0x807E0180, 0x38800002)
    p.call("IsMenuItemEnabled")
    p.expect(1, "InterfaceLib and MenusLib share item state")
    p.emit(0x807E0180, 0x38800001, 0x38A00001)
    p.call("SetSubmenuParentChoosable")
    p.expect(0, "set native submenu-parent state without Carbon initialization")

    def parent(item, expected):
        p.emit(0x3C60A5A5, 0x6063A5A5, 0x907E01C0)
        p.emit(0x807E0180, 0x38800000 | item, 0x38BE01C1)
        p.call("GetSubmenuParentChoosable")
        p.expect(0, "query native submenu-parent state")
        p.emit(0x887E01C1)
        p.expect(expected, "submenu-parent setting is retained per item")
        for offset in (0x1C0, 0x1C2, 0x1C3):
            p.emit(0x887E0000 | offset)
            p.expect(0xA5, "Boolean output preserves adjacent bytes")

    parent(1, 1)
    parent(2, 0)
    p.emit(0x807E0180, 0x389E02E0, 0x38A00000)
    p.call("InsertMenuItem")
    parent(1, 0)
    parent(2, 1)
    p.emit(0x807E0180, 0x38800001)
    p.call("DeleteMenuItem")
    parent(1, 1)
    p.emit(0x807E0180, 0x38800001, 0x38A00000)
    p.call("SetSubmenuParentChoosable")
    p.expect(0, "clear native submenu-parent state")
    parent(1, 0)
    p.emit(0x807E0180, 0x38800001, 0x38A00001)
    p.call("SetSubmenuParentChoosable")
    p.expect(0, "restore native submenu-parent state")
    p.emit(0x807E0180, 0x38800001)
    p.call("DeleteMenuItem")
    parent(1, 0)
    p.emit(0x38600000, 0x38800001, 0x38A00001)
    p.call("SetSubmenuParentChoosable")
    p.expect(-50, "Classic MenusLib rejects a null menu with paramErr")
    p.emit(0x807E0180, 0x38800003, 0x38A00001)
    p.call("SetSubmenuParentChoosable")
    p.expect(-50, "invalid menu item is rejected")
    p.emit(0x807E0180, 0x38800001, 0x38A00000)
    p.call("GetSubmenuParentChoosable")
    p.expect(-50, "query requires a Boolean output")
    p.emit(0x3860251C, 0x38800100)
    p.call("HMSetMenuResID")
    p.expect(-855, "native Help Manager entry reports its real initialization status")
    p.emit(0x3860251C)
    p.call("DeleteMenu")
    p.emit(0x807E0180)
    p.call("DisposeMenu")
    marker = b"MENUS_LIBRARY_OK"
    p.data[0x300 : 0x301 + len(marker)] = bytes([len(marker)]) + marker
    p.emit(0x387E0300)
    p.call("DebugStr")
    p.finish(destination, "PPC Menus Library Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
