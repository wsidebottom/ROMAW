# +===================================================================
#   File:      compare-reference.py
#
#   Summary:   Rational ordering and architectural NaN/status cases for FCMPU/FCMPO.
#
#   Classes:   None declared.
#
#   Functions: classify, expected, generate
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Rational ordering and architectural NaN/status cases for FCMPU/FCMPO."""

from pathlib import Path
import random
import runpy
import sys

decode = runpy.run_path(str(Path(__file__).with_name("fused-reference.py")))["decode"]


def classify(bits):
    exponent = (bits >> 52) & 2047
    fraction = bits & ((1 << 52) - 1)
    if exponent == 2047:
        if fraction:
            return "qnan" if fraction & (1 << 51) else "snan", None
        return "infinity", -1 if bits >> 63 else 1
    return "finite", decode(bits, 11, 52)


def expected(a, b, ordered, enabled):
    ka, va = classify(a)
    kb, vb = classify(b)
    raised = 0
    if ka.endswith("nan") or kb.endswith("nan"):
        condition = 1
        signaling = ka == "snan" or kb == "snan"
        if signaling:
            raised |= 0x01000000
        if ordered and (not signaling or not enabled):
            raised |= 0x00080000
    elif ka == "infinity" or kb == "infinity":
        if ka == kb:
            condition = 2 if va == vb else 8 if va < vb else 4
        elif ka == "infinity":
            condition = 8 if va < 0 else 4
        else:
            condition = 4 if vb < 0 else 8
    else:
        condition = 2 if va == vb else 8 if va < vb else 4
    status = (condition << 12) | (0x80 if enabled else 0)
    if raised:
        status |= raised | 0xA0000000 | (0x40000000 if enabled else 0)
    return condition, status


def generate(destination):
    positive = [
        0,
        1,
        (1 << 52) - 1,
        1 << 52,
        0x3FF0000000000000,
        0x7FEFFFFFFFFFFFFF,
        0x7FF0000000000000,
        0x7FF0000000000001,
        0x7FF7FFFFFFFFFFFF,
        0x7FF8000000000123,
    ]
    edges = positive + [bits | (1 << 63) for bits in positive]
    pairs = [(a, b) for a in edges for b in edges]
    rng = random.Random(0xFC0F750)
    pairs += [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(600)]
    cases = []
    for a, b in pairs:
        for ordered in (0, 1):
            for enabled in (0, 1):
                condition, status = expected(a, b, ordered, enabled)
                cases.append(f"{a:x} {b:x} {ordered} {enabled} {condition:x} {status:x}\n")
    Path(destination).write_text("".join(cases), encoding="ascii")
    print(f"Generated {len(cases)} rational comparison/NaN cases")


if __name__ == "__main__":
    generate(sys.argv[1])
