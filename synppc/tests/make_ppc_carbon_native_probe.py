#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_native_probe.py
#
#   Summary:   Original native Carbon accessor and real-fragment fallback integration probe.
#
#   Classes:   None declared.
#
#   Functions: write_carbon_fallback, make_carbon_native_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original native Carbon accessor and real-fragment fallback integration probe."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def write_carbon_fallback(destination):
    # The native accessor must beat the first export. The fallback token
    # must run the PEF initializer. The wait check exercises the classic trap.
    # The wrapper calls the actual classic wait, then supplies its own event.
    wrapper = [
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93E10038,
        0x7C9F2378,
        0x81820008,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x38600001,
        0xB07F0000,
        0x83E10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x4E800020,
    ]
    library = pef(
        [0x38601234, 0x4E800020, 0x80620028, 0x4E800020, 0x38600049, 0x90620028, 0x38600000, 0x4E800020] + wrapper,
        struct.pack(">11I", 0, 0, 8, 0, 16, 0, 32, 24, 0, 0, 0),
        imports=[("InterfaceLib", "WaitNextEvent", 2)],
        exports=[("GetWindowPort", 2, 0, 1), ("CarbonFallbackToken", 2, 8, 1), ("WaitNextEvent", 2, 24, 1)],
        init=16,
        reloc=[0x4600, 0x4600, 0x4600, 0x4600, 0x4A00],
    )
    write_fragment(destination, "CarbonLib", library, 0)


def make_carbon_native_probe(destination):
    write_carbon_fallback(destination)
    names = [
        "InitWindows",
        "NewWindow",
        "DisposeWindow",
        "HiliteWindow",
        "GetWindowPort",
        "IsWindowHilited",
        "IsWindowUpdatePending",
        "GetWindowStandardState",
        "GetWindowUserState",
        "SetWindowStandardState",
        "SetWindowUserState",
        "SetPortWindowPort",
        "GetWindowPortBounds",
        "GetWindowFromPort",
        "CarbonFallbackToken",
        "GetPortBounds",
        "GetPortPixMap",
        "GetPortVisibleRegion",
        "GetPortClipRegion",
        "GetPortTextFont",
        "GetPortTextSize",
        "GetPixRowBytes",
        "GetQDGlobalsThePort",
        "SetQDError",
        "QDError",
        "TextFont",
        "TextSize",
        "NewRgn",
        "DisposeRgn",
    ]
    imports = [("CarbonLib", name, 2) for name in names]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value, nonzero=False):
        emit((0x28030000 if value >= 0x8000 else 0x2C030000) | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def same_window():
        emit(0x809E00A0, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def same_rect(offset):
        for delta in (0, 4):
            emit(0x807E00B0 + delta, 0x809E0000 + offset + delta, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    emit(0x38600000, 0x389E0080, 0x38BE0090, 0x38C00000, 0x38E00008, 0x3900FFFF, 0x39200001, 0x3940002A)
    call("NewWindow")
    check(0, True)
    emit(0x907E00A0)
    # Native GetWindowPort must return a real translated pointer, not 0x1234.
    call("GetWindowPort")
    same_window()
    # Carbon NewWindow must expose a color port.
    emit(0xA0630006, 0x7063C000)
    check(0xC000)
    emit(0x807E00A0)
    call("SetPortWindowPort")
    call("GetQDGlobalsThePort")
    same_window()
    # A null GrafPtr means the actual current port for these accessors.
    emit(0x38600000, 0x389E00B0)
    call("GetPortBounds")
    emit(0x389E00B0, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    for delta in (0, 4):
        emit(0x807E00A0, 0x80830010 + delta, 0x807E00B0 + delta, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    emit(0x38600016)
    call("TextFont")
    emit(0x38600013)
    call("TextSize")
    emit(0x38600000)
    call("GetPortTextFont")
    check(22)
    emit(0x38600000)
    call("GetPortTextSize")
    check(19)
    emit(0x38600000)
    call("GetPortPixMap")
    check(0, True)
    emit(0x907E00D0, 0x80630000, 0xA0830004, 0x70847FFF, 0x909E00D8, 0x807E00D0)
    call("GetPixRowBytes")
    emit(0x809E00D8, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    call("NewRgn")
    check(0, True)
    emit(0x907E00D4)
    for name in ("GetPortVisibleRegion", "GetPortClipRegion"):
        emit(0x38600000, 0x809E00D4)
        call(name)
        emit(0x809E00D4, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    emit(0x807E00D4)
    call("DisposeRgn")
    # Errors are visible through the same native QDError used by classic code.
    emit(0x3860FF4F)
    call("SetQDError")
    call("QDError")
    check(-177)
    emit(0x38600000)
    call("GetPortTextSize")
    check(19)
    call("QDError")
    check(-177)
    emit(0x38600000, 0x38800000)
    call("GetPortBounds")
    check(0)
    call("QDError")
    check(-50)
    emit(0x38600000)
    call("SetQDError")
    emit(0x38600000)
    call("GetWindowFromPort")
    same_window()
    emit(0x807E00A0)
    call("GetWindowFromPort")
    same_window()
    emit(0x807E00A0, 0x38800001)
    call("HiliteWindow")
    emit(0x807E00A0)
    call("IsWindowHilited")
    check(1)
    emit(0x807E00A0)
    call("IsWindowUpdatePending")
    check(0)
    emit(0x807E00A0, 0x389E00B0)
    call("GetWindowPortBounds")
    emit(0x389E00B0, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    # Rectangle copies must preserve guest bytes and the pointer result.
    for offset, setter, getter in [
        (0xC0, "SetWindowUserState", "GetWindowUserState"),
        (0xC8, "SetWindowStandardState", "GetWindowStandardState"),
    ]:
        emit(0x807E00A0, 0x389E0000 + offset)
        call(setter)
        emit(0x807E00A0, 0x389E00B0)
        call(getter)
        same_rect(offset)
    emit(0x807E00A0)
    call("DisposeWindow")
    call("CarbonFallbackToken")
    check(73)
    emit(0x38600000)
    call("GetWindowPort")
    check(0)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(224)
    struct.pack_into(">4h", data, 128, 40, 50, 140, 200)
    data[144:150] = b"\x05Probe"
    struct.pack_into(">8h", data, 192, -7, 11, 230, 410, 20, -12, 480, 640)
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Native Probe", app, 1)


if __name__ == "__main__":
    make_carbon_native_probe(Path(sys.argv[1]))
