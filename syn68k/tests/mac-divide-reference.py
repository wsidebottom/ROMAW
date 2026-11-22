#!/usr/bin/env python3
# +===================================================================
#   File:      mac-divide-reference.py
#
#   Summary:   Original long division probe for Apple's PowerPC 68K emulator.
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

"""Original long division probe for Apple's PowerPC 68K emulator.

Only nonzero divisors are used; overflow does not take an OS exception.
Undefined N/Z flags on overflow are recorded but excluded from comparison.
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
    kind, q, r, source, _, _, _, ccr = case
    registers = initial(case)
    signed = bool(kind & 2)
    wide = bool(kind & 1)
    dividend = registers[q]
    if wide:
        dividend |= registers[r] << 32
    divisor = registers[source]
    if signed:
        signbit = 1 << (63 if wide else 31)
        if dividend & signbit:
            dividend -= signbit * 2
        if divisor & 0x80000000:
            divisor -= 1 << 32
    # Python arbitrary precision is independent of the runtime's C arithmetic.
    quotient = abs(dividend) // abs(divisor)
    if (dividend < 0) != (divisor < 0):
        quotient = -quotient
    remainder = dividend - quotient * divisor
    overflow = not (-(1 << 31) <= quotient < (1 << 31) if signed else 0 <= quotient < (1 << 32))
    flags = ccr & 16
    if overflow:
        flags |= 2
    else:
        if r != q:
            registers[r] = remainder & 0xFFFFFFFF
        registers[q] = quotient & 0xFFFFFFFF
        flags |= (8 if quotient < 0 or quotient & 0x80000000 else 0) | (4 if quotient == 0 else 0)
    return (*registers, flags), (0x13 if overflow else 0x1F)


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
                0x4C40 | source,
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
        application_name="68K Divide Comparison",
        result_name="68K Divide Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K long division",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong divide result length")
    for index, case in enumerate(cases):
        actual = struct.unpack_from(">4I", data, index * 16)
        result, mask = expected(case)
        if actual[:3] != result[:3] or actual[3] & mask != result[3]:
            raise ValueError(f"Divide case {case}: {actual} != {result}, mask {mask}")
    print(f"{len(cases)} classic long divide values and defined CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
