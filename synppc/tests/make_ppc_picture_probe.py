#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_picture_probe.py
#
#   Summary:   Original large PICT2 playback fixtures; no Apple application data.
#
#   Classes:   None declared.
#
#   Functions: make_picture_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original large PICT2 playback fixtures; no Apple application data."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_picture_probe(destination):
    names = [
        "InitWindows",
        "NewCWindow",
        "SetPort",
        "RGBBackColor",
        "EraseRect",
        "GetPicture",
        "DrawPicture",
        "GetCPixel",
        "DisposeWindow",
    ]
    words, failures = [], []

    def emit(*values):
        words.extend(values)

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check(expected):
        emit(0x2C030000 | (expected & 0xFFFF))
        failures.append(len(words))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
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
    call("NewCWindow")
    emit(0x907E0140)
    call("SetPort")
    emit(0x387E0118)
    call("RGBBackColor")
    resources = []
    for resource, length, declared in [
        (128, 235744, 0x98E0),
        (129, 70000, 0xFFFF),
        (130, 128, 0),
        (131, 128, 1),
    ]:
        prefix = struct.pack(">H4h", declared, 0, 0, 32, 32) + bytes.fromhex("001102ff0c00")
        prefix += struct.pack(">I4iI", 0xFFFFFFFF, 0, 0, 32 << 16, 32 << 16, 0)
        tail = bytes.fromhex("001affff000000000031000000000020002000ff")
        picture = prefix + bytes(length - len(prefix) - len(tail)) + tail
        assert len(picture) == length
        resources.append((b"PICT", resource, picture))
        emit(0x387E0108)
        call("EraseRect")
        emit(0x38600000 | resource)
        call("GetPicture")
        emit(0x389E0108)
        call("DrawPicture")
        emit(0x38600010, 0x38800010, 0x38BE0110)
        call("GetCPixel")
        for offset, expected in [(0, -1), (2, 0), (4, 0)]:
            emit(0xA87E0110 + offset)
            check(expected)
    emit(0x807E0140)
    call("DisposeWindow")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(words)
    emit(0)
    for branch in failures:
        words[branch] |= (bad - branch) * 4
    data = bytearray(352)
    struct.pack_into(">4h", data, 256, 40, 40, 72, 72)
    struct.pack_into(">4h", data, 264, 0, 0, 32, 32)
    struct.pack_into(">3H", data, 280, 65535, 65535, 65535)
    data[288:296] = b"\x07Picture"
    app = pef(
        words,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1)],
    )
    write_fragment(destination, "PPC Picture Probe", app, 1, resources)


if __name__ == "__main__":
    make_picture_probe(Path(sys.argv[1]))
