#!/usr/bin/env python3
# +===================================================================
#   File:      make_cfm68k_classic_probe.py
#
#   Summary:   Classic CODE application opening and calling an original CFM-68K plug-in.
#
#   Classes:   None declared.
#
#   Functions: make_classic
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Classic CODE application opening and calling an original CFM-68K plug-in."""

import struct
import sys
from pathlib import Path

from make_cfm68k_shared_probe import make_probe
from make_ppc_probe import write_fragment


def make_classic(destination):
    make_probe(destination)
    code = bytearray()
    branches, addresses = [], []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def push(value):
        words(0x2F3C, value >> 16, value & 0xFFFF)

    def pointer(offset):
        words(0x486D, offset)

    def name(label):
        addresses.append((len(code), label))
        words(0x487A, 0)  # pea d16(pc)

    def check(value, short=False):
        if short:
            words(0x0C40, value)
        else:
            words(0x0C80, value >> 16, value & 0xFFFF)
        branches.append(len(code))
        words(0x6600, 0)

    words(0x203C, 0x6366, 0x7267, 0xA1AD)  # Gestalt('cfrg'), before any closure.
    check(0, short=True)
    words(0x2008)
    check(1)
    # A rejected optional memory fragment must leave the classic application's
    # callback context usable for a subsequent real library connection.
    words(0x4267)
    name("invalid_fragment")
    push(0)
    name("empty_name")
    push(1)
    for offset in (48, 52, 160):
        pointer(offset)
    words(0x3F3C, 3, 0xAA5A, 0x301F)
    check(0xFFCE, short=True)  # paramErr
    # An EventAvail patch makes WaitNextEvent service guest hooks. The rejected
    # CFM request must not make that path return before consuming an event.
    words(0x303C, 0xA971, 0xA746)
    addresses.append((len(code), "event_hook"))
    words(0x43FA, 0, 0x2348, 2)  # lea hook(pc),a1; move.l a0,2(a1)
    addresses.append((len(code), "event_hook"))
    words(0x41FA, 0, 0x303C, 0xA971, 0xA647)
    words(0x307C, 12, 0x7000, 0xA02F)  # A0=app1Evt, D0=message; no window needed.
    check(0, short=True)
    words(0x4267, 0x3F3C, 0x1000)
    pointer(64)
    push(0)
    push(0)
    words(0xA860, 0x101F, 0x0240, 0xFF)
    check(1, short=True)
    words(0x4267)  # reserve OSErr
    name("library")
    push(0x6D36386B)
    push(1)
    for offset in (48, 52, 160):
        pointer(offset)
    words(0x3F3C, 1, 0xAA5A, 0x301F)
    check(0, short=True)
    words(0x4267, 0x2F2D, 48)
    name("symbol")
    for offset in (56, 60):
        pointer(offset)
    words(0x3F3C, 5, 0xAA5A, 0x301F)
    check(0, short=True)
    # RoutineDescriptor: Pascal short(short,short), native ISA/RTA 0x10.
    for offset, value in (
        (96, 0xAAFE0700),
        (100, 0),
        (104, 0),
        (108, 0x2B0),
        (112, 0x00100000),
        (120, 0),
        (124, 0),
    ):
        words(0x2B7C, value >> 16, value & 0xFFFF, offset)
    words(0x2B6D, 56, 116)
    push(0)
    words(0x3F3C, 1, 0x3F3C, 2, 0x41ED, 96, 0x4E90, 0x201F)
    check(43)
    words(0x4267)
    pointer(48)
    words(0x3F3C, 4, 0xAA5A, 0x301F)
    check(0, short=True)
    words(0xA9F4)
    failure = len(code)
    words(0x4AFC)
    labels = {}
    for label, value in (
        ("library", b"\x08Probe68K"),
        ("symbol", b"\x06Answer"),
        ("empty_name", b"\0"),
        ("invalid_fragment", b"not a PEF container\0"),
        ("event_hook", bytes.fromhex("4ef900000000")),
    ):
        if label == "event_hook" and len(code) & 1:
            code.append(0)
        labels[label] = len(code)
        code.extend(value)
    for at, label in addresses:
        struct.pack_into(">h", code, at + 2, labels[label] - (at + 2))
    for at in branches:
        struct.pack_into(">h", code, at + 2, failure - (at + 2))
    jump_table = struct.pack(">4H", 0, 0x3F3C, 1, 0xA9F0)
    code0 = struct.pack(">4I", 448, 0, 8, 32) + jump_table
    code1 = struct.pack(">2H", 0, 1) + code
    write_fragment(
        destination,
        "Classic CFM-68K Probe",
        b"",
        1,
        ((b"CODE", 0, code0), (b"CODE", 1, code1)),
        include_cfrg=False,
    )


if __name__ == "__main__":
    make_classic(Path(sys.argv[1]))
