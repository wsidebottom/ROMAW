# +===================================================================
#   File:      fused-status-reference.py
#
#   Summary:   Exact rational PowerPC fused values, rounding status and scaled results.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Exact rational PowerPC fused values, rounding status and scaled results."""

from pathlib import Path
import runpy
import sys

ref = runpy.run_path(str(Path(__file__).with_name("rounding-status-reference.py")))
decode, power, rounded, unbounded, classify = (ref[k] for k in ("decode", "power", "rounded", "unbounded", "classify"))


def generate(source, destination):
    count = 0
    with open(destination, "w", encoding="ascii") as output:
        for line in Path(source).read_text().splitlines():
            single, mode, a, c, b, _ = line.split()
            single, mode = int(single), int(mode)
            a, b, c = (int(x, 16) for x in (a, b, c))
            e, f = (8, 23) if single else (11, 52)
            infinity = ((1 << e) - 1) << f
            sign = 1 << (e + f)
            if any((x & ~sign) >= infinity for x in (a, b, c)):
                continue
            first, second, third = (decode(x, e, f) for x in (a, b, c))
            sources = [rounded(v, bool(x & sign), 0, 11, 52) for v, x in zip((first, second, third), (a, b, c))]
            for operation in range(28, 32):
                addend = second if operation & 1 else -second
                value = first * third + addend
                negative_product = bool((a ^ c) & sign)
                negative_addend = bool(b & sign) ^ (not (operation & 1))
                zero_sign = negative_product if negative_product == negative_addend else mode == 3
                wide = unbounded(value, mode, f + 1)
                truncated = unbounded(value, 1, f + 1)
                maximum = decode(infinity - 1, e, f)
                overflow = abs(wide) > maximum
                tiny = bool(value) and abs(value) < power(2 - (1 << (e - 1)))
                for enables in (0, 0x40, 0x20, 8, 0x68):
                    scaled = bool((overflow and enables & 0x40) or (tiny and enables & 0x20))
                    if scaled:
                        adjustment = (-1 if overflow else 1) * (192 if single else 1536)
                        result = rounded(wide * power(adjustment), zero_sign, 0, e, f)
                        inexact = wide != value
                        incremented = abs(wide) > abs(truncated)
                    else:
                        result = rounded(value, zero_sign, mode, e, f)
                        inexact = overflow or decode(result, e, f) != value
                        incremented = not overflow and abs(decode(result, e, f)) > abs(value)
                    raised = 0x10000000 if overflow else 0
                    if tiny and (inexact or enables & 0x20):
                        raised |= 0x08000000
                    if inexact:
                        raised |= 0x02000000
                    magnitude = result & ~sign
                    if magnitude == infinity:
                        bits = 0x7FF0000000000000 | ((result >> (e + f)) << 63)
                    else:
                        bits = rounded(decode(result, e, f), bool(result & sign), 0, 11, 52)
                    if operation >= 30:
                        bits ^= 1 << 63
                    old = (0, 0x02000000, 0x08000000)[count % 3]
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
                    mask = 0xFFFFFFFF ^ (0x40000 if overflow and not scaled else 0)
                    output.write(
                        f"{operation} {single} {sources[0]:x} {sources[1]:x} {sources[2]:x} {initial:x} {bits:x} {status:x} {mask:x}\n"
                    )
                    count += 1
    print(f"Generated {count} independent fused FPSCR cases")


if __name__ == "__main__":
    generate(*sys.argv[1:])
