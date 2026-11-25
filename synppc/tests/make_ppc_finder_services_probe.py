#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_finder_services_probe.py
#
#   Summary:   Generate grouped checks for Finder's native services, object callbacks and explicit optional-service errors.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe, check_object_accessors, check_file_icons; script entry point.
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
        "PPCOpenSync",
        "PPCCloseSync",
        "LMGetEventQueue",
        "GetEvQHdr",
        "CallUniversalProc",
        "NGetTrapAddress",
        "AECreateList",
        "NewControl",
        "FrontWindow",
        "DisposeControl",
        "GetControlFeatures",
        "GetControlData",
        "SetControlData",
        "AEPutKeyPtr",
        "AEPutKeyDesc",
        "AEDisposeDesc",
    ]
    imports = [("InterfaceLib", name, 2) for name in names]
    imports += [("PrivateInterfaceLib", name, 2) for name in ("RequestVolumeNotification", "DeclineVolumeNotification")]
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
        )
    ]
    imports += [
        ("IconServicesLib", name, 2)
        for name in (
            "GetIconRefByName",
            "GetIconRefFromFolder",
            "GetIconRefFromFile",
            "UpdateIconRef",
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
    imports += [("MacDialogsLib", name, 2) for name in ("SetModalDialogEventMask", "GetModalDialogEventMask")]
    imports += [("ThreadsLib", name, 2) for name in ("GetThreadCurrentTaskRef", "GetThreadStateGivenTaskRef")]
    p = Probe(imports)
    p.import_base = 0x1000
    p.identify_failed_checks = True
    p.reserve_parameter_words(9)
    p.data.extend(bytes(0xC00 - len(p.data)))
    p.call("InitWindows")
    for slot, icon_type in ((0x2E0, 0x74727368), (0x2E4, 0x66747268)):
        p.emit(
            0x3C606D61, 0x60636373, 0x3C800000 | (icon_type >> 16), 0x60840000 | (icon_type & 0xFFFF), 0x38BE0000 | slot
        )
        p.call("GetIconRefByName")
        p.expect(0, "empty/full Trash resolve actual bundled icon artwork without an isrv map")
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
    p.expect(-4, "optional volume notification reports unavailable instead of stopping execution")
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
    p.emit(0x38600000, 0x389E0300, 0x38BE0310, 0x38C00001, 0x38E00000, 0x3900FFFF, 0x39200001, 0x39400000)
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
    p.emit(0x807E0440, 0x38800000, 0x3CA0616E, 0x60A5696D, 0x38C00001, 0x38FE0448, 0x391E044C)
    p.call("GetControlData")
    p.expect(0, "read the actual chasing-arrows animation setting")
    p.emit(0x887E0448)
    p.expect(1, "new chasing-arrows controls animate by default")
    p.emit(0x807E044C)
    p.expect(1, "animation data has a one-byte Boolean ABI")
    p.emit(0x38000000, 0x981E0448, 0x807E0440, 0x38800000, 0x3CA0616E, 0x60A5696D, 0x38C00001, 0x38FE0448)
    p.call("SetControlData")
    p.expect(0, "stop the chasing-arrows animation through its actual control data")
    p.emit(0x807E0440, 0x38800000, 0x3CA0616E, 0x60A5696D, 0x38C00001, 0x38FE0448, 0x391E044C)
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
    p.expect(-4, "unavailable native file proxies return an error for the plain-title fallback")
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
    p.expect(0x16E, "new dialogs retain classic modal events by default")
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
    p.routines.append((0x720, [0xA0C40000, 0x90C204C8, 0x38C00001, 0xB0C50000, 0x38600001, 0x4E800020]))
    p.emit(0x387E0720, 0x389E04CC)
    p.call("ModalDialog")
    p.emit(0x807E04C8)
    p.expect(12, "ModalDialog really delivers the added OS event to the guest filter")
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
    p.expect(0, "Display Manager leaves desktop clicks to Finder when no animation component is installed")
    for kind, result in ((0, 0), (0x776F706E, -4)):
        p.emit(0x3C600000 | (kind >> 16), 0x60630000 | (kind & 0xFFFF))
        p.call("PlayThemeSound")
        p.expect(result, "native theme sound provider distinguishes silence from unavailable sound assets")
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
    check_file_icons(p, destination)
    message = b"PPC Finder services: passed"
    p.data[0x240 : 0x241 + len(message)] = bytes([len(message)]) + message
    p.emit(0x387E0240)
    p.call("DebugStr")
    p.finish(destination, "PPC Finder Services Probe")


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
    p.emit(0x3C60646F, 0x60636375, 0x3C806E75, 0x60846C6C, 0x38BE0700, 0x38C0FF85, 0x38E00000)
    p.call("AEInstallObjectAccessor")
    p.expect(0, "install a genuine PPC object accessor with signed refcon")
    p.call("AEObjectInit")
    p.expect(0, "repeated initialization preserves installed accessors")
    p.emit(0x3C60646F, 0x60636375, 0x3C806E75, 0x60846C6C, 0x38BE0680, 0x38DE0688, 0x38E00000)
    p.call("AEGetObjectAccessor")
    p.expect(0, "look up an exact application accessor")
    p.emit(0x807E0680, 0x389E0700)
    p.equal("accessor result is a four-byte guest procedure pointer")
    p.emit(0x807E0688)
    p.expect(-123, "accessor reference constant has guest byte order")
    p.emit(0x807E0684)
    p.expect(0xA5A5A5A5, "accessor lookup preserves adjacent output storage")
    p.emit(0x3C60646F, 0x60636375, 0x389E0600, 0x3CA0636F, 0x60A56E74, 0x3CC06E61, 0x60C66D65, 0x38FE0610, 0x391E0620)
    p.call("AECallObjectAccessor")
    p.expect(-123, "propagate accessor errors through the mixed-mode callback")
    for offset, value in ((0x780, 0x646F6375), (0x788, 0x636F6E74), (0x78C, 0x6E616D65), (0x798, -123)):
        p.emit(0x807E0000 | offset)
        p.expect(value, "object accessor receives the real class, form and refcon")
    for offset, target in ((0x784, 0x600), (0x790, 0x610), (0x794, 0x620)):
        p.emit(0x807E0000 | offset, 0x389E0000 | target)
        p.equal("object accessor receives actual guest descriptor pointers")
    p.emit(0x3C60646F, 0x60636375, 0x3C806E75, 0x60846C6C, 0x38BE0700, 0x38C00000, 0x38E00000)
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
        p.emit(0x387E0640, 0x3C800000 | (keyword >> 16), 0x60840000 | (keyword & 0xFFFF), 0x38BE0000 | offset)
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
    p.emit(0x38600000, 0x38800000, 0x38BE0710, 0x38C00000, 0x38E00000, 0x39000000, 0x39200000)
    p.call("AESetObjectCallbacks")
    p.expect(0, "install real token disposal callback")
    p.emit(0x38600000, 0x38800000, 0x38A00000, 0x38C00000, 0x38E00000, 0x39000000, 0x39200000)
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
    p.emit(0x3C60646F, 0x60636375, 0x3C806E75, 0x60846C6C, 0x38BE0680, 0x38DE0688, 0x38E00000)
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
            destination, name, b"icon fixture data", 0, extra_resources=[(b"ICN#", -16455, artwork)], include_cfrg=False
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
    p.emit(0x807E0A88)
    p.call("UpdateIconRef")
    p.expect(0, "refresh the reference from its actual source resource fork")
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
    p.data[0xA80:0xAC0] = bytes([0xA5]) * 64


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
