# +===================================================================
#   File:      fused-reference.py
#
#   Summary:   Independent Fraction reference for IEEE fused a*b+c, all rounding modes.
#
#   Classes:   None declared.
#
#   Functions: power, decode, rounded, generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Independent Fraction reference for IEEE fused a*b+c, all rounding modes."""

from fractions import Fraction as Q
from pathlib import Path
import random
import sys


def power(exponent):
    return Q(1 << exponent) if exponent >= 0 else Q(1, 1 << -exponent)


def decode(bits, exponent_bits, fraction_bits):
    bias = (1 << (exponent_bits - 1)) - 1
    exponent = (bits >> fraction_bits) & ((1 << exponent_bits) - 1)
    mantissa = bits & ((1 << fraction_bits) - 1)
    if exponent:
        mantissa += 1 << fraction_bits
    value = mantissa * power((exponent or 1) - bias - fraction_bits)
    return -value if bits >> (exponent_bits + fraction_bits) else value


def rounded(value, negative_zero, mode, e, f):
    bias = (1 << (e - 1)) - 1
    infinity = ((1 << e) - 1) << f
    negative = value < 0 if value else negative_zero
    sign = int(negative) << (e + f)
    value = abs(value)
    if not value:
        return sign
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    if value < power(exponent):
        exponent -= 1
    quantum = max(exponent - f, 1 - bias - f)
    scaled = value / power(quantum)
    integer, remainder = divmod(scaled.numerator, scaled.denominator)
    increment = (
        remainder * 2 > scaled.denominator or (remainder * 2 == scaled.denominator and integer % 2)
        if mode == 0
        else bool(remainder) and ((mode == 2 and not negative) or (mode == 3 and negative))
    )
    integer += increment
    if integer >= 1 << (f + 1):
        integer >>= 1
        exponent += 1
    if exponent > bias:
        infinite = mode == 0 or (mode == 2 and not negative) or (mode == 3 and negative)
        return sign | (infinity if infinite else infinity - 1)
    if integer < 1 << f:
        return sign | integer
    exponent = max(exponent, 1 - bias)
    return sign | ((exponent + bias) << f) | (integer - (1 << f))


def generate(destination):
    rng = random.Random(0xF053D)
    cases = []
    for single, e, f in [(1, 8, 23), (0, 11, 52)]:
        maximum = (1 << e) - 1
        bias = (1 << (e - 1)) - 1
        sign = 1 << (e + f)
        infinity = maximum << f

        def operand():
            exponent = (
                rng.choice([0, 1, 2, bias - 1, bias, bias + 1, maximum - 2, maximum - 1])
                if rng.randrange(2)
                else rng.randrange(maximum)
            )
            fraction = rng.choice([0, 1, (1 << f) - 1, 1 << (f - 1)]) if rng.randrange(2) else rng.getrandbits(f)
            return rng.randrange(2) * sign | exponent << f | fraction

        triples = [(operand(), operand(), operand()) for _ in range(1600)]
        for _ in range(400):
            a, b = operand(), operand()
            product = decode(a, e, f) * decode(b, e, f)
            nearest = rounded(-product, False, 0, e, f)
            if (nearest & ~sign) < infinity:
                triples.append((a, b, nearest))
        for a in [0, sign]:
            for b in [bias << f, sign | bias << f]:
                for c in [0, sign]:
                    triples.append((a, b, c))
        for a, b, c in triples:
            exact = decode(a, e, f) * decode(b, e, f) + decode(c, e, f)
            product_sign = bool((a ^ b) & sign)
            addend_sign = bool(c & sign)
            for mode in range(4):
                zero_sign = product_sign if product_sign == addend_sign else mode == 3
                expected = rounded(exact, zero_sign, mode, e, f)
                cases.append((single, mode, a, b, c, expected))
        # NaN preference and quieting use the architecture's operand order.
        quiet = 1 << (f - 1)
        nans = [sign | infinity | quiet | 1, infinity | 2, infinity | quiet | 3]
        for mask in range(1, 8):
            operands = [nans[i] if mask & (1 << i) else bias << f for i in range(3)]
            preferred = next(i for i in [0, 2, 1] if mask & (1 << i))
            expected = nans[preferred] | quiet
            cases.append((single, 0, *operands, expected))
        canonical = infinity | quiet
        cases += [
            (single, 0, infinity, 0, 0, canonical),
            (single, 0, infinity, bias << f, sign | infinity, canonical),
            (single, 0, infinity, bias << f, 0, infinity),
        ]
    Path(destination).write_text(
        "".join(f"{s} {m} {a:x} {b:x} {c:x} {r:x}\n" for s, m, a, b, c, r in cases),
        encoding="ascii",
    )
    print(f"Generated {len(cases)} independent rational fused cases")


if __name__ == "__main__":
    generate(sys.argv[1])
