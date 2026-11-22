# +===================================================================
#   File:      mac-reciprocal-reference.py
#
#   Summary:   Original classic PPC FRES probe using normal exact-power operands.
#
#   Classes:   None declared.
#
#   Functions: generate, compare
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original classic PPC FRES probe using normal exact-power operands.

General estimate bits are processor-dependent. These vectors sample powers of
two across normal, tiny and scaled-result boundaries, all signs/RN/NI/OE/UE.
"""

import argparse
from pathlib import Path
import runpy

from classic_fpu_reference import arithmetic_body, generate_classic


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    exponents = (-1022, *range(-130, -124), 0, *range(125, 154), 500, 1023)
    for exponent in exponents:
        for negative in (False, True):
            operand = ((exponent + 1023) << 52) | (int(negative) << 63)
            for mode in range(4):
                for ni in (0, 4):
                    for enables in (0, 0x20, 0x40, 0x60):
                        cases.append(
                            dict(
                                opcode=(59 << 26) | (4 << 21) | (2 << 11) | 48,
                                inputs=[0, operand, 0],
                                fpscr=mode | ni | enables,
                            )
                        )
    generate_classic(
        directory,
        cases,
        arithmetic_body(cases),
        application_name="PPC Reciprocal Probe",
        result_name="PPC Reciprocal Results",
        description="reciprocal power",
    )


def compare(directory, reference):
    actual = (directory / "hardware.txt").read_text(encoding="ascii").splitlines()
    expected = reference.read_text(encoding="ascii").splitlines()
    if not expected or len(actual) != len(expected):
        raise ValueError("Reciprocal capture row count differs")
    for index, (got, want) in enumerate(zip(actual, expected)):
        got, want = ([int(x, 16) for x in row.split()] for row in (got, want))
        # FR/FI are undefined. The 603 capture's extra range-path XX is not
        # the documented 750 policy, which the independent model checks.
        mask = 0xFDF9FFFF
        if got[:6] != want[:6] or (got[6] & mask) != (want[6] & mask):
            raise ValueError(f"Reciprocal hardware mismatch at row {index}")
    print(
        f"{len(actual)} reciprocal power values/selected FPSCR fields matched " "the physical Mac (XX/FR/FI excluded)"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    if args.compare and not args.decode_binary:
        parser.error("--compare requires --decode-binary")
    if args.decode_binary:
        reference = runpy.run_path(str(Path(__file__).with_name("mac-fpu-reference.py")))
        reference["decode"](args.directory, args.decode_binary, binary=True)
        if args.compare:
            compare(args.directory, args.compare)
    else:
        generate(args.directory)
