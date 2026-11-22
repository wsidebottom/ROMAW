#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_records_probe.py
#
#   Summary:   Original PPC QuickDraw record, unsigned result and PEF fallback checks.
#
#   Classes:   None declared.
#
#   Functions: make_records_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC QuickDraw record, unsigned result and PEF fallback checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_records_probe(destination):
    write_carbon_fallback(destination)
    names = [
        "InitWindows",
        "NewWindow",
        "DisposeWindow",
        "SetPort",
        "TextFace",
        "TextMode",
        "PenMode",
        "HidePen",
        "SpaceExtra",
        "PenSize",
        "MoveTo",
        "RGBForeColor",
        "RGBBackColor",
        "GetPortTextFace",
        "GetPortTextMode",
        "GetPortPenMode",
        "GetPortPenVisibility",
        "GetPortSpExtra",
        "GetPortChExtra",
        "GetPortFracHPenLocation",
        "GetPortPenSize",
        "GetPortPenLocation",
        "GetPortGrafProcs",
        "GetPortBitMapForCopyBits",
        "GetPixBounds",
        "GetPixDepth",
        "GetRegionBounds",
        "GetPortForeColor",
        "GetPortBackColor",
        "GetPortOpColor",
        "GetPortHiliteColor",
        "GetPortPixMap",
        "NewRgn",
        "RectRgn",
        "DisposeRgn",
        "SetEmptyRgn",
        "CarbonFallbackToken",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def equal():
        emit(0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def check(value):
        value &= 0xFFFFFFFF
        emit(0x3C800000 | (value >> 16), 0x60840000 | (value & 65535))
        equal()

    def output_pointer():
        emit(0x389E0120)
        equal()

    def scalar(name, expected):
        emit(0x38600000)
        call(name)
        check(expected)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    emit(0x38600000, 0x389E0200, 0x38BE0210, 0x38C00000, 0x38E00000, 0x3900FFFF, 0x39200000, 0x39400000)
    call("NewWindow")
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000, 0x907E0100)
    call("SetPort")
    for name, value in [("TextFace", 255), ("TextMode", 1), ("PenMode", 10)]:
        emit(0x38600000 | value)
        call(name)
    call("HidePen")
    emit(0x3C601234, 0x60638000)
    call("SpaceExtra")
    emit(0x38600003, 0x38800005)
    call("PenSize")
    emit(0x3860FFF3, 0x3880001B)
    call("MoveTo")
    for name, value in [
        ("GetPortTextFace", 255),
        ("GetPortTextMode", 1),
        ("GetPortPenMode", 10),
        ("GetPortPenVisibility", -1),
        ("GetPortSpExtra", 0x12348000),
    ]:
        scalar(name, value)
    emit(0x807E0100, 0x3880FFEF, 0xB083000C, 0x3880FB2E, 0xB083000E)
    scalar("GetPortChExtra", -17)
    scalar("GetPortFracHPenLocation", -1234)
    for name, value in [("GetPortPenSize", 0x00050003), ("GetPortPenLocation", 0x001BFFF3)]:
        emit(0x38600000, 0x389E0120)
        call(name)
        output_pointer()
        emit(0x807E0120)
        check(value)
    emit(0x807E0100, 0x389E0280, 0x90830068, 0x38600000)
    call("GetPortGrafProcs")
    emit(0x389E0280)
    equal()
    emit(0x807E0100, 0x38800000, 0x90830068, 0x38600000)
    call("GetPortBitMapForCopyBits")
    emit(0x809E0100, 0x38840002)
    equal()
    emit(0x38600000)
    call("GetPortPixMap")
    emit(0x907E0110, 0x80630000, 0xA8830020, 0x909E0118, 0x807E0110)
    call("GetPixDepth")
    emit(0x809E0118)
    equal()
    emit(0x807E0110, 0x389E0120)
    call("GetPixBounds")
    output_pointer()
    for delta in (0, 4):
        emit(0x807E0110, 0x80630000, 0x80830006 + delta, 0x807E0120 + delta)
        equal()
    for setter, getter, offset in [
        ("RGBForeColor", "GetPortForeColor", 544),
        ("RGBBackColor", "GetPortBackColor", 552),
    ]:
        emit(0x387E0000 + offset)
        call(setter)
        emit(0x38600000, 0x389E0120)
        call(getter)
        output_pointer()
        emit(0x807E0120, 0x809E0000 + offset)
        equal()
        emit(0xA07E0124, 0xA09E0000 + offset + 4)
        equal()
    # GrafVars are a public classic record; write distinct operation/highlight colors.
    emit(0x807E0100, 0x80630008, 0x80630000)
    for delta in (0, 4, 8):
        emit(0x809E0238 + delta, 0x90830000 + delta)
    for name, offset in [("GetPortOpColor", 568), ("GetPortHiliteColor", 574)]:
        emit(0x38600000, 0x389E0120)
        call(name)
        output_pointer()
        emit(0x807E0120, 0x809E0000 + offset)
        equal()
        emit(0xA07E0124, 0xA09E0000 + offset + 4)
        equal()
    call("NewRgn")
    emit(0x907E0114, 0x389E0248)
    call("RectRgn")
    emit(0x807E0114, 0x389E0120)
    call("GetRegionBounds")
    output_pointer()
    for delta in (0, 4):
        emit(0x807E0120 + delta, 0x809E0248 + delta)
        equal()
    emit(0x807E0114)
    call("SetEmptyRgn")
    emit(0x807E0114, 0x389E0120)
    call("GetRegionBounds")
    for delta in (0, 4):
        emit(0x807E0120 + delta)
        check(0)
    emit(0x807E0114)
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
    data = bytearray(768)
    struct.pack_into(">4h", data, 512, 40, 50, 140, 200)
    data[528:534] = b"\x05Probe"
    struct.pack_into(">3H", data, 544, 0x1234, 0x5678, 0xABCD)
    struct.pack_into(">3H", data, 552, 0xFEDC, 0x4321, 0x8765)
    struct.pack_into(">6H", data, 568, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666)
    struct.pack_into(">4h", data, 584, -10, 2, 234, 567)
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Records Probe", app, 1)


if __name__ == "__main__":
    make_records_probe(Path(sys.argv[1]))
