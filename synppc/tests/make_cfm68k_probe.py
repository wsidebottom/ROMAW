#!/usr/bin/env python3
# +===================================================================
#   File:      make_cfm68k_probe.py
#
#   Summary:   Original CFM-68K machine code: A5, scalar Toolbox ABI and lifecycle checks.
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

"""Original CFM-68K machine code: A5, scalar Toolbox ABI and lifecycle checks."""

import struct
import sys
from pathlib import Path

from make_ppc_probe import write_fragment


def make_probe(destination, segmented=False, exit_trap=False):
    code = bytearray()
    branches = []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def push(value):
        words(0x2F3C, value >> 16, value & 0xFFFF)

    def call(offset):
        # A4 retains our data world while the imported transition changes A5.
        words(0x226C, offset + (12 if segmented else 0), 0x2059, 0x4E90)

    def check():
        branches.append(len(code))
        words(0x6600, 0)

    # Initializer: check the CFragInitBlock locator and leave an A5 sentinel.
    init = len(code)
    words(0x2A51, 0x206F, 4, 0x0CA8, 0, 2 if segmented else 1, 12)
    check()
    words(0x2B7C, 0, 1, 96, 0x7000, 0x4E74, 4)
    main = len(code)
    words(0x2A51, 0x284D, 0x0CAC, 0, 1, 96)
    check()
    for selector, expected in ((b"sysa", 1), (b"cput", 4), (b"cfrg", 1), (b"mixd", 2)):
        words(0x486C, 72)
        push(int.from_bytes(selector, "big"))
        call(36)
        words(0x4A40)
        check()
        words(0x0CAC, 0, expected, 72)
        check()
    push(64)
    call(24)  # NewPtr
    words(0x4A80)
    branches.append(len(code))
    words(0x6700, 0)
    words(0x2F00)
    call(28)  # DisposePtr
    for value in (120, 110, 20, 10):
        push(value)
    words(0x486C, 64)
    call(32)  # SetRect(Rect*, short, short, short, short)
    words(0x0C6C, 20, 64)
    check()
    words(0x0C6C, 110, 70)
    check()
    words(0x297C, 0, 2, 96, 0x2A4C, 0x7000, 0xA9F4 if exit_trap else 0x4E75)
    term = len(code)
    words(0x2A51, 0x0CAD, 0, 2, 96)
    check()
    words(0x7000, 0x4E75)
    failure = len(code)
    words(0x4AFC)  # A failed assertion must not look like normal completion.
    for branch in branches:
        struct.pack_into(">h", code, branch + 2, failure - (branch + 2))

    imports = ("NewPtr", "DisposePtr", "SetRect", "Gestalt")
    strings = b"InterfaceLib\0"
    symbols = []
    for name in imports:
        symbols.append(0x02000000 | len(strings))
        strings += name.encode() + b"\0"
    reloc_offset = 56 + 24 + 4 * len(imports) + 12
    relocations = struct.pack(">HH", 0x4402 if segmented else 0x4602, 0x4A03)
    string_offset = reloc_offset + len(relocations)
    loader = struct.pack(
        ">14I",
        1,
        0,
        1,
        12 if segmented else 8,
        1,
        24 if segmented else 16,
        1,
        len(imports),
        1,
        reloc_offset,
        string_offset,
        string_offset + len(strings),
        0,
        0,
    )
    loader += struct.pack(">6I", 0, 0, 0, len(imports), 0, 0)
    loader += b"".join(struct.pack(">I", symbol) for symbol in symbols)
    loader += struct.pack(">HHII", 1, 0, len(relocations) // 2, 0)
    loader += relocations + strings
    data = bytearray(128)
    for index, entry in enumerate((main, init, term)):
        if segmented:
            struct.pack_into(">III", data, index * 12, 0, 0, (1 << 20) | (entry // 2))
        else:
            struct.pack_into(">II", data, index * 8, entry, 0)
    resources = []
    if segmented:
        header = struct.pack(">10I", 0xFFFD0000, 0, 0, 0, 3, 0, 0, 0, 0, 0)
        resources.append((b"CODE", 1, header + code))
        # If the launch path mistakes this for classic CODE 0 it must fail.
        resources.append((b"CODE", 0, bytes(16)))
        code = b"\x4e\x75"
    code_offset = (128 + len(loader) + 15) & ~15
    data_offset = (code_offset + len(code) + 15) & ~15
    pef = bytearray(data_offset + len(data))
    pef[:40] = b"Joy!peffm68k" + struct.pack(">5IHHI", 1, 0, 0, 0, 0, 3, 2, 0)
    for index, (payload, offset, kind) in enumerate(((code, code_offset, 0), (data, data_offset, 1), (loader, 128, 4))):
        struct.pack_into(
            ">6I4B",
            pef,
            40 + 28 * index,
            0xFFFFFFFF,
            0,
            len(payload),
            len(payload),
            len(payload),
            offset,
            kind,
            0,
            4,
            0,
        )
        pef[offset : offset + len(payload)] = payload
    if segmented:
        descriptor = bytearray(pef[:40]) + bytes(16)
        descriptor[4:8] = b"rseg"
        struct.pack_into(">HHI", descriptor, 48, 1, 1, 0)
        resources.extend(((b"rseg", 0, descriptor), (b"rseg", 1, bytes(pef))))
    write_fragment(
        destination,
        "CFM-68K Probe",
        pef,
        1,
        resources,
        arch=b"m68k",
        location=2 if segmented else 1,
    )


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), "--segmented" in sys.argv[2:], "--exit-trap" in sys.argv[2:])
