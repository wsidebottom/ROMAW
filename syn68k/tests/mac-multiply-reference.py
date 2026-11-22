#!/usr/bin/env python3
# +===================================================================
#   File:      mac-multiply-reference.py
#
#   Summary:   Original long multiplication probe for Apple's PowerPC 68K emulator.
#
#   Classes:   None declared.
#
#   Functions: vectors, initial, expected, generate, check
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Original long multiplication probe for Apple's PowerPC 68K emulator.

Undefined size=1 Dh==Dl results are excluded from hardware comparisons.
"""

import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    values = [
        (0, 0, 1),
        (1, 0, 2),
        (0xFFFFFFFF, 0, 3),
        (0x80000000, 0, 0xFFFFFFFF),
        (0, 0x80000000, 0xFFFFFFFF),
        (0xFFFFFFFF, 0x7FFFFFFF, 1),
        (0x7FFFFFFF, 0xFFFFFFFF, 0x80000000),
        (0x80000001, 0xFFFFFFFF, 16),
        (0x55555555, 0xAAAAAAAA, 0xFFFFFFF0),
        (0, 1, 2),
        (0, 0xFFFFFFFF, 0x80000000),
    ]
    return [
        (kind, q, r, source, low, high, divisor, ccr)
        for kind in range(4)
        for q, r, source in [(0, 1, 2), (0, 0, 2), (1, 0, 1), (0, 1, 1)]
        for low, high, divisor in values
        if not (kind & 1 and q == r)
        for ccr in range(32)
    ]


def initial(case):
    _, q, r, source, low, high, divisor, _ = case
    registers = [0, 0x11223344, 0x22334488]
    registers[q] = low
    if r != q:
        registers[r] = high
    registers[source] = divisor
    return registers


def expected(case):
    kind, low, high, source, _, _, _, ccr = case
    registers = initial(case)
    multiplicand = registers[low]
    multiplier = registers[source]
    if kind & 2:
        if multiplicand & 0x80000000:
            multiplicand -= 1 << 32
        if multiplier & 0x80000000:
            multiplier -= 1 << 32
    product = multiplicand * multiplier
    result = product & 0xFFFFFFFF
    wide = bool(kind & 1)
    overflow = not wide and not (-(1 << 31) <= product < (1 << 31) if kind & 2 else 0 <= product < (1 << 32))
    registers[low] = result
    if wide:
        registers[high] = (product >> 32) & 0xFFFFFFFF
    negative = bool(product & (1 << 63)) if wide else bool(result & 0x80000000)
    zero = product == 0 if wide else result == 0
    flags = (ccr & 16) | (8 if negative else 0) | (4 if zero else 0) | (2 if overflow else 0)
    return (*registers, flags), 31


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for case in cases:
        kind, q, r, source, _, _, _, ccr = case
        for register, value in enumerate(initial(case)):
            words.extend([0x203C | (register << 9), value >> 16, value & 65535])
        words.extend(
            [
                0x44FC,
                ccr,
                0x4C00 | source,
                (q << 12) | (kind << 10) | r,
                0x42C3,  # move.w ccr,d3, before any flag-changing work.
                0x0283,
                0,
                31,
                0x2247,  # movea.l d7,a1: output pointer.
                0x22C0,
                0x22C1,
                0x22C2,
                0x22C3,
                0x2E09,  # move.l a1,d7: advance output pointer.
                0x6002,
                0x4E71,
            ]
        )
    words.extend([0x4CDF, 0x7CFC, 0x4E75])
    body = [
        0x7CC33378,
        0x388000C1,
        0x7CE53B78,
        0x819E0024,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
    ]
    generate_classic(
        directory,
        cases,
        body,
        application_name="68K Multiply Comparison",
        result_name="68K Multiply Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K long multiplication",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong multiply result length")
    for index, case in enumerate(cases):
        actual = struct.unpack_from(">4I", data, index * 16)
        result, mask = expected(case)
        if actual[:3] != result[:3] or actual[3] & mask != result[3]:
            raise ValueError(f"Multiply case {case}: {actual} != {result}, mask {mask}")
    print(f"{len(cases)} classic long multiply values and defined CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
