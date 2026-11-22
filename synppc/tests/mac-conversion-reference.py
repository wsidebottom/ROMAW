# +===================================================================
#   File:      mac-conversion-reference.py
#
#   Summary:   Original classic PPC signed-word conversion hardware probe.
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

"""Original classic PPC signed-word conversion hardware probe.

The high output word is architecturally undefined; comparisons must use only
the low word. NI is tested with normal operands, zero, infinities and NaNs.
"""

import argparse
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import arithmetic_body, generate_classic


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    values = [0, 0x0010000000000000, 0x7FEFFFFFFFFFFFFF]
    values += [0x7FF0000000000000 | n for n in (0, 1, 0x8000000000123)]
    for value in (0.25, 0.5, 0.75, 1.5, 2.5, 2147483647, 2147483648):
        bits = struct.unpack(">Q", struct.pack(">d", value))[0]
        values.extend((bits - 1, bits, bits + 1))
    cases = []
    for operation in (14, 15):
        for bits in values:
            for negative in (False, True):
                for mode in range(4):
                    for ni in (0, 4):
                        cases.append(
                            dict(
                                opcode=(63 << 26) | (4 << 21) | (2 << 11) | (operation << 1),
                                inputs=[0, bits | (int(negative) << 63), 0],
                                fpscr=0x15000 | mode | ni,
                            )
                        )
    generate_classic(
        directory,
        cases,
        arithmetic_body(cases),
        application_name="PPC Conversion Probe",
        result_name="PPC Conversion Results",
        description="signed-word conversion",
    )


def compare(directory, reference):
    actual = (directory / "hardware.txt").read_text(encoding="ascii").splitlines()
    expected = reference.read_text(encoding="ascii").splitlines()
    if not expected or len(actual) != len(expected):
        raise ValueError("Conversion capture row count differs")
    for index, (got, want) in enumerate(zip(actual, expected)):
        got, want = ([int(x, 16) for x in row.split()] for row in (got, want))
        if got[:5] != want[:5] or (got[5] & 0xFFFFFFFF) != (want[5] & 0xFFFFFFFF) or got[6] != want[6]:
            raise ValueError(f"Conversion hardware mismatch at row {index}")
    print(f"{len(actual)} conversion low-word/FPSCR cases matched the physical Mac")


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
