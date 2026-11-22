# +===================================================================
#   File:      integer-reference.py
#
#   Summary:   Independent rational reference for PowerPC signed-word conversion.
#
#   Classes:   None declared.
#
#   Functions: expected, generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Independent rational reference for PowerPC signed-word conversion."""

from fractions import Fraction
from pathlib import Path
import random
import runpy
import struct
import sys

decode = runpy.run_path(str(Path(__file__).with_name("fused-reference.py")))["decode"]


def expected(bits, mode):
    exponent = (bits >> 52) & 2047
    fraction = bits & ((1 << 52) - 1)
    if exponent == 2047:
        result = -2147483648 if fraction or bits >> 63 else 2147483647
        status = 0xA0000100
        if fraction and not fraction & (1 << 51):
            status |= 0x01000000
        return result & 0xFFFFFFFF, 16, 0, status | mode
    value = decode(bits, 11, 52)
    lower = value.numerator // value.denominator
    remainder = value - lower
    if mode == 0:
        result = lower + (remainder > Fraction(1, 2) or (remainder == Fraction(1, 2) and lower % 2))
    elif mode == 1:
        result = int(value)
    elif mode == 2:
        result = lower + bool(remainder)
    else:
        result = lower
    if not -2147483648 <= result <= 2147483647:
        return (0x80000000 if value < 0 else 0x7FFFFFFF), 16, 0, 0xA0000100 | mode
    inexact = value != result
    incremented = abs(result) > abs(int(value))
    status = (0x82020000 if inexact else 0) | (0x40000 if incremented else 0)
    return result & 0xFFFFFFFF, int(inexact), int(incremented), status | mode


def generate(destination):
    inputs = [0, 1, (1 << 52) - 1, 1 << 52, 0x7FEFFFFFFFFFFFFF]
    inputs += [0x7FF0000000000000 | n for n in (0, 1, 0x7FFFFFFFFFFFF, 0x8000000000000, 0x8000000000123)]
    for integer in (0, 1, 2, 3, 2147483646, 2147483647, 2147483648, 2147483649):
        for fraction in (-0.75, -0.5, -0.25, 0, 0.25, 0.5, 0.75):
            bits = struct.unpack(">Q", struct.pack(">d", integer + fraction))[0]
            inputs += [bits, max(bits - 1, 0), bits + 1]
    inputs += [bits ^ (1 << 63) for bits in inputs]
    rng = random.Random(0xFC710)
    inputs += [rng.getrandbits(64) for _ in range(900)]
    inputs += [(rng.randrange(2) << 63) | (rng.randrange(1020, 1057) << 52) | rng.getrandbits(52) for _ in range(900)]
    cases = []
    for bits in inputs:
        for mode in range(4):
            result, flags, incremented, status = expected(bits, mode)
            cases.append(f"{mode} {bits:x} {result:x} {flags} {incremented} {status:x}\n")
    Path(destination).write_text("".join(cases), encoding="ascii")
    print(f"Generated {len(cases)} rational integer-conversion cases")


if __name__ == "__main__":
    generate(sys.argv[1])
