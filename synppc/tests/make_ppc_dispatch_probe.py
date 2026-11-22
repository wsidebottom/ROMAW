#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_dispatch_probe.py
#
#   Summary:   Original selector-dispatched PPC descriptor and nine-argument ABI probe.
#
#   Classes:   None declared.
#
#   Functions: make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original selector-dispatched PPC descriptor and nine-argument ABI probe."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination, mode="valid"):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def check(register, value):
        emit(0x2C000000 | register << 16 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    def call(selector, convention, expected):
        emit(
            0x387E0100,
            0x3C8003F7,
            0x60849FB0 | convention,
            0x38A00000 | selector,
            0x38C00011,
            0x38E00022,
            0x390000F3,
            0x39208044,
            0x39400055,
        )
        for offset, value in [(56, 0xE6), (60, 0x77), (64, 0x88), (68, 0x99)]:
            emit(0x38000000 | value, 0x90010000 | offset)
        emit(0x819E0008, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
        check(3, expected)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0x93C10078, 0x7C5E1378)
    call(0x5A, 14, 42)  # select second record; omit selector; relative TV
    call(0x1111, 8, 11)  # D0 dispatch selects first record
    call(0x5A, 12, 42)  # D1 dispatch selects second record
    call(0x7777, 14, -50)  # no exact match: use the default record
    emit(0x83C10078, 0x80010084, 0x7C0803A6, 0x38210080, 0x38600000, 0x4E800020)
    first = len(code) * 4
    emit(0x3860000B, 0x4E800020)
    second = len(code) * 4
    for register, value in [
        (3, 0x11),
        (4, 0x22),
        (5, -13),
        (6, -32700),
        (7, 0x55),
        (8, -26),
        (9, 0x77),
        (10, 0x88),
    ]:
        check(register, value)
    emit(0x81610038)
    check(11, 0x99)
    emit(0x3860002A, 0x4E800020)
    default = len(code) * 4
    emit(0x3860FFCE, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(352)
    for at, pc in [(64, first), (72, second), (80, default)]:
        struct.pack_into(">II", data, at, pc, 0)
    struct.pack_into(">HBBIBBH", data, 256, 0xAAFE, 7, 0, 0, 0, 0, 2)
    for at, selector, flags, vector in [
        (268, 0x1111, 0x0C, 64),
        (288, 0x5A, 0x0D, (72 - 256) & 0xFFFFFFFF),
        (308, 0, 0x1C, 80),
    ]:
        struct.pack_into(">IBBHIII", data, at, 0x00FDE7F0, 0, 1, flags, vector, 0, selector)
    if mode == "missing":
        struct.pack_into(">I", data, 304, 0x9999)
        struct.pack_into(">H", data, 314, 0x0C)
    elif mode == "signature":
        struct.pack_into(">I", data, 288, 0x00FDE7C0)
    elif mode == "relative":
        struct.pack_into(">I", data, 296, 0x7FFFFFFF)
    elif mode == "unprepared":
        struct.pack_into(">H", data, 294, 0x0F)
    elif mode == "count":
        struct.pack_into(">H", data, 266, 256)
    elif mode == "indexed":
        data[259] = 1
    elif mode != "valid":
        raise ValueError(mode)
    # Main TV, import, three target TVs, then absolute record vectors.
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", "CallUniversalProc", 2)],
        main=0,
        reloc=[0x4600, 0x4A00, 0x8033, 0x4602, 0x80BB, 0x4200, 0x8023, 0x4200],
    )
    write_fragment(destination, "PPC Dispatch Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
