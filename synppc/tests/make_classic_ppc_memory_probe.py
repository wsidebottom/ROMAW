#!/usr/bin/env python3
# +===================================================================
#   File:      make_classic_ppc_memory_probe.py
#
#   Summary:   Generate a classic 68K client loading a bounded zero-length PPC memory fragment.
#
#   Classes:   None.
#
#   Functions: make_probe
#
#   Origin:    Original regression machine code for Executor's classic CFM bridge.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

"""Classic CODE client, original embedded PPC code, no Apple binary inputs."""

import struct
import sys
from pathlib import Path

from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = bytearray()
    branches, references = [], []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def push(value):
        words(0x2F3C, value >> 16, value & 0xFFFF)

    def pointer(offset):
        words(0x486D, offset)

    def address(label):
        references.append((len(code), label))
        words(0x487A, 0)

    def check(value, short=False):
        words(0x0C40, value) if short else words(0x0C80, value >> 16, value & 0xFFFF)
        branches.append(len(code))
        words(0x6600, 0)

    for label in ("overflow", "out_of_bounds"):
        words(0x4267)
        address(label)
        push(0)
        address("name")
        push(1)
        for offset in (48, 52, 160):
            pointer(offset)
        words(0x3F3C, 3, 0xAA5A, 0x301F)
        check(0xFFCE, short=True)
        words(0x202D, 48)
        check(0)
        words(0x202D, 52)
        check(0)
    words(0x4267)
    address("pef")
    push(0)
    address("name")
    push(1)
    for offset in (48, 52, 160):
        pointer(offset)
    words(0x3F3C, 3, 0xAA5A, 0x301F)
    check(0, short=True)
    # Pascal long(void), PowerPC routine descriptor using the returned vector.
    for offset, value in ((96, 0xAAFE0700), (100, 0), (104, 0), (108, 0x30), (112, 0x00010000), (120, 0), (124, 0)):
        words(0x2B7C, value >> 16, value & 0xFFFF, offset)
    words(0x2B6D, 52, 116)
    push(0)
    words(0x41ED, 96, 0x4E90, 0x201F)
    check(42)
    words(0x4267)
    pointer(48)
    words(0x3F3C, 4, 0xAA5A, 0x301F)
    check(0, short=True)
    words(0xA9F4)
    failure = len(code)
    words(0x4AFC)
    labels = {}
    container = pef(
        [
            0x7C0802A6,  # Save LR across the availability-probe call.
            0x9421FFE0,
            0x90010024,
            0x81820008,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
            0x2C030000,
            0x40820018,  # Unexpectedly advertising MP support is a failure.
            0x3860002A,
            0x80010024,
            0x7C0803A6,
            0x38210020,
            0x4E800020,
            0,
        ],
        bytes(12),
        imports=[("MPLibrary", "_MPIsFullyInitialized", 2)],
        main=0,
        reloc=[0x4600, 0x4A00],
    )
    overflow = bytearray(container)
    struct.pack_into(">II", overflow, 40 + 16, 0x100, 0xFFFFFFF0)
    out_of_bounds = bytearray(container)
    struct.pack_into(">II", out_of_bounds, 40 + 16, 0x100, 0x70000000)
    for label, payload in (
        ("name", b"\x0bPPCInMemory"),
        ("overflow", overflow),
        ("out_of_bounds", out_of_bounds),
        ("pef", container),
    ):
        code.extend(bytes((-len(code)) & 3))
        labels[label] = len(code)
        code.extend(payload)
    for offset, label in references:
        struct.pack_into(">h", code, offset + 2, labels[label] - offset - 2)
    for offset in branches:
        struct.pack_into(">h", code, offset + 2, failure - offset - 2)
    jump = struct.pack(">4H", 0, 0x3F3C, 1, 0xA9F0)
    write_fragment(
        destination,
        "Classic PPC Memory Probe",
        b"",
        1,
        ((b"CODE", 0, struct.pack(">4I", 448, 0, 8, 32) + jump), (b"CODE", 1, struct.pack(">2H", 0, 1) + code)),
        include_cfrg=False,
    )


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
