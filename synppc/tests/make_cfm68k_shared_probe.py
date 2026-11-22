#!/usr/bin/env python3
# +===================================================================
#   File:      make_cfm68k_shared_probe.py
#
#   Summary:   Original CFM-68K shared-library and public connection API probe.
#
#   Classes:   None declared.
#
#   Functions: fragment, make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CFM-68K shared-library and public connection API probe."""

import struct
import sys
from pathlib import Path

from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def fragment(code, data, **kwargs):
    code += bytes((-len(code)) % 4)
    result = bytearray(pef(struct.unpack(">" + "I" * (len(code) // 4), code), data, **kwargs))
    result[8:12] = b"m68k"
    return result


def make_probe(destination):
    # Exported function adds two four-byte arguments to initialized library data.
    library_code = struct.pack(
        ">21H",
        0x2A51,
        0x202D,
        32,
        0xD0AF,
        4,
        0xD0AF,
        8,
        0x4E74,
        8,
        0x2A51,
        0x2B7C,
        0,
        40,
        32,
        0x7000,
        0x4E74,
        4,
        0x2A51,
        0x7000,
        0x4E75,
        0x4E71,
    )
    data = bytearray(36)
    struct.pack_into(">6I", data, 0, 0, 0, 18, 0, 34, 0)
    library = fragment(
        library_code,
        data,
        init=8,
        term=16,
        exports=(("Answer", 2, 0, 1), ("State", 1, 32, 1)),
        reloc=(0x4602,),
    )
    write_fragment(destination, "Probe68K", library, 0, arch=b"m68k")

    code = bytearray()
    branches = []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def push(value):
        words(0x2F3C, value >> 16, value & 0xFFFF)

    def pointer(offset):
        words(0x486C, offset)

    def call(offset):
        words(0x226C, offset, 0x2059, 0x4E90)

    def check(value):
        words(0x0C80, value >> 16, value & 0xFFFF)
        branches.append(len(code))
        words(0x6600, 0)

    words(0x2A51, 0x284D)
    push(2)
    push(1)
    call(8)
    check(43)
    words(0x206C, 12, 0x2010)
    check(40)
    # GetSharedLibrary(name, architecture, flags, connection, main, errorName).
    for offset in (160, 72, 64):
        pointer(offset)
    push(1)
    push(0x6D36386B)
    pointer(96)
    call(16)
    check(0)
    # FindSymbol(connection, name, address, class).
    for offset in (76, 80, 112):
        pointer(offset)
    words(0x2F2C, 64)
    call(20)
    check(0)
    push(4)
    push(3)
    call(80)
    check(47)
    # Build a CFM-68K descriptor, then enter it using an actual classic Pascal
    # JSR: short arguments and a stack result must cross into four-byte slots.
    push(0x10)
    push(0x2B0)
    words(0x2F2C, 80)
    call(28)
    words(0x2940, 84, 0x2040)
    push(0)
    words(0x3F3C, 1, 0x3F3C, 2, 0x4E90, 0x201F)
    check(43)
    push(2)
    push(1)
    push(0x2B0)
    words(0x2F2C, 84)
    call(36)  # Variadic CallUniversalProc: the caller removes all arguments.
    words(0x4FEF, 16)
    check(43)
    words(0x2F2C, 84)
    call(32)
    pointer(148)
    words(0x2F2C, 64)
    call(40)
    check(0)
    words(0x202C, 148)
    check(2)
    for offset in (144, 140, 160):
        pointer(offset)
    push(0)
    words(0x2F2C, 64)
    call(44)
    check(0)
    words(0x7000, 0x102C, 144)
    check(2)
    words(0x202C, 140, 0xB0AC, 80)
    branches.append(len(code))
    words(0x6600, 0)
    push(64)
    call(48)  # StdCLib malloc
    words(0x2940, 88)
    push(128)
    words(0x2F00)
    call(56)  # realloc
    words(0x2F00)
    call(60)  # free
    push(16)
    push(4)
    call(52)  # calloc
    words(0x2940, 88, 0x2040, 0x2010)
    check(0)
    words(0x2F2C, 88)
    call(60)
    pointer(64)
    call(24)
    check(0)
    words(0x202C, 64)
    check(0)
    words(0x2A4C, 0x7000, 0x4E75)
    fail = len(code)
    words(0x4AFC)
    for branch in branches:
        struct.pack_into(">h", code, branch + 2, fail - (branch + 2))
    data = bytearray(448)
    data[96:105] = b"\x08Probe68K"
    data[112:119] = b"\x06Answer"
    application = fragment(
        code,
        data,
        main=0,
        imports=(
            ("Probe68K", "Answer", 2),
            ("Probe68K", "State", 1),
            ("InterfaceLib", "GetSharedLibrary", 2),
            ("InterfaceLib", "FindSymbol", 2),
            ("InterfaceLib", "CloseConnection", 2),
            ("InterfaceLib", "NewRoutineDescriptor", 2),
            ("InterfaceLib", "DisposeRoutineDescriptor", 2),
            ("InterfaceLib", "CallUniversalProc", 2),
            ("InterfaceLib", "CountSymbols", 2),
            ("InterfaceLib", "GetIndSymbol", 2),
            ("StdCLib", "malloc", 2),
            ("StdCLib", "calloc", 2),
            ("StdCLib", "realloc", 2),
            ("StdCLib", "free", 2),
        ),
        reloc=(0x4600, 0x4A0D),
    )
    write_fragment(destination, "CFM-68K Shared Probe", application, 1, arch=b"m68k")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
