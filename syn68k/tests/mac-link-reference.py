#!/usr/bin/env python3
# +===================================================================
#   File:      mac-link-reference.py
#
#   Summary:   Original LINK instruction comparisons through classic Mac Mixed Mode.
#
#   Classes:   None declared.
#
#   Functions: vectors, generate, check
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Original LINK instruction comparisons through classic Mac Mixed Mode.

The PowerPC wrapper calls original 68K machine code. These are Apple emulator
comparisons on PowerPC Macs, not physical MC68040 bus/FPU measurements.
"""

import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    return [
        (wide, reg, displacement, ccr)
        for wide in (0, 1)
        for reg in range(7)
        # Keep the frame above SP so asynchronous Mac interrupts cannot
        # overwrite it before the snapshot. Isolated CPU tests cover positives.
        for displacement in (-256, -128, -64, -32, -16, -8, -4, -2, 0)
        for ccr in range(32)
    ]


def generate(directory):
    # Save the 68K C ABI's nonvolatile registers; the output pointer is the
    # sole C-stack argument, above the saved registers and return address.
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for wide, reg, displacement, ccr in cases:
        words.extend([0x207C | (reg << 9), 0x1234, 0x5678, 0x2C0F, 0x44FC, ccr])
        words.append((0x4808 if wide else 0x4E50) | reg)
        if wide:
            words.append((displacement >> 16) & 0xFFFF)
        words.append(displacement & 0xFFFF)
        words.extend(
            [
                0x42C0,  # move.w ccr,d0, before any flag-changing work.
                0x220F,  # move.l sp,d1.
                0x2608 | reg,  # move.l An,d3.
                0x2810 | reg,  # move.l (An),d4.
                0x2E46,  # movea.l d6,sp: restore the actual caller stack.
                0x9286,  # sub.l d6,d1: normalized SP.
                0x9686,  # sub.l d6,d3: normalized frame pointer.
                0x0280,
                0,
                31,  # andi.l #31,d0.
                0x2247,  # movea.l d7,a1: dedicated output storage.
                0x22C0,
                0x22C3,
                0x22C1,
                0x22C4,
                0x2E09,  # move.l a1,d7: advance output pointer.
                0x6002,  # Bound each translated block independently of case count.
                0x4E71,
            ]
        )
    words.extend([0x4CDF, 0x7CFC, 0x4E75])
    body = [
        0x7CC33378,  # mr r3,r6: raw 68K procedure address.
        0x388000C1,  # C-stack, void result, one four-byte argument.
        0x7CE53B78,  # mr r5,r7: output pointer.
        0x819E0024,  # CallUniversalProc, appended import slot 7.
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
    ]
    generate_classic(
        directory,
        cases,
        body,
        application_name="68K LINK Comparison",
        result_name="68K LINK Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K LINK",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong LINK result length")
    for index, (_, _, displacement, ccr) in enumerate(cases):
        actual = struct.unpack_from(">4I", data, index * 16)
        expected = (ccr, 0xFFFFFFFC, (displacement - 4) & 0xFFFFFFFF, 0x12345678)
        if actual != expected:
            raise ValueError(f"LINK case {cases[index]}: {actual} != {expected}")
    print(f"{len(cases)} classic 68K LINK values and CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
