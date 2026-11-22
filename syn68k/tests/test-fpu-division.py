#!/usr/bin/env python3
# +===================================================================
#   File:      test-fpu-division.py
#
#   Summary:   Check Motorola division and writeback data using exact integer ratios.
#
#   Classes:   None declared.
#
#   Functions: expected, main
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Check Motorola division and writeback data using exact integer ratios."""

from pathlib import Path
import random
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def expected(ah, a, bh, b, precision, mode):
    sign = (ah ^ bh) & 0x8000
    exponent = (ah & 0x7FFF) - (bh & 0x7FFF) + 0x3FFF
    if a < b:
        a <<= 1
        exponent -= 1
    width = {32: 24, 64: 53, 80: 64}[precision]
    rounded, remainder = divmod(a << (width - 1), b)
    if mode == 0:
        increment = 2 * remainder > b or (2 * remainder == b and rounded & 1)
    else:
        increment = remainder != 0 and ((mode == 2 and sign) or (mode == 3 and not sign))
    rounded += bool(increment)
    rounded_exponent = exponent
    if rounded == 1 << width:
        rounded >>= 1
        rounded_exponent += 1
    rounded <<= 64 - width
    minimum = {32: 0x3F81, 64: 0x3C01, 80: 0}[precision]
    maximum = {32: 0x407E, 64: 0x43FE, 80: 0x7FFE}[precision]
    status = 0x800 if exponent < minimum else 0x1000 if rounded_exponent > maximum else 0
    intermediate, remainder = divmod(a << 63, b)
    grs, remainder = divmod(remainder * 4, b)
    sticky = int(remainder != 0)
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
    )


def main():
    output = ROOT / "syn68k/obj/tests/fpu"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "division-rational-test"
    subprocess.run(
        [
            "gcc",
            "-std=gnu17",
            "-g",
            "-O2",
            "-fwrapv",
            "-Isyn68k/src/runtime/include",
            "syn68k/tests/fpu-division-test.c",
            "syn68k/third_party/softfloat/softfloat.c",
            "-o",
            str(binary),
        ],
        cwd=ROOT,
        check=True,
    )
    rng = random.Random(68040)
    cases = []
    for precision in (32, 64, 80):
        for mode in range(4):
            for _ in range(500):
                cases.append(
                    (
                        rng.randrange(0x7FFF) | rng.choice((0, 0x8000)),
                        rng.getrandbits(63) | (1 << 63),
                        rng.randrange(0x7FFF) | rng.choice((0, 0x8000)),
                        rng.getrandbits(63) | (1 << 63),
                        precision,
                        mode,
                    )
                )
            for ah in (0, 0x3F80, 0x3F81, 0x407E, 0x43FE, 0x7FFE):
                for a in (1 << 63, (1 << 63) + (1 << 39), (1 << 64) - 1):
                    cases.append((ah, a, 0x3FFF, 1 << 63, precision, mode))
    data = "".join(f"{ah:x} {a:x} {bh:x} {b:x} {p} {m}\n" for ah, a, bh, b, p, m in cases)
    result = subprocess.run([str(binary)], input=data, text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(cases)
    for case, line in zip(cases, lines):
        words = line.split()
        actual = tuple(int(word, 16 if i < 4 else 10) for i, word in enumerate(words))
        reference = expected(*case)
        assert actual == reference, (case, actual, reference)
    print(f"{len(cases)} exact-rational division/writeback cases passed.")


if __name__ == "__main__":
    main()
