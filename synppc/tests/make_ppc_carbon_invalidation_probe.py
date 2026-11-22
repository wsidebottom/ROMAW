#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_invalidation_probe.py
#
#   Summary:   Original explicit-window invalidation, region ownership and ABI checks.
#
#   Classes:   None declared.
#
#   Functions: make_invalidation_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original explicit-window invalidation, region ownership and ABI checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_invalidation_probe(destination):
    write_carbon_fallback(destination)
    names = [
        "InitWindows",
        "NewWindow",
        "DisposeWindow",
        "NewRgn",
        "DisposeRgn",
        "RectRgn",
        "EqualRgn",
        "SetEmptyRgn",
        "EmptyRgn",
        "DiffRgn",
        "GetQDGlobalsThePort",
        "SetPort",
        "InvalWindowRect",
        "ValidWindowRect",
        "InvalWindowRgn",
        "ValidWindowRgn",
        "CarbonFallbackToken",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def equal(first, second):
        emit(0x807E0000 + first, 0x809E0000 + second)
        call("EqualRgn")
        check(1)

    def rect_region(handle, rectangle):
        emit(0x807E0000 + handle, 0x389E0000 + rectangle)
        call("RectRgn")

    def change(name, argument, is_region=False):
        emit(0x807E0100, (0x809E0000 if is_region else 0x389E0000) + argument)
        call(name)
        check(0)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    call("GetQDGlobalsThePort")
    emit(0x907E0104, 0x38600000, 0x389E0120, 0x38BE0150, 0x38C00000, 0x38E00000, 0x3900FFFF, 0x39200000, 0x39400000)
    call("NewWindow")
    check(0, True)
    emit(0x907E0100, 0x8063007A, 0x907E0108)
    call("SetEmptyRgn")
    for offset in (268, 272, 276):
        call("NewRgn")
        check(0, True)
        emit(0x907E0000 + offset)
    # Explicit-window operations must preserve this unrelated current port.
    emit(0x807E0104)
    call("SetPort")
    rect_region(268, 296)
    rect_region(276, 296)
    rect_region(272, 304)
    change("InvalWindowRgn", 268, True)
    equal(268, 276)
    equal(264, 272)
    change("ValidWindowRgn", 268, True)
    equal(268, 276)
    emit(0x807E0108)
    call("EmptyRgn")
    check(1)
    change("InvalWindowRect", 296)
    equal(264, 272)
    # Remove an interior rectangle and compare the actual nonrectangular region.
    change("ValidWindowRect", 312)
    rect_region(268, 320)
    emit(0x807E0110, 0x809E010C, 0x80BE0110)
    call("DiffRgn")
    equal(264, 272)
    call("GetQDGlobalsThePort")
    emit(0x809E0104, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    for name in ("InvalWindowRect", "ValidWindowRect", "InvalWindowRgn", "ValidWindowRgn"):
        emit(0x38600000, 0x389E0128)
        call(name)
        check(-5600)
        emit(0x807E0100, 0x38800000)
        call(name)
        check(-50)
    for offset in (268, 272, 276):
        emit(0x807E0000 + offset)
        call("DisposeRgn")
    emit(0x807E0100)
    call("DisposeWindow")
    call("CarbonFallbackToken")
    check(73)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(352)
    for offset, rect in [
        (288, (40, 50, 140, 200)),
        (296, (10, 10, 30, 40)),
        (304, (50, 60, 70, 90)),
        (312, (15, 15, 25, 30)),
        (320, (55, 65, 65, 80)),
    ]:
        struct.pack_into(">4h", data, offset, *rect)
    data[336:342] = b"\x05Probe"
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Invalidation Probe", app, 1)


if __name__ == "__main__":
    make_invalidation_probe(Path(sys.argv[1]))
