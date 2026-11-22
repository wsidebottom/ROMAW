#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_jit_probe.py
#
#   Summary:   Original PPC fixture heats native integer blocks inside ROMAW.
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

"""Original PPC fixture heats native integer blocks inside ROMAW."""

from pathlib import Path
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code = [
        0x38600000,  # li r3,0
        0x388003E8,  # li r4,1000
        0x7C8903A6,  # mtctr r4
        0x38630001,  # addi r3,r3,1
        0x60658001,  # ori r5,r3,0x8001
        0x64A60080,  # oris r6,r5,0x80
        0x68C78000,  # xori r7,r6,0x8000
        0x6CE80080,  # xoris r8,r7,0x80
        0x3D200001,  # lis r9,1
    ]
    for operation, target, source, other in [
        (266, 10, 3, 4),  # add r10,r3,r4
        (40, 11, 3, 4),  # subf r11,r3,r4
        (28, 12, 10, 11),  # and
        (60, 0, 10, 11),  # andc
        (444, 5, 12, 0),  # or
        (316, 6, 5, 10),  # xor
        (124, 7, 6, 12),  # nor
        (284, 8, 6, 12),  # eqv
        (412, 9, 6, 12),  # orc
        (476, 12, 6, 12),  # nand
    ]:
        arithmetic = operation in (266, 40)
        code.append(
            (31 << 26)
            | ((target if arithmetic else source) << 21)
            | ((source if arithmetic else target) << 16)
            | (other << 11)
            | (operation << 1)
        )
    for target, source, other, operation in [
        (5, 10, 3, 491),  # signed quotient, final 2000 / 1000 = 2
        (6, 10, 4, 459),  # unsigned quotient, final 2000 / 1000 = 2
        (7, 11, 11, 491),  # signed zero divisor on final iteration
    ]:
        code.append((31 << 26) | (target << 21) | (source << 16) | (other << 11) | (operation << 1) | 1)
    code.extend([0x3D008000, 0x3920FFFF])  # lis r8,0x8000; li r9,-1
    code.append((31 << 26) | (8 << 21) | (8 << 16) | (9 << 11) | (491 << 1))
    code.append((31 << 26) | (9 << 21) | (10 << 16) | (11 << 11) | (459 << 1))
    code.append(0x3CE08000)  # lis r7,0x8000
    # Overflow sets OV/SO; a subsequent exact product clears OV, retaining SO.
    code.append((31 << 26) | (7 << 21) | (7 << 16) | (8 << 11) | (747 << 1) | 1)
    code.append((31 << 26) | (7 << 21) | (8 << 16) | (8 << 11) | (747 << 1) | 1)
    code.append(0x39200000)  # li r9,0: signed division overflows.
    code.append((31 << 26) | (7 << 21) | (8 << 16) | (9 << 11) | (1003 << 1) | 1)
    code.append(0x39200001)  # li r9,1: exact unsigned division clears OV.
    code.append((31 << 26) | (7 << 21) | (9 << 16) | (9 << 11) | (971 << 1) | 1)
    code.append((31 << 26) | (7 << 21) | (8 << 16) | (8 << 11) | (778 << 1) | 1)
    code.append((31 << 26) | (7 << 21) | (9 << 16) | (8 << 11) | (552 << 1) | 1)
    for operation in (522, 650, 520, 648, 746, 714, 744, 712):
        binary = operation in (522, 650, 520, 648)
        code.append((31 << 26) | (7 << 21) | (8 << 16) | ((9 if binary else 0) << 11) | (operation << 1) | 1)
    # Heat a direct BL exit after three simple instructions. Preserve the
    # leaf probe's incoming LR, and reject accidental fallthrough visibly.
    code.extend([0x7D2802A6, 0x60000000, 0x60000000, 0x60000000, 0x48000009, 0, 0x7D2803A6])
    code.append(0x38E0FFFF)  # li r7,-1; exact negation clears OV, retaining SO.
    code.append((31 << 26) | (7 << 21) | (7 << 16) | (616 << 1) | 1)
    code.append(0x7D0102A6)  # mfxer r8
    code.append((21 << 26) | (8 << 21) | (8 << 16) | (1 << 1))  # mask SO/OV
    code.append(0x3D208000)  # lis r9,0x8000: expected SO=1, OV=0
    code.append(0x60000000)  # Keep the counted branch in a four-word hot block.
    code.append(0x42000000 | ((3 - len(code)) * 4 & 0xFFFC))  # bdnz loop
    branches = []
    for register, expected in [
        (3, 1000),
        (10, 2000),
        (11, 0),
        (12, -1),
        (0, 2000),
        (5, 2),
        (6, 2),
        (7, 1),
    ]:
        code.append(0x2C000000 | (register << 16) | (expected & 0xFFFF))
        branches.append(len(code))
        code.append(0x40820000)
    code.append((31 << 26) | (8 << 16) | (9 << 11))  # cmpw r8,r9
    branches.append(len(code))
    code.append(0x40820000)
    code.extend([0x38600000, 0x4E800020])
    for branch in branches:
        code[branch] |= (len(code) - branch) * 4
    code.append(0)
    write_fragment(destination, "PPC JIT Probe", pef(code, bytes(8), main=0, reloc=[0x4600]), 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
