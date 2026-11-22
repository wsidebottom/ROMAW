#!/usr/bin/env python3
# +===================================================================
#   File:      compare-fpu-oracle.py
#
#   Summary:   Compare original instruction results without trusting QEMU's FP exceptions.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Compare original instruction results without trusting QEMU's FP exceptions."""

import sys
from pathlib import Path


def main():
    reference = Path(sys.argv[1]).read_bytes()
    actual = Path(sys.argv[2]).read_bytes()
    if len(reference) != 1600 or len(actual) != len(reference):
        raise SystemExit(f"Invalid oracle result lengths: {len(reference)}, {len(actual)}")
    for offset in range(0, len(reference), 16):
        # Each record contains a 12-byte extended value followed by FPSR.
        # QEMU does not implement all Motorola status/accrued exceptions, and
        # its FMOVE-out changes CC contrary to the manual. The assembly takes
        # FPSR first; only CC/quotient and the value are compared here. Syn68k's
        # exception bits have separate instruction-level assertions.
        expected = reference[offset : offset + 14]
        received = actual[offset : offset + 14]
        if expected != received:
            raise SystemExit(f"Oracle case {offset // 16}: {received.hex()} != {expected.hex()}")
    print(f"{len(reference) // 16} independent FPU value/condition cases passed.")


if __name__ == "__main__":
    main()
