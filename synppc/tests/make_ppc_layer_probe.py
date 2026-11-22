#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_layer_probe.py
#
#   Summary:   Original PPC floating-layer, system-window, callback and ownership checks.
#
#   Classes:   None declared.
#
#   Functions: make_layer_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC floating-layer, system-window, callback and ownership checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_layer_probe(destination):
    imports = [
        ("InterfaceLib", name, 2)
        for name in [
            "InitWindows",
            "NewRoutineDescriptor",
            "CallUniversalProc",
            "DisposeRoutineDescriptor",
            "FrontWindow",
            "SystemEvent",
            "GetCurrentProcess",
            "GetToolTrapAddress",
            "SetToolTrapAddress",
            "ShowWindow",
            "HideWindow",
            "LMGetWindowList",
        ]
    ]
    imports += [
        ("PrivateInterfaceLib", name, 2)
        for name in [
            "NewFloatLayer",
            "DisposeFloatLayer",
            "SwapCurLayer",
            "GetCurLayer",
            "GetRootLayer",
            "GetSubWindows",
            "GetParent",
            "FrontWindowIn",
            "IsLayer",
        ]
    ]
    imports += [("WindowsLib", name, 2) for name in ["NewCWindow", "DisposeWindow"]]
    names = [name for _, name, _ in imports]
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0008 + names.index(name) * 4,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def pointer(offset):
        emit(0x809E0000 + offset, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0x93C10078, 0x7C5E1378)
    call("InitWindows")
    emit(0x387E0090)
    call("NewFloatLayer")
    check(0)
    emit(0x807E0090)
    call("IsLayer")
    check(1)
    emit(0x807E0090)
    call("SwapCurLayer")
    check(0, True)
    emit(0x907E0094)
    call("GetCurLayer")
    pointer(144)
    emit(0x807E0090)
    call("GetSubWindows")
    check(0)
    emit(0x807E0094)
    call("SwapCurLayer")
    pointer(144)
    emit(
        0x38600000,
        0x389E00A0,
        0x38BE00B0,
        0x38C00001,
        0x38E00000,
        0x3900FFFF,
        0x39200000,
        0x3940002A,
    )
    call("NewCWindow")
    check(0, True)
    emit(0x907E0084)
    call("LMGetWindowList")
    pointer(132)
    emit(0x807E0084)
    call("GetParent")
    pointer(148)
    call("FrontWindow")
    pointer(132)
    emit(0x387E0110, 0x38803BE0, 0x38A00001)
    call("NewRoutineDescriptor")
    check(0, True)
    emit(0x907E008C)
    # OSDispatch selector5A: nine arguments including four PPC stack parameters.
    emit(
        0x8060103C,
        0x3C8003F7,
        0x60849FBE,
        0x38A0005A,
        0x38DE00A0,
        0x38FE00B0,
        0x39000000,
        0x39200000,
        0x3940FFFF,
    )
    for offset in [56, 60, 68]:
        emit(0x38000000, 0x90010000 + offset)
    emit(0x801E008C, 0x90010040)
    call("CallUniversalProc")
    check(0, True)
    emit(0x907E0088)
    call("GetParent")
    pointer(144)
    emit(0x807E0090)
    call("GetSubWindows")
    pointer(136)
    emit(0x807E0094)
    call("GetSubWindows")
    pointer(132)
    emit(0x807E0088, 0x80630090)
    check(0)  # Canonical child chain stops at the layer boundary.
    call("FrontWindow")
    pointer(132)
    # Owner's real serial number is the same as the Process Manager public API.
    emit(0x387E00F0)
    call("GetCurrentProcess")
    check(0)
    emit(0x807E0088, 0x806300AA, 0x80830024, 0x80BE00F0, 0x7C042800)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x80830028, 0x80BE00F4, 0x7C042800)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E00D0)
    call("SystemEvent")
    check(1)
    emit(0x807E00E8)
    check(1)
    # Update and keyboard events belonging to the application also pass through.
    emit(0x38600006, 0xB07E00D0, 0x807E0084, 0x907E00D2, 0x387E00D0)
    call("SystemEvent")
    check(0)
    emit(0x38600004, 0xB07E00D0, 0x387E00D0)
    call("SystemEvent")
    check(0)
    emit(0x807E00E8)
    check(1)
    # Hidden system windows must not intercept the application's mouse input.
    emit(0x38600001, 0xB07E00D0, 0x3C600050, 0x60630050, 0x907E00DA)
    emit(0x387E00D0)
    call("SystemEvent")
    check(0)
    emit(0x807E00E8)
    check(1)
    emit(0x807E0088)
    call("ShowWindow")
    call("GetCurLayer")
    pointer(148)
    emit(0x387E00D0)
    call("SystemEvent")
    check(1)
    emit(0x807E00E8)
    check(2)
    emit(0x807E00EC)
    check(3)  # inContent
    emit(0x807E0088)
    call("HideWindow")
    call("GetCurLayer")
    pointer(148)
    emit(0x38600002, 0xB07E00D0, 0x387E00D0)
    call("SystemEvent")
    check(0)
    emit(0x807E00E8)
    check(2)
    # The private disposal returns a short OSErr and preserves the application's layer.
    emit(0x8060103C, 0x388003AE, 0x38A0005B, 0x80DE0088)
    call("CallUniversalProc")
    check(0)
    emit(0x807E0090)
    call("GetSubWindows")
    check(0)
    call("GetCurLayer")
    pointer(148)
    call("FrontWindow")
    pointer(132)
    emit(0x807E0084)
    call("DisposeWindow")
    call("LMGetWindowList")
    check(0)
    emit(0x387E0090)
    call("DisposeFloatLayer")
    check(0)
    emit(0x807E0090)
    check(0)
    emit(0x807E008C)
    call("DisposeRoutineDescriptor")
    emit(0x83C10078, 0x80010084, 0x7C0803A6, 0x38210080, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    callback = len(code) * 4
    emit(
        0x90A200EC,
        0x806200E8,
        0x38630001,
        0x906200E8,
        0x38600001,
        0x98660000,
        0x38600000,
        0x4E800020,
    )
    data = bytearray(320)
    struct.pack_into(">II", data, 272, callback, 0)
    struct.pack_into(">4h", data, 160, 40, 50, 140, 200)
    data[176:182] = b"\x05Probe"
    app = pef(
        code,
        data,
        imports=imports,
        main=0,
        reloc=[0x4600, 0x4A00 | (len(imports) - 1), 0xA000, 272, 0x4600],
    )
    write_fragment(destination, "PPC Layer Probe", app, 1)


if __name__ == "__main__":
    make_layer_probe(Path(sys.argv[1]))
