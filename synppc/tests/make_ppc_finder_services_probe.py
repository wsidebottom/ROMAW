#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_finder_services_probe.py
#
#   Summary:   Generate grouped checks for Finder's native services, object callbacks and explicit optional-service errors.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe, check_object_accessors, check_file_icons, check_picture_controls, check_progress_controls;
#              check_icon_file_registration, check_device_parameter_blocks, check_date_formatting, check_label_icon, check_desktop_drag, check_resource_overrides;
#              script entry point.
#
#   Origin:    Original ROMAW Finder startup service regression fixture.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

import sys
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    names = [
        "InitWindows",
        "Gestalt",
        "GetCurrentProcess",
        "GetProcessInformation",
        "HSetVol",
        "FSMakeFSSpec",
        "CurResFile",
        "FSpOpenResFile",
        "UseResFile",
        "Get1Resource",
        "ChangedResource",
        "WriteResource",
        "CloseResFile",
        "ResError",
        "GetResFileAttrs",
        "SetResFileAttrs",
        "TempNewHandle",
        "NewHandle",
        "GetHandleSize",
        "DisposeHandle",
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "InsTime",
        "InsXTime",
        "PrimeTime",
        "RmvTime",
        "Delay",
        "DebugStr",
        "PostEvent",
        "OSEventAvail",
        "GetOSEvent",
        "NewCWindow",
        "NewCDialog",
        "DisposeDialog",
        "ModalDialog",
        "DisposeWindow",
        "GetMainDevice",
        "NewGWorld",
        "GetGWorldDevice",
        "DisposeGWorld",
        "PPCOpenSync",
        "PPCCloseSync",
        "LMGetEventQueue",
        "GetEvQHdr",
        "CallUniversalProc",
        "NGetTrapAddress",
        "AECreateList",
        "HMGetBalloons",
        "HMSetBalloons",
        "IUDateString",
        "IUTimeString",
        "IUDatePString",
        "IUTimePString",
        "IUGetIntl",
        "EqualString",
        "PBControlSync",
        "PBStatusSync",
        "NSetTrapAddress",
        "NewControl",
        "GetPicture",
        "GetCIcon",
        "PlotCIcon",
        "DisposeCIcon",
        "GetCtlValue",
        "Draw1Control",
        "TestControl",
        "TrackControl",
        "GetCPixel",
        "GetBackColor",
        "GetPort",
        "EraseRect",
        "SetPort",
        "FrontWindow",
        "DisposeControl",
        "GetControlFeatures",
        "GetControlData",
        "SetControlData",
        "NewRgn",
        "RectRgn",
        "DisposeRgn",
        "PtInRgn",
        "AEPutKeyPtr",
        "AEPutKeyDesc",
        "AEDisposeDesc",
    ]
    imports = [("InterfaceLib", name, 2) for name in names]
    imports += [
        ("DragLib", name, 2)
        for name in (
            "NewDrag",
            "DisposeDrag",
            "TrackDrag",
            "InstallTrackingHandler",
            "InstallReceiveHandler",
            "RemoveTrackingHandler",
            "RemoveReceiveHandler",
            "GetDragAttributes",
            "GetDragMouse",
        )
    ]
    imports += [("PrivateInterfaceLib", name, 2) for name in ("RequestVolumeNotification", "DeclineVolumeNotification")]
    imports += [("PrivateInterfaceLib", name, 2) for name in ("GetMap", "GetOverrideAttributes")]
    imports.append(("PrivateInterfaceLib", "NewDesktopLayerOwner", 2))
    imports.append(("PrivateInterfaceLib", "SysZoneFloatSizes", 2))
    imports.append(("PrivateInterfaceLib", "VM_GetHoldableBytes", 2))
    imports += [("PrivateInterfaceLib", name, 2) for name in ("SetIconDevice", "GetIconDevice")]
    imports += [
        ("WindowsLib", name, 2)
        for name in (
            "SetWindowProxyFSSpec",
            "GetWindowProxyIcon",
            "SetWindowProxyIcon",
            "InvalWindowRect",
            "ValidWindowRect",
            "TransitionWindow",
        )
    ]
    imports += [
        ("AppearanceLib", name, 2)
        for name in (
            "GetWindowFeatures",
            "IsWindowCollapsed",
            "CollapseWindow",
            "PlayThemeSound",
            "DrawThemeWindowHeader",
            "DrawThemeWindowListViewHeader",
            "DrawThemePrimaryGroup",
            "DrawThemeSecondaryGroup",
            "ApplyThemeBackground",
            "DrawThemeTabPane",
            "DrawThemePopupArrow",
            "DrawThemeFocusRegion",
            "GetThemeAccentColors",
            "GetThemeMenuTitleExtra",
            "GetThemeMenuBarShape",
            "GetThemeMenuBackgroundRegion",
            "DrawThemeMenuBackground",
            "DrawThemeTickMark",
            "DrawThemeScrollBarDelimiters",
        )
    ]
    imports += [
        ("IconServicesLib", name, 2)
        for name in (
            "GetIconRefByName",
            "GetIconRefFromFolder",
            "GetIconRefFromFile",
            "GetIconRefFromFolder",
            "UpdateIconRef",
            "GetCustomIconsEnabled",
            "SetCustomIconsEnabled",
            "GetDeepIconsEnabled",
            "SetDeepIconsEnabled",
            "ReleaseIconRef",
            "GetIconRefOwners",
            "IsValidIconRef",
            "IconRefToIconFamily",
            "GetIconFamilyData",
        )
    ]
    imports += [
        ("ObjectSupportLib", name, 2)
        for name in (
            "AEObjectInit",
            "AEInstallObjectAccessor",
            "AEGetObjectAccessor",
            "AERemoveObjectAccessor",
            "AECallObjectAccessor",
            "AESetObjectCallbacks",
            "AEResolve",
            "AEDisposeToken",
        )
    ]
    imports += [
        ("MacDialogsLib", name, 2)
        for name in (
            "SetModalDialogEventMask",
            "GetModalDialogEventMask",
            "SetDialogTimeout",
        )
    ]
    imports.append(("CarbonLib", "GetDialogTimeout", 2))
    imports += [("ThreadsLib", name, 2) for name in ("GetThreadCurrentTaskRef", "GetThreadStateGivenTaskRef")]
    imports += [
        ("CarbonLib", name, 2)
        for name in (
            "CreateProgressBarControl",
            "GetControl32BitValue",
            "GetControl32BitMinimum",
            "GetControl32BitMaximum",
            "ReadIconFile",
            "WriteIconFile",
            "RegisterIconRefFromIconFile",
            "RegisterIconRefFromResource",
            "UnregisterIconRef",
            "OverrideIconRef",
            "OverrideIconRefFromResource",
            "RemoveIconRefOverride",
            "FlushIconRefs",
            "FlushIconRefsByVolume",
        )
    ]
    p = Probe(imports)
    p.import_base = 0x1000
    p.identify_failed_checks = True
    p.reserve_parameter_words(9)
    p.data.extend(bytes(0x1000 - len(p.data)))
    p.call("InitWindows")
    for slot, icon_type in ((0x2E0, 0x74727368), (0x2E4, 0x66747268)):
        p.emit(
            0x3C606D61,
            0x60636373,
            0x3C800000 | (icon_type >> 16),
            0x60840000 | (icon_type & 0xFFFF),
            0x38BE0000 | slot,
        )
        p.call("GetIconRefByName")
        p.expect(
            0,
            "empty/full Trash resolve actual bundled icon artwork without an isrv map",
        )
    p.emit(0x807E02E0, 0x809E02E4)
    p._check(0x7C032000, 0x40820000, "empty and full Trash have distinct IconRef identities")
    for slot in (0x2E0, 0x2E4):
        p.emit(0x807E0000 | slot)
        p.call("ReleaseIconRef")
        p.expect(0, "release the caller's Trash icon ownership")
    p.call("GetMainDevice")
    p.nonzero("an initialized screen device is available")
    p.emit(0x907E02D0)
    p.call("SetIconDevice")
    p.emit(0x387E02D4)
    p.call("GetIconDevice")
    p.emit(0x807E02D0, 0x809E02D4)
    p.equal("private icon-device gateway selects the supported screen")
    p.emit(0x38600000)
    p.call("SetIconDevice")
    p.emit(0x387E02D4)
    p.call("GetIconDevice")
    p.emit(0x807E02D0, 0x809E02D4)
    p.equal("automatic icon selection resolves to the supported screen")
    # The timer callback gets its task pointer from A1 through uppTimerProcInfo.
    p.routines.append((0x180, [0x90620208, 0x80820204, 0x38840001, 0x90820204, 0x4E800020]))
    p.emit(0x387E0180, 0x38800000, 0x6084B802, 0x38A00001)
    p.call("NewRoutineDescriptor")
    p.nonzero("create real PPC timer routine descriptor")
    p.emit(0x907E0106)
    for install in ("InsTime", "InsXTime"):
        p.emit(0x38000000, 0x901E0204, 0x901E0208, 0x387E0100)
        p.call(install)
        p.emit(0x807E010A)
        p.expect(-1, "installed timer remains dormant until primed")
        p.emit(0x387E0100, 0x38800001)
        p.call("PrimeTime")
        p.emit(0x38600004, 0x389E0200)
        p.call("Delay")
        p.emit(0x807E0204)
        p.expect(1, "primed timer invokes its callback once")
        p.emit(0x807E0208)
        p.pointer(0x100, "timer callback receives the original task pointer")
        p.emit(0x387E0100)
        p.call("RmvTime")
        p.emit(0x38600002, 0x389E0200)
        p.call("Delay")
        p.emit(0x807E0204)
        p.expect(1, "removed timer cannot run again")
    p.emit(0x807E0106)
    p.call("DisposeRoutineDescriptor")
    p.emit(0x387E0180, 0x38800000)
    p.call("RequestVolumeNotification")
    p.expect(
        -4,
        "optional volume notification reports unavailable instead of stopping execution",
    )
    p.emit(0x387E0180, 0x38800000)
    p.call("DeclineVolumeNotification")
    p.expect(0, "declining an unavailable volume notification is harmless")
    p.emit(0x3860000C, 0x3C801234, 0x60845678)
    p.call("PostEvent")
    p.expect(0, "post application event to the real OS event queue")
    for _ in range(2):
        p.emit(0x38601000, 0x389E0280)
        p.call("OSEventAvail")
        p.expect(1, "OSEventAvail repeatedly peeks without consuming the event")
        p.emit(0xA07E0280)
        p.expect(12, "event kind uses big-endian guest layout")
        p.emit(0x807E0282)
        p.expect(0x12345678, "peek retains the original event message")
    p.emit(0x38601000, 0x389E0280)
    p.call("GetOSEvent")
    p.expect(1, "GetOSEvent consumes the queued event")
    p.emit(0x38601000, 0x389E0280)
    p.call("OSEventAvail")
    p.expect(0, "consumed events are no longer available")
    p.emit(0x38600000)
    p.call("NewDesktopLayerOwner")
    p.expect(-50, "desktop layer ownership requires an output pointer")
    p.emit(0x387E02A0)
    p.call("NewDesktopLayerOwner")
    p.expect(0, "create actual native desktop layer ownership")
    p.emit(0x807E02A0)
    p.nonzero("desktop owner identifies an allocated application layer")
    p.emit(0x387E02A4)
    p.call("NewDesktopLayerOwner")
    p.expect(0, "repeat desktop ownership succeeds")
    p.emit(0x807E02A0, 0x809E02A4)
    p.equal("repeated ownership retains the same application layer")
    p.data[0x300:0x308] = struct.pack(">4h", 50, 50, 100, 150)
    p.data[0x310:0x315] = b"\x04Test"
    # Finder uses InterfaceLib to get the device for its drag image.
    p.emit(0x387E02D8, 0x38800020, 0x38BE0300, 0x38C00000, 0x80FE02D0, 0x39000002)
    p.call("NewGWorld")
    p.expect(0, "allocate a drag image that uses the existing screen device")
    p.emit(0x807E02D8)
    p.call("GetGWorldDevice")
    p.emit(0x809E02D0)
    p.equal("the direct InterfaceLib import returns the drag image's graphics device")
    p.emit(0x807E02D8)
    p.call("DisposeGWorld")
    p.emit(0x38600000)
    p.call("GetGWorldDevice")
    p.expect(0, "a missing GWorld has no graphics device")
    p.emit(
        0x38600000,
        0x389E0300,
        0x38BE0310,
        0x38C00001,
        0x38E00000,
        0x3900FFFF,
        0x39200001,
        0x39400000,
    )
    p.call("NewCWindow")
    p.nonzero("create a real native color window")
    p.emit(0x907E02C0)
    # Finder's folder template contains the Appearance chasing-arrows control.
    # It must be a functioning control rather than a failed CDEF allocation.
    p.emit(
        0x807E02C0,
        0x389E0460,
        0x38BE0310,
        0x38C00001,
        0x38E00000,
        0x39000000,
        0x39200001,
        0x39400070,
        0x38000000,
        0x90010038,
    )
    p.call("NewControl")
    p.nonzero("construct the native chasing-arrows control used by Finder's folder view")
    p.emit(0x907E0440, 0x389E0444)
    p.call("GetControlFeatures")
    p.expect(0, "chasing arrows expose real control features")
    p.emit(0x807E0444)
    p.expect(0x48, "chasing arrows request idle time and support animation data")
    p.emit(
        0x807E0440,
        0x38800000,
        0x3CA0616E,
        0x60A5696D,
        0x38C00001,
        0x38FE0448,
        0x391E044C,
    )
    p.call("GetControlData")
    p.expect(0, "read the actual chasing-arrows animation setting")
    p.emit(0x887E0448)
    p.expect(1, "new chasing-arrows controls animate by default")
    p.emit(0x807E044C)
    p.expect(1, "animation data has a one-byte Boolean ABI")
    p.emit(
        0x38000000,
        0x981E0448,
        0x807E0440,
        0x38800000,
        0x3CA0616E,
        0x60A5696D,
        0x38C00001,
        0x38FE0448,
    )
    p.call("SetControlData")
    p.expect(0, "stop the chasing-arrows animation through its actual control data")
    p.emit(
        0x807E0440,
        0x38800000,
        0x3CA0616E,
        0x60A5696D,
        0x38C00001,
        0x38FE0448,
        0x391E044C,
    )
    p.call("GetControlData")
    p.expect(0, "read back the stopped animation state")
    p.emit(0x887E0448)
    p.expect(0, "disabled animation remains stopped")
    p.emit(0x807E0440)
    p.call("DisposeControl")
    p.data[0x460:0x468] = struct.pack(">4h", 20, 20, 36, 36)
    p.emit(0x807E02C0, 0x389E0404)
    p.call("GetWindowFeatures")
    p.expect(0, "query the actual native WDEF's capabilities")
    p.emit(0x807E0404)
    p.expect(0x41, "classic document WDEF advertises grow and title, without WindowShade")
    p.emit(0x807E02C0)
    p.call("IsWindowCollapsed")
    p.expect(0, "native document window remains expanded")
    p.emit(0x807E02C0, 0x38800001)
    p.call("CollapseWindow")
    p.expect(-4, "unavailable WindowShade leaves the real window expanded")
    for operation in ("InvalWindowRect", "ValidWindowRect"):
        p.emit(0x807E02C0, 0x389E0300)
        p.call(operation)
        p.expect(0, "WindowsLib explicit-window update region service succeeds")
    for drawing in ("DrawThemeWindowHeader", "DrawThemeWindowListViewHeader"):
        p.emit(0x387E0300, 0x38800001)
        p.call(drawing)
        p.expect(0, "draw the native folder/window header with actual QuickDraw geometry")
    p.emit(0x38600000, 0x38800001)
    p.call("DrawThemeWindowHeader")
    p.expect(-50, "window header drawing rejects missing bounds")
    for drawing in ("DrawThemePrimaryGroup", "DrawThemeSecondaryGroup"):
        p.emit(0x387E0300, 0x38800001)
        p.call(drawing)
        p.expect(0, "the direct AppearanceLib import draws a group frame")
        p.emit(0x38600000, 0x38800001)
        p.call(drawing)
        p.expect(-50, "group drawing rejects missing bounds")
    p.emit(0x387E0450)
    p.call("GetPort")
    p.emit(0x807E02C0)
    p.call("SetPort")
    for kind, state, depth, is_color, gray in (
        (1, 1, 8, 1, 0xEEEE),
        (2, 2, 8, 1, 0xDDDD),
        (3, 0, 4, 0, 0xEEEE),
        (4, 1, 4, 1, 0xFFFF),
    ):
        p.emit(
            0x38600000 | kind,
            0x389E0300,
            0x38A00000 | state,
            0x38C00000 | depth,
            0x38E00000 | is_color,
        )
        p.call("ApplyThemeBackground")
        p.expect(0, "configure the current port for an embedded control")
        p.emit(0x387E0454)
        p.call("GetBackColor")
        for address in (0x454, 0x456, 0x458):
            p.emit(0xA07E0000 | address)
            p.expect(
                gray,
                "the native Appearance background matches the Platinum device and state",
            )
    p.emit(0x38600003, 0x389E0300, 0x38A00002, 0x38C00008, 0x38E00001)
    p.call("ApplyThemeBackground")
    p.expect(-50, "only placards accept the pressed state")
    p.emit(0x38600002, 0x38800000, 0x38A00001, 0x38C00008, 0x38E00001)
    p.call("ApplyThemeBackground")
    p.expect(-50, "the embedding API requires bounds")
    p.data[0x490:0x498] = struct.pack(">4h", 8, 8, 30, 30)
    p.data[0x49C:0x4A4] = struct.pack(">4h", 14, 14, 24, 24)
    p.emit(0x387E0490)
    p.call("EraseRect")
    p.call("NewRgn")
    p.nonzero("allocate a focus region")
    p.emit(0x907E0498, 0x389E049C)
    p.call("RectRgn")
    for focused in (1, 0):
        p.emit(0x807E0498, 0x38800000 | focused)
        p.call("DrawThemeFocusRegion")
        p.expect(0, "draw or erase focus around the same caller-owned region")
        p.emit(0x38600010, 0x3880000B, 0x38BE0478)
        p.call("GetCPixel")
        p.emit(0xA07E0478)
        if focused:
            p.emit(0x6863FFFF)
            p.nonzero("focus paints the outer two-pixel frame")
        else:
            p.expect(0xFFFF, "clearing focus restores the background")
    p.emit(0x387E049C, 0x38800000, 0x80BE0498)
    p.call("GetThemeMenuBackgroundRegion")
    p.expect(0, "build the Platinum menu frame and shadow region")
    for y, x, inside in ((13, 13, 1), (13, 25, 0), (14, 25, 1), (25, 25, 1)):
        p.emit(0x3C600000 | y, 0x60630000 | x, 0x809E0498)
        p.call("PtInRgn")
        p.expect(inside, "the one-pixel shadow extends to the bottom and right")
    p.emit(0x807E0498)
    p.call("DisposeRgn")
    for squished, width in ((0, 13), (1, 9)):
        p.emit(0x387E0494, 0x38800000 | squished)
        p.call("GetThemeMenuTitleExtra")
        p.expect(0, "measure the menu title's horizontal padding")
        p.emit(0xA07E0494)
        p.expect(width, "squished and regular Platinum titles use different widths")
    p.call("GetThemeMenuBarShape")
    p.expect(0, "the built-in Platinum menu bar uses the rounded shape")
    p.emit(0x387E049C, 0x38800000)
    p.call("DrawThemeMenuBackground")
    p.expect(0, "paint a Platinum menu background and its frame")
    p.emit(0x3860000D, 0x38800012, 0x38BE0478)
    p.call("GetCPixel")
    p.emit(0xA07E0478)
    p.expect(0, "the menu edge is painted outside the supplied content rectangle")
    p.emit(0x387E049C)
    p.call("EraseRect")
    p.emit(0x387E049C, 0x38800001)
    p.call("DrawThemeTickMark")
    p.expect(0, "draw the active slider tick using the current display")
    for x, expected in ((15, 0), (19, 0xFFFF)):
        p.emit(0x38600000 | x, 0x38800010, 0x38BE0478)
        p.call("GetCPixel")
        p.emit(0xA07E0478)
        p.expect(expected, "the vertical tick paints only its narrow band")
    p.emit(0x38600001, 0x389E049C, 0x38A00001, 0x38C00000)
    p.call("DrawThemeScrollBarDelimiters")
    p.expect(0, "draw the active scroll-bar delimiters")
    for x, y in ((18, 9), (9, 12)):
        p.emit(0x38600000 | x, 0x38800000 | y, 0x38BE0478)
        p.call("GetCPixel")
        p.emit(0xA07E0478)
        p.expect(0, "the scrollbar delimiter sits 15 pixels from the content edge")
    p.emit(0x38600000)
    p.call("GetThemeAccentColors")
    p.expect(-50, "an accent palette copy requires an output slot")
    p.data[0x470:0x478] = struct.pack(">4h", 12, 12, 26, 28)
    p.emit(0x387E0470)
    p.call("EraseRect")
    p.routines.append(
        (
            0x718,
            [
                0x80030000,
                0x90020480,
                0x80030004,
                0x90020484,
                0x90820488,
                0x90A2048C,
                0x4E800020,
            ],
        )
    )
    p.emit(0x387E0470, 0x38800001, 0x38A00000, 0x38C00001, 0x38FE0718, 0x39001234)
    p.call("DrawThemePopupArrow")
    p.expect(0, "draw a three-point down arrow at the supplied origin")
    for offset, value in ((0x480, 0x000C000C), (0x484, 0x000E000F), (0x488, 0x1234)):
        p.emit(0x807E0000 | offset)
        p.expect(
            value,
            "the popup erase callback receives the exact bitmap rectangle and refcon",
        )
    p.emit(0x807E048C)
    p.nonzero("the popup erase callback receives the drawing display's pixel depth")
    for y, channel in ((12, 0), (14, 0xFFFF)):
        p.emit(0x3860000C, 0x38800000 | y, 0x38BE0478)
        p.call("GetCPixel")
        p.emit(0xA07E0478)
        p.expect(
            channel,
            "the arrow mask paints its base and preserves pixels outside the glyph",
        )
    p.emit(0x387E0470, 0x38800004, 0x38A00000, 0x38C00001, 0x38E00000, 0x39000000)
    p.call("DrawThemePopupArrow")
    p.expect(-50, "invalid popup-arrow orientations are rejected")
    p.emit(0x387E0470, 0x38800001)
    p.call("DrawThemeTabPane")
    p.expect(0, "draw a Platinum tab pane in the current port")
    p.emit(0x3860000D, 0x3880000C, 0x38BE0478)
    p.call("GetCPixel")
    p.emit(0xA07E0478)
    p.expect(0, "the active tab pane paints its outer border in black")
    p.emit(0x38600014, 0x38800012, 0x38BE0478)
    p.call("GetCPixel")
    p.emit(0xA07E0478)
    p.nonzero("the tab pane interior is lighter than its active border")
    p.emit(0x387E0470, 0x38800002)
    p.call("DrawThemeTabPane")
    p.expect(-50, "tab panes accept only active or inactive state")
    p.emit(0x807E0450)
    p.call("SetPort")
    for action in (2, 1):
        p.emit(0x807E02C0, 0x38800001, 0x38A00000 | action, 0x38C00000)
        p.call("TransitionWindow")
        p.expect(0, "zoom transition performs the actual window visibility change")
        p.call("FrontWindow")
        if action == 2:
            p.expect(0, "hidden window no longer participates in front-window selection")
        else:
            p.emit(0x809E02C0)
            p.equal("shown window returns to the live visible window list")
    p.emit(0x807E02C0, 0x389E0340)
    p.call("SetWindowProxyFSSpec")
    p.expect(
        -4,
        "unavailable native file proxies return an error for the plain-title fallback",
    )
    p.emit(0x807E02C0, 0x38800000)
    p.call("SetWindowProxyFSSpec")
    p.expect(-50, "file proxies require a file specification")
    p.emit(0x38600000, 0x389E0340)
    p.call("SetWindowProxyFSSpec")
    p.expect(-5600, "file proxies reject invalid windows")
    p.emit(0x3C001234, 0x60005678, 0x901E02C4, 0x807E02C0, 0x389E02C4)
    p.call("GetWindowProxyIcon")
    p.expect(-5600, "plain native title bars report no proxy icon")
    p.emit(0x807E02C4)
    p.expect(0x12345678, "missing proxy icon leaves the caller's output untouched")
    p.emit(0x807E02C0, 0x38800000)
    p.call("SetWindowProxyIcon")
    p.expect(-50, "null proxy icons return paramErr")
    check_picture_controls(p)
    check_label_icon(p)
    check_progress_controls(p)
    p.emit(0x807E02C0)
    p.call("DisposeWindow")
    p.emit(
        0x38600000,
        0x389E0300,
        0x38BE0310,
        0x38C00001,
        0x38E00001,
        0x3900FFFF,
        0x39200000,
        0x39400000,
        0x38000000,
        0x90010038,
    )
    p.call("NewCDialog")
    p.nonzero("create a real dialog for modal event routing")
    p.emit(0x907E04C0, 0x389E04C4)
    p.call("GetModalDialogEventMask")
    p.expect(0, "read the native dialog's default event mask")
    p.emit(0xA07E04C4)
    p.expect(0x17F, "new dialogs use the System 9 default modal event mask")
    p.emit(0x807E04C0, 0x38801000)
    p.call("SetModalDialogEventMask")
    p.expect(0, "set an actual per-dialog operating-system event mask")
    p.emit(0x807E04C0, 0x389E04C4)
    p.call("GetModalDialogEventMask")
    p.expect(0, "read the changed modal event mask")
    p.emit(0xA07E04C4)
    p.expect(0x1000, "modal masks are stored independently of dialog option bits")
    p.emit(0x3860000C, 0x38800001)
    p.call("PostEvent")
    p.expect(0, "post an event excluded from the default modal mask")
    p.routines.append(
        (
            0x720,
            [0xA0C40000, 0x90C204C8, 0x38C00001, 0xB0C50000, 0x38600001, 0x4E800020],
        )
    )
    p.emit(0x387E0720, 0x389E04CC)
    p.call("ModalDialog")
    p.emit(0x807E04C8)
    p.expect(12, "ModalDialog really delivers the added OS event to the guest filter")
    p.emit(0x38007777, 0xB01E04D0, 0x807E04C0, 0x389E04D0, 0x38A00000, 0x38C00000)
    p.call("GetDialogTimeout")
    p.expect(-5640, "a new dialog has no timeout")
    p.emit(0xA07E04D0)
    p.expect(0x7777, "an absent timeout preserves the caller's result storage")
    p.emit(0x807E04C0, 0x38800007, 0x38A00005)
    p.call("SetDialogTimeout")
    p.expect(0, "register the inactivity timeout through MacDialogsLib")
    p.emit(0x807E04C0, 0x389E04D0, 0x38BE04D4, 0x38DE04D8)
    p.call("GetDialogTimeout")
    p.expect(0, "the CarbonLib re-export reads the same registered timeout")
    p.emit(0xA07E04D0)
    p.expect(7, "the configured timeout item is a guest-order short")
    p.emit(0x807E04D4)
    p.expect(5, "the configured duration is a guest-order long")
    p.emit(0x807E04D8)
    p._check(0x28030005, 0x40810000, "the remaining time is bounded by the duration")
    p.emit(0x807E04C0, 0x38800007, 0x38A00001)
    p.call("SetDialogTimeout")
    p.expect(0, "replace the timeout with a one-second timer")
    p.emit(0x38600000, 0x389E04CC)
    p.call("ModalDialog")
    p.emit(0xA07E04CC)
    p.expect(7, "the native modal loop returns the timeout item while idle")
    p.emit(0x807E04C0, 0x38800000, 0x38A00000)
    p.call("SetDialogTimeout")
    p.expect(0, "zero seconds removes the timeout")
    p.emit(0x807E04C0, 0x38800000, 0x38A00000, 0x38C00000)
    p.call("GetDialogTimeout")
    p.expect(-5640, "a removed timeout is absent even with no outputs requested")
    p.emit(0x807E04C0, 0x38800000, 0x38A00000)
    p.call("SetDialogTimeout")
    p.expect(-5604, "removing a missing timer returns the window property error")
    p.emit(0x38600000, 0x38800001, 0x38A00001)
    p.call("SetDialogTimeout")
    p.expect(-5600, "a timeout requires a valid window")
    p.emit(0x807E04C0)
    p.call("DisposeDialog")
    p.data[0x340:0x360] = bytes([0xA5]) * 32
    p.emit(0x387E0340)
    p.call("PPCOpenSync")
    p.expect(-50, "unavailable PPC Toolbox rejects opening an interprocess port")
    p.emit(0xA87E0350)
    p.expect(-50, "synchronous PPC open publishes the same ioResult")
    for offset in (0x340, 0x35C):
        p.emit(0x807E0000 | offset)
        p.expect(0xA5A5A5A5, "failed PPC open preserves other parameter-block fields")
    p.emit(0x387E0340)
    p.call("PPCCloseSync")
    p.expect(-50, "unavailable PPC Toolbox rejects closing an interprocess port")
    p.emit(0xA87E0350)
    p.expect(-50, "synchronous PPC close publishes the same ioResult")
    p.call("LMGetEventQueue")
    p.emit(0x907E0400)
    p.call("GetEvQHdr")
    p.emit(0x809E0400)
    p.equal("low-memory and OS event queue accessors share the actual queue")
    p.emit(0x8060103C, 0x38803FAE, 0x38A00075, 0x38DE0408, 0x38FE0410, 0x391E0414)
    p.call("CallUniversalProc")
    p.expect(-4, "private extended process accounting returns a real unsupported status")
    for offset in (0x410, 0x414):
        p.emit(0x807E0000 | offset)
        p.expect(0xA5A5A5A5, "unavailable extended accounting preserves caller outputs")
    p.data[0x410:0x418] = bytes([0xA5]) * 8
    p.emit(0x38608000, 0x38800001, 0x38A00002, 0x38C00010, 0x38E00000, 0x391E0420)
    p.call("GetIconRefFromFolder")
    p.expect(0, "root folder obtains actual hard-disk icon artwork")
    p.emit(0x807E0420)
    p.call("ReleaseIconRef")
    p.expect(0, "release ownership returned by folder icon lookup")
    p.emit(0x38600000, 0x389E0424)
    p.call("SysZoneFloatSizes")
    p.expect(0, "system heap accounting accepts an optional high-memory output")
    p.emit(0x807E0424)
    p.expect(0, "there is no separate temporary-memory partition")
    p.emit(0x3860ABEB, 0x38800001)
    p.call("NGetTrapAddress")
    p.emit(0x388003D8, 0x38A0FFF6, 0x38DE0280)
    p.call("CallUniversalProc")
    p.expect(
        0,
        "Display Manager leaves desktop clicks to Finder when no animation component is installed",
    )
    for kind, result in ((0, 0), (0x776F706E, -4)):
        p.emit(0x3C600000 | (kind >> 16), 0x60630000 | (kind & 0xFFFF))
        p.call("PlayThemeSound")
        p.expect(
            result,
            "native theme sound provider distinguishes silence from unavailable sound assets",
        )
    p.call("VM_GetHoldableBytes")
    p.nonzero("fixed guest RAM is available to HoldMemory without enabling guest paging")
    p.emit(0x3C607468, 0x60636473, 0x389E04A0)
    p.call("Gestalt")
    p.expect(0, "native Thread Manager capabilities are discoverable")
    p.emit(0x807E04A0)
    p.expect(7, "report only present library and implemented specific stack matching")
    p.emit(0x387E04A0)
    p.call("GetThreadCurrentTaskRef")
    p.expect(0, "obtain the actual native application task reference")
    p.emit(0x807E04A0)
    p.nonzero("native task reference is initialized rather than a fabricated null context")
    p.emit(0x807E04A0, 0x38800002, 0x38BE04A4)
    p.call("GetThreadStateGivenTaskRef")
    p.expect(0, "task reference identifies the actual application thread")
    p.emit(0xA07E04A4)
    p.expect(2, "the application thread is running in its own task")
    check_object_accessors(p)
    for enabled in (0, 1):
        p.emit(0x38600000 | enabled)
        p.call("HMSetBalloons")
        p.expect(-855, "unavailable Balloon Help reports its actual initialization error")
        p.call("HMGetBalloons")
        p.expect(0, "unavailable Balloon Help remains disabled")
    check_resource_overrides(p)
    check_date_formatting(p)
    check_desktop_drag(p)
    check_device_parameter_blocks(p)
    check_file_icons(p, destination)
    for selector, offset in ((0x72616D20, 0xB50), (0x7272616D, 0xB54)):
        p.emit(0x3C600000 | (selector >> 16), 0x60630000 | (selector & 0xFFFF), 0x389E0000 | offset)
        p.call("Gestalt")
        p.expect(0, "public and Finder-private physical RAM queries are available")
    p.emit(0x807E0B50)
    p.nonzero("physical RAM reflects the configured guest allocation")
    p.emit(0x809E0B54)
    p.equal("Finder's built-in RAM value matches the actual public RAM capacity")
    # OSDispatch 0x74 reports real allocatable temporary memory. Holding a
    # near-maximum temporary handle must reduce the reported largest block.
    p.emit(0x8060103C, 0x388000BE, 0x38A00074)
    p.call("CallUniversalProc")
    p._check(
        0x28032000,
        0x41810000,
        "the largest-unused-block query reports usable temporary memory",
    )
    p.emit(0x907E0B40, 0x3863F000, 0x389E0B4C)
    p.call("TempNewHandle")
    p.nonzero("the reported temporary capacity is actually allocatable")
    p.emit(0x907E0B44, 0xA87E0B4C)
    p.expect(
        0,
        "allocating within reported capacity succeeds without a fabricated memory count",
    )
    p.emit(0x8060103C, 0x388000BE, 0x38A00074)
    p.call("CallUniversalProc")
    p.emit(0x809E0B40)
    p._check(
        0x7C032040,
        0x41800000,
        "the largest-unused-block query reflects live heap allocations",
    )
    p.emit(0x807E0B44)
    p.call("DisposeHandle")
    message = b"PPC Finder services: passed"
    p.data[0x240 : 0x241 + len(message)] = bytes([len(message)]) + message
    p.emit(0x387E0240)
    p.call("DebugStr")
    # Original one-bit colour icon, with a sparse palette entry at index one.
    label_icon = bytearray(82)
    struct.pack_into(">H4h", label_icon, 4, 0x8002, 0, 0, 2, 8)
    struct.pack_into(">II", label_icon, 18, 72 << 16, 72 << 16)
    struct.pack_into(">HHH", label_icon, 32, 1, 1, 1)
    struct.pack_into(">H4h", label_icon, 54, 2, 0, 0, 2, 8)
    label_icon += bytes.fromhex("aa00aa00")
    label_icon += struct.pack(">IHHHHHH", 1, 0, 0, 1, 0xFFFF, 0, 0)
    label_icon += bytes.fromhex("ff00ff00")
    picture = struct.pack(">H4h", 34, 0, 0, 16, 16) + bytes.fromhex("001102ff001affff000000000031000000000010001000ff")
    p.finish(
        destination,
        "PPC Finder Services Probe",
        extra_resources=[(b"PICT", 1234, picture), (b"cicn", 1235, bytes(label_icon))],
    )


def check_desktop_drag(p):
    # Finish a desktop drag through the live event vector, without host input.
    # Callbacks record the real messages, registered desktop owner and drag reference.
    # Query attributes in every tracking callback, including enter-handler.
    start = len(p.code)
    p.call("GetDragAttributes")
    attributes_call = p.code[start:]
    del p.code[start:]
    p.routines.extend(
        [
            (
                0xD00,
                [
                    0x38000002,
                    0xB0040000,
                    0x38000140,
                    0xB004000A,
                    0x380001E0,
                    0xB004000C,
                    0x38000080,
                    0xB004000E,
                    0x38600001,
                    0x4E800020,
                ],
            ),
            (
                0xD08,
                [
                    0x7C0802A6,
                    0x90010008,
                    0x93C1FFFC,
                    0x9421FFC0,
                    0x7C5E1378,
                    0x80E50000,
                    0x39000001,
                    0x7D081830,
                    0x7CE74378,
                    0x90E50000,
                    0x90850004,
                    0x90C50008,
                    0x7CC33378,
                    0x389E0D98,
                    *attributes_call,
                    0x807E0D98,
                    0x809E0DA0,
                    0x7C632038,
                    0x907E0DA0,
                    0x38210040,
                    0x83C1FFFC,
                    0x80010008,
                    0x7C0803A6,
                    0x38600000,
                    0x4E800020,
                ],
            ),
            (0xD10, [0x38000001, 0x9004000C, 0x90640010, 0x90A40014, 0x38600000, 0x4E800020]),
        ]
    )
    p.data[0xD60:0xD70] = struct.pack(">HIIhhH", 1, 0, 0, 300, 450, 0)
    struct.pack_into(">I", p.data, 0xDA0, 6)
    p.emit(0x387E0D20)
    p.call("NewDrag")
    p.expect(0, "create a real desktop drag")
    for offset, info, output in ((0xD00, 0x3F90, 0xD44), (0xD08, 0x3FA0, 0xD48), (0xD10, 0xFE0, 0xD4C)):
        p.emit(0x387E0000 | offset, 0x38800000 | info, 0x38A00001)
        p.call("NewRoutineDescriptor")
        p.nonzero("create a native callback descriptor for the desktop drag")
        p.emit(0x907E0000 | output)
    for name, offset in (("InstallTrackingHandler", 0xD48), ("InstallReceiveHandler", 0xD4C)):
        p.emit(0x807E0000 | offset, 0x38800000, 0x38BE0D80)
        p.call(name)
        p.expect(0, "register an application-wide desktop handler")
    p.emit(0x80600F80, 0x907E0D40, 0x807E0D44, 0x90600F80)
    p.emit(0x807E0D20, 0x389E0D60, 0x38A00000)
    p.call("TrackDrag")
    p.expect(0, "a desktop drop is delivered to its real receive handler")
    p.emit(0x807E0D40, 0x90600F80)
    for offset, value in ((0xD80, 0x3E), (0xD8C, 1)):
        p.emit(0x807E0000 | offset)
        p.expect(value, "desktop tracking preserves the complete callback lifecycle")
    for offset in (0xD84, 0xD90):
        p.emit(0x807E0000 | offset, 0x809E02A0)
        p.equal("desktop callbacks receive the actual registered desktop layer owner")
    for offset in (0xD88, 0xD94):
        p.emit(0x807E0000 | offset, 0x809E0D20)
        p.equal("desktop callbacks receive the actual drag reference")
    p.emit(0x807E0D20, 0x389E0D98)
    p.call("GetDragAttributes")
    p.expect(0, "read completed desktop drag attributes")
    p.emit(0x807E0D98)
    p.expect(6, "the desktop remains inside the sender application and sender surface")
    p.emit(0x807E0DA0)
    p.expect(6, "every tracking callback sees the sender attributes, including enter-handler")
    p.emit(0x807E0D20, 0x389E0D98, 0x38BE0D9C)
    p.call("GetDragMouse")
    p.expect(0, "read the final desktop drop point")
    p.emit(0x807E0D98)
    p.expect((320 << 16) | 480, "the receiver sees the final mouse position")
    for name, offset in (("RemoveTrackingHandler", 0xD48), ("RemoveReceiveHandler", 0xD4C)):
        p.emit(0x807E0000 | offset, 0x38800000)
        p.call(name)
        p.expect(0, "remove only the fixture's desktop handler")
    p.emit(0x807E0D20)
    p.call("DisposeDrag")
    p.expect(0, "dispose the completed desktop drag")
    for offset in (0xD44, 0xD48, 0xD4C):
        p.emit(0x807E0000 | offset)
        p.call("DisposeRoutineDescriptor")


def check_resource_overrides(p):
    p.call("CurResFile")
    p.emit(0xB07E0B64)
    p.call("GetResFileAttrs")
    p.emit(0x907E0B68, 0xA87E0B64)
    p.call("GetMap")
    p.nonzero("resolve the real current resource map")
    p.emit(0x907E0B6C, 0xA87E0B64, 0x809E0B68, 0x6084001E)
    p.call("SetResFileAttrs")
    p.emit(0x807E0B6C)
    p.call("GetOverrideAttributes")
    p.expect(0x1E, "read all four real in-memory resource-map override bits")
    p.emit(0x3860FB2E)
    p.call("GetResFileAttrs")
    p.emit(0x807E0B6C)
    p.call("GetOverrideAttributes")
    p.expect(0x1E, "a valid map query still returns its override attributes after an error")
    p.call("ResError")
    p.expect(-193, "the private override query preserves existing ResErr on success")
    p.emit(0x38600000)
    p.call("GetOverrideAttributes")
    p.expect(0, "a missing map publishes no override flags")
    p.call("ResError")
    p.expect(-109, "a missing map reports the actual nil-handle error")
    p.emit(0xA87E0B64, 0x809E0B68)
    p.call("SetResFileAttrs")


def check_date_formatting(p):
    # Compare public date/time entry points with their resource-handle variants.
    # The timestamp has its high bit set, as in Finder's actual Get Info call.
    for plain, explicit, form, resource in (
        ("IUDateString", "IUDatePString", 0, 0),
        ("IUDateString", "IUDatePString", 1, 1),
        ("IUDateString", "IUDatePString", 2, 1),
        ("IUTimeString", "IUTimePString", 1, 0),
    ):
        p.emit(0x38600000 | resource)
        p.call("IUGetIntl")
        p.nonzero("date formatting uses the real System international resource")
        p.emit(0x907E0B64)
        p.emit(0x3C60E702, 0x60637E13, 0x38800000 | form, 0x38BE0E00)
        p.call(plain)
        p.emit(0x887E0E00)
        p.nonzero("Get Info date and time formatting produces a Pascal string")
        for handle in (0, 1):
            p.emit(0x3C60E702, 0x60637E13, 0x38800000 | form, 0x38BE0F00)
            p.emit(0x80DE0B64 if handle else 0x38C00000)
            p.call(explicit)
            p.emit(0x387E0E00, 0x389E0F00, 0x38A00001, 0x38C00001)
            p.call("EqualString")
            p.expect(1, "explicit and default locale handles preserve date/time formatting")


def check_device_parameter_blocks(p):
    # A genuine 68K OS patch records D1/A0, updates the PB and returns an error.
    p.data[0xBC0:0xBD6] = bytes.fromhex("31410006217c12345678001c317cffd6001070d64e75")
    p.data[0xB7F] = p.data[0xBB2] = 0xA5
    p.data[0xB98:0xB9A] = struct.pack(">h", 1)  # Invalid positive driver reference.
    for name, trap in (("PBControlSync", 0xA004), ("PBStatusSync", 0xA005)):
        p.emit(0x38600000)
        p.call(name)
        p.expect(-50, "synchronous device calls reject a missing parameter block")
        p.emit(0x3C60FFFF, 0x6063FFF0)
        p.call(name)
        p.expect(-50, "synchronous device calls reject a parameter block outside guest RAM")
        p.emit(0x387E0B80)
        p.call(name)
        p.expect(-21, "the native Device Manager rejects an invalid driver reference")
        p.emit(0xA87E0B90)
        p.expect(-21, "native synchronous device errors update ioResult")
        p.emit(0x38600000 | (trap & 0xFFFF), 0x38800000)
        p.call("NGetTrapAddress")
        p.emit(0x907E0B60, 0x387E0BC0, 0x38800000 | (trap & 0xFFFF), 0x38A00000)
        p.call("NSetTrapAddress")
        p.emit(0x387E0B80)
        p.call(name)
        p.expect(-42, "synchronous device imports invoke the real installed guest OS patch")
        p.emit(0xA07E0B86)
        p.expect(trap, "the installed patch receives the synchronous Control or Status trap word")
        p.emit(0x807E0B9C)
        p.expect(0x12345678, "the installed patch receives the actual parameter-block pointer in A0")
        p.emit(0xA87E0B90)
        p.expect(-42, "the guest driver's ioResult is preserved")
        p.emit(0x807E0B60, 0x38800000 | (trap & 0xFFFF), 0x38A00000)
        p.call("NSetTrapAddress")
        p.emit(0x387E0B80)
        p.call(name)
        p.expect(-21, "restoring the native OS vector restores the actual driver fallback")
        for offset in (0xB7F, 0xBB2):
            p.emit(0x887E0000 | offset)
            p.expect(0xA5, "device parameter-block calls preserve adjacent guest storage")


def check_picture_controls(p):
    # A real resource supplies the picture; drawing must reach the current port.
    p.data[0xB10:0xB14] = bytes([255]) * 4
    p.emit(0x386004D2)
    p.call("GetPicture")
    p.nonzero("load the original regression PICT resource")
    p.emit(0x907E0B00)
    for definition in (304, 305):
        p.emit(
            0x807E02C0,
            0x389E0460,
            0x38BE0310,
            0x38C00001,
            0x38E004D2,
            0x39000000,
            0x39200000,
            0x39400000 | definition,
            0x38000000,
            0x90010038,
        )
        p.call("NewControl")
        p.nonzero("create the actual CDEF 19 picture control used by Finder's About window")
        p.emit(0x907E0B04)
        p.call("GetCtlValue")
        p.expect(0, "picture initialization consumes the resource ID and resets the value")
        p.emit(
            0x807E0B04,
            0x38800000,
            0x3CA07069,
            0x60A56368,
            0x38C00004,
            0x38FE0B08,
            0x391E0B0C,
        )
        p.call("GetControlData")
        p.expect(0, "read the real borrowed picture handle through pich")
        p.emit(0x807E0B08, 0x809E0B00)
        p.equal("picture controls resolve their creation resource map")
        p.emit(0x807E0B0C)
        p.expect(4, "the picture handle tag has a four-byte guest ABI")
        p.emit(0x807E02C0)
        p.call("SetPort")
        p.emit(0x807E0B04)
        p.call("Draw1Control")
        p.emit(0x38600019, 0x38800019, 0x38BE0B18)
        p.call("GetCPixel")
        p.emit(0xA07E0B18)
        p.expect(0xFFFF, "picture drawing paints the fixture's red pixels into the window")
        for offset in (0xB1A, 0xB1C):
            p.emit(0xA07E0000 | offset)
            p.expect(0, "PICT drawing preserves the actual green and blue components")
        p.emit(0x807E0B04, 0x3C800019, 0x60840019)
        p.call("TestControl")
        p.expect(6, "pictures return their standard picture part code")
        if definition == 305:
            p.emit(0x807E0B04, 0x3C800019, 0x60840019, 0x38A00000)
            p.call("TrackControl")
            p.expect(
                6,
                "non-tracking pictures return immediately without waiting for a mouse release",
            )
        p.emit(0x807E0B04, 0x38800000, 0x3CA07069, 0x60A56368, 0x38C00003, 0x38FE0B00)
        p.call("SetControlData")
        p.expect(-50, "short picture handle writes are rejected")
        for offset, result in ((0xB10, -50), (0xB14, 0), (0xB00, 0)):
            p.emit(
                0x807E0B04,
                0x38800000,
                0x3CA07069,
                0x60A56368,
                0x38C00004,
                0x38FE0000 | offset,
            )
            p.call("SetControlData")
            p.expect(
                result,
                "picture handle writes validate addresses and allow clearing or replacing borrowed content",
            )
            p.emit(
                0x807E0B04,
                0x38800000,
                0x3CA07069,
                0x60A56368,
                0x38C00004,
                0x38FE0B08,
                0x391E0B0C,
            )
            p.call("GetControlData")
            p.expect(0, "query picture content after a write")
            p.emit(0x807E0B08)
            if offset == 0xB14:
                p.expect(0, "clearing picture content returns a null handle")
            else:
                p.emit(0x809E0B00)
                p.equal("failed writes preserve, and valid writes restore, the original picture")
        p.emit(0x807E0B04)
        p.call("DisposeControl")
        p.emit(0x807E0B00)
        p.call("GetHandleSize")
        p.expect(34, "disposing a control leaves its resource-owned picture intact")


def check_label_icon(p):
    p.emit(0x807E02C0)
    p.call("SetPort")
    p.emit(0x387E0460)
    p.call("EraseRect")
    p.emit(0x386004D3)
    p.call("GetCIcon")
    p.nonzero("load an original one-bit colour icon with a custom palette")
    p.emit(0x907E0B68, 0x7C641B78, 0x387E0460)
    p.call("PlotCIcon")
    for x, expected in ((20, (0xFFFF, 0, 0)), (22, (0xFFFF, 0xFFFF, 0xFFFF))):
        p.emit(0x38600000 | x, 0x38800019, 0x38BE0B70)
        p.call("GetCPixel")
        for channel, value in enumerate(expected):
            p.emit(0xA07E0000 | (0xB70 + channel * 2))
            p.expect(value, "one-bit colour icons retain their palette and transparent mask pixels")
    p.emit(0x807E0B68)
    p.call("DisposeCIcon")


def check_progress_controls(p):
    for definition in (80, 81):
        p.emit(
            0x807E02C0,
            0x389E0460,
            0x38BE0310,
            0x38C00001,
            0x38E00032,
            0x39000000,
            0x39200064,
            0x39400000 | definition,
            0x38000000,
            0x90010038,
        )
        p.call("NewControl")
        p.nonzero("create a legacy CDEF 5 progress or relevance bar")
        p.emit(0x907E0B04)
        p.call("GetCtlValue")
        p.expect(50, "the progress indicator preserves its initial value")
        p.emit(0x807E0B04, 0x3C800019, 0x60840019)
        p.call("TestControl")
        p.expect(0, "progress indicators do not return a clickable part")
        p.emit(0x807E0B04, 0x389E0B0C)
        p.call("GetControlFeatures")
        p.expect(0, "query the CDEF 5 capabilities")
        p.emit(0x807E0B0C)
        p.expect(
            0x48 if definition == 80 else 0,
            "only progress bars support idling and data access",
        )
        if definition == 80:
            p.emit(0x38000001, 0x981E0B10)
            p.emit(0x807E0B04, 0x38800000, 0x3CA0696E, 0x60A56465, 0x38C00001, 0x38FE0B10)
            p.call("SetControlData")
            p.expect(0, "enable indeterminate progress")
            p.emit(
                0x807E0B04,
                0x38800000,
                0x3CA0696E,
                0x60A56465,
                0x38C00001,
                0x38FE0B11,
                0x391E0B0C,
            )
            p.call("GetControlData")
            p.expect(0, "retrieve the stored indeterminate flag")
            p.emit(0x887E0B11)
            p.expect(1, "the indeterminate flag uses a one-byte guest Boolean")
        p.emit(0x807E0B04)
        p.call("DisposeControl")

    p.emit(0x807E02C0, 0x389E0460)
    for register, value in enumerate((65000, -100000, 100000), 5):
        p.emit(
            0x3C000000 | (register << 21) | ((value >> 16) & 0xFFFF),
            0x60000000 | (register << 21) | (register << 16) | (value & 0xFFFF),
        )
    p.emit(0x39000001, 0x393E0B04)
    p.call("CreateProgressBarControl")
    p.expect(0, "create a Carbon progress bar without a guest CarbonLib installed")
    for operation, value in (
        ("GetControl32BitValue", 65000),
        ("GetControl32BitMinimum", -100000),
        ("GetControl32BitMaximum", 100000),
    ):
        p.emit(0x807E0B04)
        p.call(operation)
        p.expect(
            value,
            "the native Carbon constructor preserves the full signed 32-bit range",
        )
    p.emit(0x807E0B04)
    p.call("DisposeControl")


def check_object_accessors(p):
    # The real guest callback records its complete seven-word ABI, then returns
    # its refcon. Descriptor storage belongs to the caller, not a host pointer.
    p.routines.append(
        (
            0x700,
            [
                0x90620780,
                0x90820784,
                0x90A20788,
                0x90C2078C,
                0x90E20790,
                0x91020794,
                0x91220798,
                0x3C00746F,
                0x60006B6E,
                0x90080000,
                0x38000000,
                0x90080004,
                0x7D234B78,
                0x4E800020,
            ],
        )
    )
    p.routines.append((0x710, [0x8082079C, 0x38840001, 0x9082079C, 0x3860FFD6, 0x4E800020]))
    p.call("AEObjectInit")
    p.expect(0, "initialize real object accessor tables")
    p.emit(
        0x3C60646F,
        0x60636375,
        0x3C806E75,
        0x60846C6C,
        0x38BE0700,
        0x38C0FF85,
        0x38E00000,
    )
    p.call("AEInstallObjectAccessor")
    p.expect(0, "install a genuine PPC object accessor with signed refcon")
    p.call("AEObjectInit")
    p.expect(0, "repeated initialization preserves installed accessors")
    p.emit(
        0x3C60646F,
        0x60636375,
        0x3C806E75,
        0x60846C6C,
        0x38BE0680,
        0x38DE0688,
        0x38E00000,
    )
    p.call("AEGetObjectAccessor")
    p.expect(0, "look up an exact application accessor")
    p.emit(0x807E0680, 0x389E0700)
    p.equal("accessor result is a four-byte guest procedure pointer")
    p.emit(0x807E0688)
    p.expect(-123, "accessor reference constant has guest byte order")
    p.emit(0x807E0684)
    p.expect(0xA5A5A5A5, "accessor lookup preserves adjacent output storage")
    p.emit(
        0x3C60646F,
        0x60636375,
        0x389E0600,
        0x3CA0636F,
        0x60A56E74,
        0x3CC06E61,
        0x60C66D65,
        0x38FE0610,
        0x391E0620,
    )
    p.call("AECallObjectAccessor")
    p.expect(-123, "propagate accessor errors through the mixed-mode callback")
    for offset, value in (
        (0x780, 0x646F6375),
        (0x788, 0x636F6E74),
        (0x78C, 0x6E616D65),
        (0x798, -123),
    ):
        p.emit(0x807E0000 | offset)
        p.expect(value, "object accessor receives the real class, form and refcon")
    for offset, target in ((0x784, 0x600), (0x790, 0x610), (0x794, 0x620)):
        p.emit(0x807E0000 | offset, 0x389E0000 | target)
        p.equal("object accessor receives actual guest descriptor pointers")
    p.emit(
        0x3C60646F,
        0x60636375,
        0x3C806E75,
        0x60846C6C,
        0x38BE0700,
        0x38C00000,
        0x38E00000,
    )
    p.call("AEInstallObjectAccessor")
    p.expect(0, "replace an accessor without duplicating its key")
    p.emit(0x38600000, 0x38800000, 0x38A00001, 0x38DE0640)
    p.call("AECreateList")
    p.expect(0, "create an actual object-specifier record payload")
    for keyword, offset in ((0x77616E74, 0x660), (0x666F726D, 0x664)):
        p.emit(
            0x387E0640,
            0x3C800000 | (keyword >> 16),
            0x60840000 | (keyword & 0xFFFF),
            0x3CA07479,
            0x60A57065,
            0x38DE0000 | offset,
            0x38E00004,
        )
        p.call("AEPutKeyPtr")
        p.expect(0, "populate the real wanted class and key form")
    for keyword, offset in ((0x66726F6D, 0x600), (0x73656C64, 0x610)):
        p.emit(
            0x387E0640,
            0x3C800000 | (keyword >> 16),
            0x60840000 | (keyword & 0xFFFF),
            0x38BE0000 | offset,
        )
        p.call("AEPutKeyDesc")
        p.expect(0, "populate object specifier container and selector descriptors")
    p.emit(0x3C006F62, 0x60006A20, 0x901E0640, 0x387E0640, 0x38800000, 0x38BE0620)
    p.call("AEResolve")
    p.expect(0, "resolve a real object specifier through its registered guest accessor")
    p.emit(0x807E0620)
    p.expect(0x746F6B6E, "resolution returns the application's actual token")
    p.emit(0x807E0640)
    p.expect(0x6F626A20, "resolution preserves the input object-specifier type")
    p.emit(0x387E0640)
    p.call("AEDisposeDesc")
    p.expect(0, "release object specifier fields and record storage")
    p.data[0x660:0x668] = b"docuname"
    p.emit(
        0x38600000,
        0x38800000,
        0x38BE0710,
        0x38C00000,
        0x38E00000,
        0x39000000,
        0x39200000,
    )
    p.call("AESetObjectCallbacks")
    p.expect(0, "install real token disposal callback")
    p.emit(
        0x38600000,
        0x38800000,
        0x38A00000,
        0x38C00000,
        0x38E00000,
        0x39000000,
        0x39200000,
    )
    p.call("AESetObjectCallbacks")
    p.expect(0, "nil callback arguments preserve previously installed functions")
    p.emit(0x387E0620)
    p.call("AEDisposeToken")
    p.expect(-42, "propagate token disposal callback status")
    p.emit(0x807E079C)
    p.expect(1, "token disposal invokes the registered callback once")
    p.emit(0x3C60646F, 0x60636375, 0x3C806E75, 0x60846C6C, 0x38A00000, 0x38C00000)
    p.call("AERemoveObjectAccessor")
    p.expect(0, "remove the application's real accessor record")
    p.emit(
        0x3C60646F,
        0x60636375,
        0x3C806E75,
        0x60846C6C,
        0x38BE0680,
        0x38DE0688,
        0x38E00000,
    )
    p.call("AEGetObjectAccessor")
    p.expect(-1723, "removed object accessor reports accessor-not-found")
    p.data[0x600:0x608] = b"null" + bytes(4)
    p.data[0x610:0x618] = b"null" + bytes(4)
    p.data[0x684:0x688] = bytes([0xA5]) * 4


def check_file_icons(p, destination):
    from make_ppc_probe import write_fragment

    artwork = bytes((i * 37 + 19) & 255 for i in range(128)) + bytes([255]) * 128
    for name, label in (("Icon Fixture A", 3), ("Icon Fixture B", 6)):
        write_fragment(
            destination,
            name,
            b"icon fixture data",
            0,
            extra_resources=[(b"ICN#", -16455, artwork)],
            include_cfrg=False,
        )
        sidecar = destination / ".AppleDouble" / name
        data = bytearray(sidecar.read_bytes())
        finder_offset = struct.unpack_from(">I", data, 42)[0]
        struct.pack_into(">H", data, finder_offset + 8, 0x400 | (label << 1))
        sidecar.write_bytes(data)
    p.data[0x900:0x904] = struct.pack(">I", 60)
    p.emit(0x381E0980, 0x901E0938, 0x387E0940)
    p.call("GetCurrentProcess")
    p.expect(0, "obtain the actual launching process")
    p.emit(0x387E0940, 0x389E0900)
    p.call("GetProcessInformation")
    p.expect(0, "obtain the application's real parent directory")
    p.emit(0x38600000, 0xA89E0980, 0x80BE0982)
    p.call("HSetVol")
    p.expect(0, "select the fixture directory through the File Manager")
    for index, (name, label, offset) in enumerate((("Icon Fixture A", 3, 0xA20), ("Icon Fixture B", 6, 0xA40))):
        encoded = name.encode()
        p.data[offset : offset + len(encoded) + 1] = bytes([len(encoded)]) + encoded
        p.emit(0x38600000, 0x38800000, 0x38BE0000 | offset, 0x38DE09D0)
        p.call("FSMakeFSSpec")
        p.expect(0, "resolve the genuine resource-fork fixture file")
        p.emit(0x387E09D0, 0x389E0A88 + index * 4, 0x38BE0A81 + index * 16)
        p.call("GetIconRefFromFile")
        p.expect(0, "load the actual custom icon resource from a file")
        p.emit(0xA07E0A81 + index * 16)
        p.expect(label, "return the file label independently of icon identity")
    for offset in (0xA80, 0xA83, 0xA90, 0xA93):
        p.emit(0x887E0000 | offset)
        p.expect(0xA5, "unaligned label output preserves adjacent bytes")
    for enabled in (0, 1):
        p.emit(0x38600000 | enabled)
        p.call("SetDeepIconsEnabled")
        p.expect(0, "change the deep icon setting")
        p.emit(0x387E0AC0)
        p.call("GetDeepIconsEnabled")
        p.expect(0, "read the current deep icon setting")
        p.emit(0x887E0AC0)
        p.expect(enabled, "deep icon settings round trip")
    p.emit(0x887E0AC1)
    p.expect(0xA5, "the deep icon setting is a single guest byte")
    p.emit(0xA87E09D0, 0x38800000)
    p.call("SetCustomIconsEnabled")
    p.expect(0, "disable custom artwork on the selected mounted volume")
    p.emit(0xA87E09D0, 0x389E0AC0)
    p.call("GetCustomIconsEnabled")
    p.expect(0, "query the selected mounted volume's icon policy")
    p.emit(0x887E0AC0)
    p.expect(0, "the volume reports that custom icons are disabled")
    p.emit(0x387E09D0, 0x389E0AA8, 0x38BE0AAC)
    p.call("GetIconRefFromFile")
    p.expect(0, "disabled custom artwork still resolves a file icon")
    p.emit(0x807E0AA8, 0x809E0A8C)
    p._check(0x7C032000, 0x40820000, "the disabled policy chooses a non-custom icon instead")
    p.emit(0xA07E0AAC)
    p.expect(6, "the file's Finder label survives disabling custom icons")
    p.emit(0x807E0AA8)
    p.call("ReleaseIconRef")
    p.expect(0, "release the fallback icon ownership")
    p.emit(0xA87E09D0, 0x38800001)
    p.call("SetCustomIconsEnabled")
    p.expect(0, "restore custom icon selection on the selected volume")
    p.emit(0xA87E09D0, 0x389E0AC0)
    p.call("GetCustomIconsEnabled")
    p.expect(0, "read the restored volume policy")
    p.emit(0x887E0AC0)
    p.expect(1, "the volume reports that custom icons are enabled again")
    p.emit(0x38608000, 0x389E0AC0)
    p.call("GetCustomIconsEnabled")
    p.expect(-35, "custom icon policy requires a mounted volume")
    p.emit(0x887E0AC0)
    p.expect(1, "a missing volume leaves the caller's setting untouched")
    p.emit(0x807E0A88, 0x389E0A98)
    p.call("GetIconRefOwners")
    p.expect(0, "query actual file-icon ownership")
    p.emit(0xA07E0A98)
    p.expect(1, "each source file retains its own caller reference")
    p.emit(0x807E0A88, 0x38800001, 0x38BE0AA0)
    p.call("IconRefToIconFamily")
    p.expect(0, "copy the retained family after its resource file has closed")
    p.emit(0x807E0AA0)
    p.call("GetHandleSize")
    p.expect(272, "copied family contains the complete custom ICN# resource")
    p.emit(0x38600001)
    p.call("NewHandle")
    p.nonzero("create caller-owned storage for the icon element")
    p.emit(0x907E0AA4, 0x807E0AA0, 0x3C804943, 0x60844E23, 0x80BE0AA4)
    p.call("GetIconFamilyData")
    p.expect(0, "extract the actual custom icon data")
    p.emit(0x807E0AA4, 0x80630000, 0x80630000)
    p.expect(0x13385D82, "resource-fork artwork bytes survive map closure and copying")
    check_icon_file_registration(p, destination)
    p.call("CurResFile")
    p.emit(0x907E0AB4, 0x38600000, 0x38800000, 0x38BE0A20, 0x38DE09D0)
    p.call("FSMakeFSSpec")
    p.expect(0, "resolve the file whose artwork will change")
    p.emit(0x387E09D0, 0x38800003)
    p.call("FSpOpenResFile")
    p.emit(0x907E0AB0)
    p.call("UseResFile")
    p.emit(0x3C604943, 0x60634E23, 0x3880BFB9)
    p.call("Get1Resource")
    p.nonzero("load the actual writable custom icon resource")
    p.emit(0x907E0AB8, 0x80830000, 0x3CA02468, 0x60A5ACE0, 0x90A40000)
    p.call("ChangedResource")
    p.emit(0x807E0AB8)
    p.call("WriteResource")
    p.call("ResError")
    p.expect(0, "persist real replacement artwork to the resource fork")
    p.emit(0x807E0AB0)
    p.call("CloseResFile")
    p.emit(0x807E0AB4)
    p.call("UseResFile")
    p.emit(0x3C602A2A, 0x60632A2A, 0x7C641B78)
    p.call("FlushIconRefs")
    p.expect(0, "discard reloadable icon data for wildcard creator and type")
    for offset, word in ((0xA88, 0x2468ACE0), (0xA8C, 0x13385D82)):
        p.emit(0x807E0AA0)
        p.call("DisposeHandle")
        p.emit(0x807E0000 | offset, 0x38800001, 0x38BE0AA0)
        p.call("IconRefToIconFamily")
        p.expect(0, "copy the current source-specific icon family")
        p.emit(0x807E0AA0, 0x3C804943, 0x60844E23, 0x80BE0AA4)
        p.call("GetIconFamilyData")
        p.expect(0, "extract refreshed artwork without changing another file's icon")
        p.emit(0x807E0AA4, 0x80630000, 0x80630000)
        p.expect(word, "only the modified source file receives replacement artwork")

    def check_overridden_artwork(expected):
        p.emit(0x807E0AA0)
        p.call("DisposeHandle")
        p.emit(0x807E0A8C, 0x38800001, 0x38BE0AA0)
        p.call("IconRefToIconFamily")
        p.expect(0, "copy the visible family for an overridden or restored icon")
        p.emit(0x807E0AA0, 0x80630000, 0x80630010)
        p.expect(expected, "the copied icon family reflects the active artwork")

    p.emit(0x807E0A8C, 0x809E0A88)
    p.call("OverrideIconRef")
    p.expect(0, "apply artwork from another live icon reference")
    check_overridden_artwork(0x2468ACE0)
    p.emit(0x807E0A8C)
    p.call("RemoveIconRefOverride")
    p.expect(0, "remove the active replacement")
    check_overridden_artwork(0x13385D82)
    p.emit(0x807E0A8C, 0x389E09D0, 0x38A0BFB9)
    p.call("OverrideIconRefFromResource")
    p.expect(0, "apply an override from the actual resource fork")
    check_overridden_artwork(0x2468ACE0)
    p.emit(0x807E0A8C)
    p.call("UpdateIconRef")
    p.expect(0, "refreshing the base artwork preserves the installed override")
    check_overridden_artwork(0x2468ACE0)
    p.emit(0x807E0A8C, 0x389E09D0, 0x38A00001)
    p.call("OverrideIconRefFromResource")
    p.expect(-192, "a missing bitmap leaves the previously installed override active")
    check_overridden_artwork(0x2468ACE0)
    p.emit(0x807E0A8C)
    p.call("RemoveIconRefOverride")
    p.expect(0, "remove the resource override")
    check_overridden_artwork(0x13385D82)

    for offset in (0xAA0, 0xAA4):
        p.emit(0x807E0000 | offset)
        p.call("DisposeHandle")
    for offset in (0xA88, 0xA8C):
        p.emit(0x807E0000 | offset)
        p.call("ReleaseIconRef")
        p.expect(0, "release one file-icon reference")
    p.emit(0x807E0A88)
    p.call("IsValidIconRef")
    p.expect(0, "last release destroys the anonymous file icon family")
    p.data[0xA80:0xAC2] = bytes([0xA5]) * (0xAC2 - 0xA80)


def check_icon_file_registration(p, destination):
    p.emit(0x3860FFFF, 0x38800002, 0x38A00003, 0x38C00010, 0x38E00000, 0x391E0C70)
    p.call("GetIconRefFromFolder")
    p.expect(0, "resolve and retain a native folder icon through IconServicesLib")
    p.emit(0x807E0C70)
    p.call("ReleaseIconRef")
    p.expect(0, "release the folder icon returned by the migrated entry")

    name = b"Round Trip.icns"
    (destination / name.decode()).write_bytes(bytes([0xA5]) * 4096)
    p.data[0xC00 : 0xC01 + len(name)] = bytes([len(name)]) + name
    p.emit(0x38600000, 0x38800000, 0x38BE0C00, 0x38DE0C20)
    p.call("FSMakeFSSpec")
    p.expect(0, "locate the data-fork icon fixture")
    p.emit(0x387E0C20, 0x389E0C70)
    p.call("ReadIconFile")
    p.expect(-50, "reject non-icon contents in an otherwise valid data fork")
    p.emit(0x807E0C70)
    p.expect(0, "a malformed icon file publishes no handle")
    p.emit(0x807E0AA0, 0x389E0C20)
    p.call("WriteIconFile")
    p.expect(0, "rewrite the existing data fork with a shorter complete icon family")
    p.emit(0x387E0C20, 0x389E0C70)
    p.call("ReadIconFile")
    p.expect(0, "read back the truncated data-fork icon family")
    p.emit(0x807E0C70)
    p.call("GetHandleSize")
    p.expect(272, "the data fork contains precisely the serialized family")
    p.emit(0x807E0C70, 0x80630000, 0x80630010)
    p.expect(0x13385D82, "the read-back handle carries the actual icon bytes")
    p.emit(0x807E0C70)
    p.call("DisposeHandle")

    p.emit(0x38600000, 0x38800000, 0x38BE0A20, 0x38DE0C20)
    p.call("FSMakeFSSpec")
    p.expect(0, "locate the classic icon resource fixture")
    p.call("CurResFile")
    p.emit(
        0x907E0C74,
        0x3C605445,
        0x60635354,
        0x3C806963,
        0x60846E31,
        0x38BE0C20,
        0x38C0BFB9,
        0x38FE0C70,
    )
    p.call("RegisterIconRefFromResource")
    p.expect(0, "register actual classic icon artwork from its resource fork")
    p.call("CurResFile")
    p.emit(0x809E0C74)
    p.equal("registration restores the caller's current resource map")
    p.emit(0x3C605445, 0x60635354, 0x3C806963, 0x60846E31)
    p.call("UnregisterIconRef")
    p.expect(0, "drop the registered resource identity")
    p.emit(0x807E0C70)
    p.call("ReleaseIconRef")
    p.expect(0, "release the independent caller's retained resource reference")

    p.emit(0x38600000, 0x38800000, 0x38BE0C00, 0x38DE0C20)
    p.call("FSMakeFSSpec")
    p.expect(0, "resolve the serialized icon file")
    p.emit(0x3C605445, 0x60635354, 0x3C806963, 0x60846E32, 0x38BE0C20, 0x38DE0C70)
    p.call("RegisterIconRefFromIconFile")
    p.expect(0, "register the actual serialized icon data")
    p.emit(0x807E0C70, 0x38800001, 0x38BE0C78)
    p.call("IconRefToIconFamily")
    p.expect(0, "copy the registered file family independently of the original handle")
    p.emit(0x807E0C78, 0x80630000, 0x80630010)
    p.expect(0x13385D82, "the registry retained the icon artwork after closing the data fork")
    p.emit(0x807E0C78)
    p.call("DisposeHandle")
    p.emit(0x807E0AA0, 0x80830000, 0x3CA089AB, 0x60A5CDEF, 0x90A40010, 0x389E0C20)
    p.call("WriteIconFile")
    p.expect(0, "replace the registered file's original on-disk artwork")
    p.emit(0x807E0AA0, 0x80830000, 0x3CA01338, 0x60A55D82, 0x90A40010)
    p.emit(0xA87E0C20)
    p.call("FlushIconRefsByVolume")
    p.expect(0, "discard cached icon data for the file's mounted volume")
    p.emit(0x807E0C70, 0x38800001, 0x38BE0C78)
    p.call("IconRefToIconFamily")
    p.expect(0, "the original reference can still publish a family after volume flushing")
    p.emit(0x807E0C78, 0x80630000, 0x80630010)
    p.expect(0x89ABCDEF, "the existing reference lazily reloads the new icon from disk")
    p.emit(0x807E0C78)
    p.call("DisposeHandle")
    p.emit(0x3C605445, 0x60635354, 0x3C806963, 0x60846E32)
    p.call("UnregisterIconRef")
    p.expect(0, "remove the data-fork registration")
    p.emit(0x807E0C70)
    p.call("ReleaseIconRef")
    p.expect(0, "release the registered icon file reference")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
