# +===================================================================
#   File:      rounding-status-reference.py
#
#   Summary:   Independent Fraction model for PowerPC ordinary FPSCR and scaled results.
#
#   Classes:   None declared.
#
#   Functions: unbounded, classify, generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Independent Fraction model for PowerPC ordinary FPSCR and scaled results."""

from fractions import Fraction as Q
from pathlib import Path
import runpy
import sys

ref = runpy.run_path(str(Path(__file__).with_name("fused-reference.py")))
decode, power, rounded = (ref[k] for k in ("decode", "power", "rounded"))


def unbounded(value, mode, precision):
    if not value:
        return Q(0)
    negative, magnitude = value < 0, abs(value)
    exponent = magnitude.numerator.bit_length() - magnitude.denominator.bit_length()
    if magnitude < power(exponent):
        exponent -= 1
    quantum = power(exponent - precision + 1)
    scaled = magnitude / quantum
    integer, remainder = divmod(scaled.numerator, scaled.denominator)
    increment = (
        remainder * 2 > scaled.denominator or (remainder * 2 == scaled.denominator and integer % 2)
        if mode == 0
        else bool(remainder) and ((mode == 2 and not negative) or (mode == 3 and negative))
    )
    return (-1 if negative else 1) * (integer + increment) * quantum


def classify(bits, single, scaled):
    magnitude = bits & 0x7FFFFFFFFFFFFFFF
    negative = bits >> 63
    if magnitude == 0x7FF0000000000000:
        return 0x9000 if negative else 0x5000
    if not magnitude:
        return 0x12000 if negative else 0x2000
    minimum = 0x3810000000000000 if single and not scaled else 0x0010000000000000
    return (0x8000 if negative else 0x4000) | (0x10000 if magnitude < minimum else 0)


def generate(source, destination):
    count = 0
    with open(destination, "w", encoding="ascii") as output:
        for line in Path(source).read_text().splitlines():
            op, single, mode, a, b, result, flags = line.split()
            op, single, mode, flags = map(int, (op, single, mode, flags))
            a, b, result = (int(x, 16) for x in (a, b, result))
            e, f = (8, 23) if single and op != 5 else (11, 52)
            if flags & 24 or ((a >> f) & ((1 << e) - 1)) == (1 << e) - 1:
                continue
            if op < 4 and ((b >> f) & ((1 << e) - 1)) == (1 << e) - 1:
                continue
            first, second = decode(a, e, f), decode(b, e, f)
            source_a = rounded(first, bool(a >> (e + f)), 0, 11, 52)
            source_b = rounded(second, bool(b >> (e + f)), 0, 11, 52)
            result_e, result_f = (8, 23) if single else (11, 52)
            result_sign = bool(result >> (result_e + result_f))
            result_magnitude = result & ((1 << (result_e + result_f)) - 1)
            if result_magnitude == ((1 << result_e) - 1) << result_f:
                expected = 0x7FF0000000000000 | (int(result_sign) << 63)
            else:
                expected = rounded(decode(result, result_e, result_f), result_sign, 0, 11, 52)
            if op != 4:
                value = (
                    first + second
                    if op == 0
                    else (
                        first - second
                        if op == 1
                        else (first * second if op == 2 else first / second if op == 3 else first)
                    )
                )
                wide = unbounded(value, mode, 24 if single else 53)
                truncated = unbounded(value, 1, 24 if single else 53)
            for enables in (0, 0x40, 0x20, 8, 0x68):
                tiny = False
                if op != 4:
                    tiny = bool(value) and abs(value) < power(-126 if single else -1022)
                scaled = bool((flags & 4 and enables & 0x40) or (tiny and enables & 0x20))
                raised = (0x10000000 if flags & 4 else 0) | (
                    0x08000000 if (tiny and enables & 0x20) or flags & 2 else 0
                )
                bits = expected
                if scaled:
                    adjustment = (-1 if flags & 4 else 1) * (192 if single else 1536)
                    bits = rounded(wide * power(adjustment), False, 0, 11, 52)
                    inexact = wide != value
                    incremented = abs(wide) > abs(truncated)
                else:
                    inexact = bool(flags & 1)
                    if op == 4:
                        y = decode(expected, 11, 52)
                        incremented = y * y > first
                    elif flags & 4:
                        incremented = False  # FR is architecturally undefined.
                    else:
                        y = decode(expected, 11, 52)
                        incremented = abs(y) > abs(value)
                if inexact:
                    raised |= 0x02000000
                old = (0, 0x10000000, 0x02000000)[count % 3]
                initial = mode | enables | old | 0x70000
                status = mode | enables | old | raised | classify(bits, single, scaled)
                if raised & ~old:
                    status |= 0x80000000
                if (status >> 25) & (status >> 3) & 31:
                    status |= 0x40000000
                if inexact:
                    status |= 0x20000
                if incremented:
                    status |= 0x40000
                mask = 0xFFFFFFFF ^ (0x40000 if flags & 4 and not scaled else 0)
                output.write(f"{op} {single} {source_a:x} {source_b:x} {initial:x} {bits:x} {status:x} {mask:x}\n")
                count += 1
    print(f"Generated {count} independent PowerPC rounding/status cases")


if __name__ == "__main__":
    generate(*sys.argv[1:])
