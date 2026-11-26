#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_icon_probe.py
#
#   Summary:   Original masked color icon and font state fixtures, including sparse indexed palettes.
#
#   Classes:   None declared.
#
#   Functions: color_icon, make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original masked color icon fixtures, including sparse indexed palettes."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def color_icon(depth):
    row = (8 * depth + 7) // 8
    bounds = struct.pack(">hhhh", 0, 0, 8, 8)
    pixmap = struct.pack(">IH", 0, 0x8000 | row) + bounds
    pixmap += struct.pack(">HHIIIHHHHIII", 0, 0, 0, 72 << 16, 72 << 16, 0, depth, 1, depth, 0, 0, 0)
    mask = struct.pack(">IH", 0, 2) + bounds
    mono = struct.pack(">IH", 0, 0) + bounds
    palette = struct.pack(">IHH", 0, 0, 1)
    palette += struct.pack(">HHHH", 0, 65535, 65535, 65535)
    palette += struct.pack(">HHHH", (1 << depth) - 1, 65535, 0, 0)
    return pixmap + mask + mono + bytes(4) + b"\xaa\x00" * 8 + palette + b"\xff" * (row * 8)


def make_probe(destination):
    names = [
        "InitWindows",
        "NewCWindow",
        "SetPort",
        "RGBBackColor",
        "EraseRect",
        "GetCIcon",
        "PlotCIcon",
        "GetCPixel",
        "DisposeCIcon",
        "DisposeWindow",
        "MakeIconCache",
        "SetIconCacheData",
        "GetIconCacheData",
        "GetIconCacheProc",
        "DisposeIconSuite",
        "NewRgn",
        "IconIDToRgn",
        "PtInIconID",
        "RectInIconID",
        "DisposeRgn",
        "LoadIconCache",
        "TENumStyles",
        "TENew",
        "TEDispose",
        "TEStylNew",
        "TESetText",
        "TESetSelect",
        "TESetStyle",
        "SetStylScrap",
        "GetStylScrap",
        "TEGetStyle",
        "DisposeHandle",
        "PlotIconID",
        "EmptyRgn",
        "SetOutlinePreferred",
        "GetOutlinePreferred",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(0)
    call(35)
    emit(0x907E01BC)
    for preferred in (0, 1):
        emit(0x38600000 | preferred)
        call(34)
        call(35)
        check(preferred)
    emit(0x807E01BC)
    call(34)
    emit(
        0x38600000,
        0x389E0100,
        0x38BE0120,
        0x38C00001,
        0x38E00000,
        0x3900FFFF,
        0x39200000,
        0x39400000,
    )
    call(1)
    emit(0x907E0140)
    call(2)
    emit(0x387E0118)
    call(3)
    for bounds in (0x108, 0x1B0):
        emit(0x387E0000 | bounds)
        call(4)
        emit(0x387E0000 | bounds, 0x38800005, 0x38A00000, 0x38C0BF9E)
        call(32)
        check(0)
        call(15)
        emit(0x907E01B8, 0x389E0000 | bounds, 0x38A00005, 0x38C0BF9E)
        call(16)
        check(0)
        emit(0x807E01B8)
        call(33)
        check(0)
        emit(0x807E01B8)
        call(19)
    for resource in [128, 129]:
        emit(0x387E0108)
        call(4)
        emit(0x38600000 | resource)
        call(5)
        emit(0x907E0144, 0x7C641B78, 0x387E0108)
        call(6)
        # The icon scales to 16x16. Masked columns are red; gaps stay white.
        for x, expected in [(0, [65535, 0, 0]), (3, [65535, 65535, 65535])]:
            emit(0x38600000 | x, 0x38800001, 0x38BE0150)
            call(7)
            for component, value in enumerate(expected):
                emit(0xA87E0150 + component * 2)
                check(value)
        emit(0x807E0144)
        call(8)
    # Selector-dispatched APIs must resolve through the same InterfaceLib as
    # trap-table imports. Verify 32-bit callback data and mask-based hit tests.
    emit(0x387E0160, 0x38800000, 0x38BE0130)
    call(10)
    check(0)
    emit(0x807E0160, 0x389E0164)
    call(12)
    check(0)
    emit(0x807E0164, 0x7C7E1850)
    check(0x130)
    emit(0x807E0160, 0x389E0164)
    call(13)
    check(0)
    emit(0x807E0164)
    check(0)
    emit(0x387E0108, 0x38800000, 0x38A00000, 0x80DE0160)
    call(20)
    check(-1000)
    emit(0x807E0160, 0x38800000)
    call(14)
    check(0)
    call(15)
    emit(0x907E0170, 0x389E0108, 0x38A00000, 0x38C00082)
    call(16)
    check(0)
    emit(0x3C600002, 0x60630004, 0x389E0108, 0x38A00000, 0x38C00082)
    call(17)
    check(1)
    emit(0x38600000, 0x389E0108, 0x38A00000, 0x38C00082)
    call(17)
    check(0)
    emit(0x387E0174, 0x389E0108, 0x38A00000, 0x38C00082)
    call(18)
    check(1)
    emit(0x807E0170)
    call(19)
    emit(0x387E0108, 0x389E0108)
    call(22)
    emit(0x907E0160, 0x7C651B78, 0x38600000, 0x3880000A)
    call(21)
    check(1)
    emit(0x807E0160)
    call(23)
    emit(0x387E0108, 0x389E0108)
    call(24)
    emit(0x907E0160, 0x7C651B78, 0x387E0130, 0x3880000A)
    call(25)
    emit(0x38600002, 0x38800005, 0x80BE0160)
    call(26)
    emit(0x38600022, 0x389E0180, 0x38A00000, 0x80DE0160)
    call(27)
    emit(0x38600000, 0x3880000A, 0x80BE0160)
    call(26)
    emit(0x38600000, 0x3880000A, 0x80BE0160)
    call(21)
    check(3)
    # Save the bold selection as a scrap, reset the document, and apply that
    # scrap outside the current selection through InterfaceLib's PPC bridge.
    emit(0x38600002, 0x38800005, 0x80BE0160)
    call(26)
    emit(0x807E0160)
    call(29)
    emit(0x907E0188)
    emit(0x38600000, 0x3880000A, 0x80BE0160)
    call(26)
    emit(0x38600002, 0x389E0190, 0x38A00000, 0x80DE0160)
    call(27)
    emit(0x38600006, 0x38800009, 0x80BE0188, 0x38C00000, 0x80FE0160)
    call(28)
    emit(0x38600007, 0x389E01A0, 0x38BE01AC, 0x38DE01AE, 0x80FE0160)
    call(30)
    emit(0x887E01A2)  # lbz r3, tsFace
    check(1)
    emit(0x817E0160, 0x816B0000, 0xA06B0020)
    check(0)
    emit(0xA06B0022)
    check(10)
    emit(0x807E0188)
    call(31)
    emit(0x807E0160)
    call(23)
    emit(0x807E0140)
    call(9)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(0x1C0)
    struct.pack_into(">hhhh", data, 0x100, 40, 40, 104, 104)
    struct.pack_into(">hhhh", data, 0x108, 0, 0, 16, 16)
    struct.pack_into(">hhhh", data, 0x1B0, 0, 0, 32, 32)
    struct.pack_into(">HHH", data, 0x118, 65535, 65535, 65535)
    data[0x120:0x12B] = b"\x0aIcon Probe"
    data[0x130:0x13A] = b"0123456789"
    data[0x182] = 1
    struct.pack_into(">hhhh", data, 0x174, 2, 4, 3, 5)
    mask = bytearray(256)
    for row in range(4, 12):
        mask[128 + row * 4 + 1] = 0xFF
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1)],
    )
    write_fragment(
        destination,
        "PPC Icon Probe",
        app,
        1,
        extra_resources=[(b"cicn", 128, color_icon(4)), (b"cicn", 129, color_icon(2)), (b"ICN#", 130, bytes(mask))],
    )


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
