# +===================================================================
#   File:      fpsp-reference.py
#
#   Summary:   Independent Decimal reference values for nontrivial FPSP operations.
#
#   Classes:   None declared.
#
#   Functions: atan, trig, extended, main
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Independent Decimal reference values for nontrivial FPSP operations."""

from decimal import Decimal as D, localcontext
from pathlib import Path
import sys


def atan(x):
    term = total = x
    n = 1
    while True:
        term *= -x * x
        n += 2
        updated = total + term / n
        if updated == total:
            return total
        total = updated


def trig(x, cosine=False):
    term = total = D(1) if cosine else x
    n = 0 if cosine else 1
    while True:
        term *= -x * x / ((n + 1) * (n + 2))
        n += 2
        updated = total + term
        if updated == total:
            return total
        total = updated


def extended(value):
    sign = 0x8000 if value < 0 else 0
    value = abs(value)
    if not value:
        return sign, 0
    numerator, denominator = value.as_integer_ratio()
    exponent = numerator.bit_length() - denominator.bit_length()
    below = numerator < denominator << exponent if exponent >= 0 else numerator << -exponent < denominator
    if below:
        exponent -= 1
    shift = 63 - exponent
    if shift >= 0:
        numerator <<= shift
    else:
        denominator <<= -shift
    significand, remainder = divmod(numerator, denominator)
    significand += remainder * 2 > denominator or (remainder * 2 == denominator and significand & 1)
    if significand == 1 << 64:
        significand >>= 1
        exponent += 1
    return sign + exponent + 0x3FFF, significand


def main():
    with localcontext() as context:
        context.prec = 120
        pi = 16 * atan(D(1) / 5) - 4 * atan(D(1) / 239)
        half, one, two = D("0.5"), D(1), D(2)
        vectors = [
            (0x02, half, (half.exp() - (-half).exp()) / 2),
            (0x06, half, (1 + half).ln()),
            (0x08, half, half.exp() - 1),
            (0x09, half, (half.exp() - (-half).exp()) / (half.exp() + (-half).exp())),
            (0x0A, half, atan(half)),
            (0x0C, half, pi / 6),
            (0x0D, half, ((1 + half) / (1 - half)).ln() / 2),
            (0x0E, one, trig(one)),
            (0x0F, one, trig(one) / trig(one, True)),
            (0x10, one, one.exp()),
            (0x11, half, two.sqrt()),
            (0x12, half, D(10).sqrt()),
            (0x14, two, two.ln()),
            (0x15, two, two.log10()),
            (0x16, D("1.5"), D("1.5").ln() / two.ln()),
            (0x19, one, (one.exp() + (-one).exp()) / 2),
            (0x1C, half, pi / 3),
            (0x1D, one, trig(one, True)),
        ]
        lines = [
            "/* Generated from independent 120-digit Decimal calculations. */",
            "static const struct { unsigned operation, input_exponent;",
            "  uint64 input_significand; unsigned exponent; uint64 significand;",
            "} fpsp_reference[] = {",
        ]
        for operation, argument, result in vectors:
            exponent, significand = extended(argument)
            result_exponent, result_significand = extended(result)
            lines.append(
                f"  {{ 0x{operation:02x}, 0x{exponent:04x}, "
                f"UINT64_C (0x{significand:016x}), 0x{result_exponent:04x}, "
                f"UINT64_C (0x{result_significand:016x}) }},"
            )
        lines.append("};\n")
        Path(sys.argv[1]).write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
