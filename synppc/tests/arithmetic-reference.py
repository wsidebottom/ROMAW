# +===================================================================
#   File:      arithmetic-reference.py
#
#   Summary:   Exact rational IEEE arithmetic, including an integer square-root oracle.
#
#   Classes:   None declared.
#
#   Functions: square_root, finite_result, generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Exact rational IEEE arithmetic, including an integer square-root oracle."""

from fractions import Fraction as Q
from math import isqrt
from pathlib import Path
import random
import runpy
import sys

reference = runpy.run_path(str(Path(__file__).with_name("fused-reference.py")))
decode, power, rounded = (reference[name] for name in ("decode", "power", "rounded"))


def square_root(value, mode, e, f):
    if not value:
        return 0, 0
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if value < power(exponent):
        exponent -= 1
    quantum = exponent // 2 - f
    scaled = value / power(2 * quantum)
    integer = isqrt(scaled.numerator // scaled.denominator)
    inexact = integer * integer * scaled.denominator != scaled.numerator
    midpoint = 4 * scaled.numerator - (2 * integer + 1) ** 2 * scaled.denominator
    increment = midpoint > 0 or (midpoint == 0 and integer % 2) if mode == 0 else inexact and mode == 2
    result = rounded((integer + increment) * power(quantum), False, 0, e, f)
    return result, int(inexact)


def finite_result(value, zero_sign, mode, e, f):
    result = rounded(value, zero_sign, mode, e, f)
    infinity = ((1 << e) - 1) << f
    magnitude = result & ((1 << (e + f)) - 1)
    bias = (1 << (e - 1)) - 1
    overflow = abs(value) >= power(bias + 1) or magnitude == infinity
    inexact = overflow or decode(result, e, f) != value
    tiny = bool(value) and abs(value) < power(1 - bias)
    return result, int(inexact) | (2 if tiny and inexact else 0) | (4 if overflow else 0)


def generate(destination):
    rng = random.Random(0x750F040)
    cases = []
    for single, e, f in [(1, 8, 23), (0, 11, 52)]:
        bias = (1 << (e - 1)) - 1
        maximum = (1 << e) - 1
        sign = 1 << (e + f)
        infinity = maximum << f
        quiet = 1 << (f - 1)
        edges = [0, 1, (1 << f) - 1, 1 << f, bias << f, infinity - 1]
        edges += [value | sign for value in edges]
        pairs = [(a, b) for a in edges for b in edges]
        for _ in range(700):
            operands = []
            for _ in range(2):
                exponent = rng.choice([0, 1, bias, maximum - 1, rng.randrange(maximum)])
                operands.append(rng.randrange(2) * sign | exponent << f | rng.getrandbits(f))
            pairs.append(tuple(operands))
        for operation in range(5):
            for a, b in pairs:
                first, second = decode(a, e, f), decode(b, e, f)
                for mode in range(4):
                    if operation == 4:
                        if first < 0:
                            expected, flags = infinity | quiet, 16
                        else:
                            expected, flags = square_root(first, mode, e, f)
                            if not first:
                                expected = a
                    elif operation == 3 and not second:
                        expected, flags = (infinity | quiet, 16) if not first else (infinity | ((a ^ b) & sign), 8)
                    else:
                        value = (
                            first + second
                            if operation == 0
                            else (
                                first - second
                                if operation == 1
                                else (first * second if operation == 2 else first / second)
                            )
                        )
                        a_sign, b_sign = bool(a & sign), bool(b & sign)
                        if operation < 2:
                            b_sign ^= operation == 1
                            zero_sign = a_sign if a_sign == b_sign else mode == 3
                        else:
                            zero_sign = a_sign != b_sign
                        expected, flags = finite_result(value, zero_sign, mode, e, f)
                    cases.append((operation, single, mode, a, b, expected, flags))
            # Signaling status must not override the architectural NaN preference.
            for a, b in [
                (infinity | quiet | 3, sign | infinity | 5),
                (sign | infinity | 7, infinity | quiet | 9),
            ]:
                invalid = 16 if operation != 4 or not (a & quiet) else 0
                cases.append((operation, single, 0, a, b, a | quiet, invalid))
        special = [0, sign, bias << f, sign | (bias << f), infinity, sign | infinity]
        for operation in range(5):
            for a in special:
                for b in special:
                    inf_a, inf_b = (a & ~sign) == infinity, (b & ~sign) == infinity
                    if not inf_a and (operation == 4 or not inf_b):
                        continue
                    negative = bool((a ^ b) & sign)
                    invalid = False
                    if operation < 2:
                        invalid = inf_a and inf_b and (bool(a & sign) != (bool(b & sign) ^ (operation == 1)))
                        expected = a if inf_a else b ^ (sign if operation == 1 else 0)
                    elif operation == 2:
                        invalid = (inf_a and not (b & ~sign)) or (inf_b and not (a & ~sign))
                        expected = infinity | (sign if negative else 0)
                    elif operation == 3:
                        invalid = inf_a and inf_b
                        expected = (infinity if inf_a else 0) | (sign if negative else 0)
                    else:
                        invalid = bool(a & sign)
                        expected = infinity
                    if invalid:
                        expected = infinity | quiet
                    cases.append((operation, single, 0, a, b, expected, 16 if invalid else 0))
        # FRSP accepts arbitrary binary64 operands and rounds directly to binary32.
    for _ in range(1800):
        bits = rng.getrandbits(64)
        if bits & 0x7FF0000000000000 == 0x7FF0000000000000:
            continue
        exact = decode(bits, 11, 52)
        for mode in range(4):
            expected, flags = finite_result(exact, bool(bits >> 63), mode, 8, 23)
            cases.append((5, 1, mode, bits, 0, expected, flags))
    for negative in (0, 1):
        for fraction in (0, 1, 0x8000000000000, 0x8000020000000):
            a = (negative << 63) | 0x7FF0000000000000 | fraction
            expected = (negative << 31) | 0x7F800000
            if fraction:
                expected |= 0x400000 | (fraction >> 29)
            flags = 16 if fraction and not (fraction & 0x8000000000000) else 0
            cases.append((5, 1, 0, a, 0, expected, flags))
    Path(destination).write_text(
        "".join(f"{op} {s} {m} {a:x} {b:x} {r:x} {flags}\n" for op, s, m, a, b, r, flags in cases),
        encoding="ascii",
    )
    print(f"Generated {len(cases)} rational arithmetic and integer-root cases")


if __name__ == "__main__":
    generate(sys.argv[1])
