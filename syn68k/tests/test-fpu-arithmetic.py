#!/usr/bin/env python3
# +===================================================================
#   File:      test-fpu-arithmetic.py
#
#   Summary:   Check normalized 040 arithmetic with exact integers and integer square root.
#
#   Classes:   None declared.
#
#   Functions: scaled, expected, main
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Check normalized 040 arithmetic with exact integers and integer square root."""

from fractions import Fraction
from math import isqrt
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def scaled(significand, exponent):
    return Fraction(significand << exponent) if exponent >= 0 else Fraction(significand, 1 << -exponent)


def expected(ah, a, bh, b, precision, mode, operation):
    ae = (ah & 0x7FFF) - (0x8000 if ah & 0x10000 else 0)
    be = (bh & 0x7FFF) - (0x8000 if bh & 0x10000 else 0)
    width = {32: 24, 64: 53, 80: 64}[precision]
    if operation in (0x24, 0x27):
        a &= 0xFFFFFF0000000000
        b &= 0xFFFFFF0000000000
        width = 24
    if operation == 4:
        if not b:
            return (bh & 0x8000, 0, 0, 0, 0, 0, 0, 0, 0)
        unbiased = be - 0x3FFF
        exponent = unbiased // 2 + 0x3FFF
        radicand = b << (67 + (unbiased % 2))
        root = isqrt(radicand)
        sign = 0
        intermediate, grs = divmod(root, 4)
        sticky = int(root * root != radicand)
        rounded, tail = divmod(root, 1 << (66 - width))
        half = 1 << (65 - width)
        inexact = tail != 0 or sticky
        above_half = tail > half or (tail == half and sticky)
        equal_half = tail == half and not sticky
    else:
        x = scaled(a * (-1 if ah & 0x8000 else 1), ae - 0x3FFF - 63)
        y = scaled(b * (-1 if bh & 0x8000 else 1), be - 0x3FFF - 63)
        if operation == 0x22:
            value = x + y
        elif operation == 0x28:
            value = x - y
        elif operation in (0x23, 0x27):
            value = x * y
        elif operation in (0x20, 0x24):
            value = x / y
        elif operation == 0x18:
            value = abs(y)
        elif operation == 0x1A:
            value = -y
        else:
            value = y
        if not value:
            sign = 0x8000 if mode == 2 else 0
            # Same-sign zero sums and multiplication retain the operand sign.
            if operation in (0x20, 0x23, 0x24, 0x27):
                sign = (ah ^ bh) & 0x8000
            elif operation not in (0x22, 0x28):
                sign = (bh & 0x8000) ^ (0x8000 if operation == 0x1A else 0)
                if operation == 0x18:
                    sign = 0
            elif (ah & 0x8000) == ((bh ^ (0x8000 if operation == 0x28 else 0)) & 0x8000):
                sign = ah & 0x8000
            return (sign, 0, 0, 0, 0, 0, 0, 0, 0)
        sign = 0x8000 if value < 0 else 0
        value = abs(value)
        n, d = value.numerator, value.denominator
        e = n.bit_length() - d.bit_length()
        if scaled(1, e) > value:
            e -= 1
        exponent = e + 0x3FFF
        value /= scaled(1, e)
        n, d = value.numerator, value.denominator
        rounded, remainder = divmod(n << (width - 1), d)
        inexact = remainder != 0
        above_half = 2 * remainder > d
        equal_half = 2 * remainder == d
        intermediate, remainder = divmod(n << 63, d)
        grs, remainder = divmod(remainder * 4, d)
        sticky = int(remainder != 0)
    increment = (
        above_half or (equal_half and rounded & 1)
        if mode == 0
        else inexact and ((mode == 2 and sign) or (mode == 3 and not sign))
    )
    rounded += bool(increment)
    rounded_exponent = exponent
    if rounded == 1 << width:
        rounded >>= 1
        rounded_exponent += 1
    rounded <<= 64 - width
    minimum = {32: 0x3F81, 64: 0x3C01, 80: 0}[precision]
    maximum = {32: 0x407E, 64: 0x43FE, 80: 0x7FFE}[precision]
    if operation in (0x24, 0x27):
        minimum, maximum = 0, 0x7FFE
    status = 0x800 if exponent < minimum else 0x1000 if rounded_exponent > maximum else 0
    if status == 0x1000:
        exponent, intermediate, grs, sticky = rounded_exponent, rounded, 0, 0
    return (
        sign | (rounded_exponent & 0x7FFF),
        rounded,
        sign | (exponent & 0x7FFF),
        intermediate,
        status,
        grs,
        sticky,
        (exponent >> 15) & 1,
        0x20 if inexact else 0,
    )


def main():
    output = ROOT / "syn68k/obj/tests/fpu"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "arithmetic-integer-test"
    subprocess.run(
        [
            "gcc",
            "-std=gnu17",
            "-g",
            "-O2",
            "-fwrapv",
            "-Isyn68k/src/runtime/include",
            "syn68k/tests/fpu-arithmetic-test.c",
            "syn68k/third_party/softfloat/softfloat.c",
            "-o",
            str(binary),
        ],
        cwd=ROOT,
        check=True,
    )
    rng = random.Random(68040)
    cases = []
    for op in (0, 4, 0x18, 0x1A, 0x20, 0x22, 0x23, 0x24, 0x27, 0x28):
        for precision in (32, 64, 80):
            for mode in range(4):
                for test in range(110):
                    ah = rng.randrange(0x7FFF) | rng.choice((0, 0x8000))
                    bh = rng.randrange(0x7FFF) | (0 if op == 4 else rng.choice((0, 0x8000)))
                    if 20 <= test < 40:
                        ah = (ah & 0x8000) | 0x10000 | rng.randrange(0x7FC0, 0x8000)
                    if 30 <= test < 50:
                        bh = (bh & 0x8000) | 0x10000 | rng.randrange(0x7FC0, 0x8000)
                    a = rng.getrandbits(63) | (1 << 63)
                    b = rng.getrandbits(63) | (1 << 63)
                    if test < 20:
                        bh = (ah & 0x7FFF) | (bh & 0x8000)
                    if test < 10:
                        b = a
                    if test == 0:
                        a = 0
                    if test == 1 and op not in (0x20, 0x24):
                        b = 0
                    cases.append((ah, a, bh, b, precision, mode, op))
    data = "".join(f"{ah:x} {a:x} {bh:x} {b:x} {p} {m} {op:x}\n" for ah, a, bh, b, p, m, op in cases)
    result = subprocess.run([str(binary)], input=data, text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases)
    for case, line in zip(cases, lines):
        actual = tuple(int(word, 16 if i < 4 else 10) for i, word in enumerate(line.split()))
        reference = expected(*case)
        assert actual == reference, (case, actual, reference)
    print(f"{len(cases)} exact-integer arithmetic/writeback cases passed.")


if __name__ == "__main__":
    main()
