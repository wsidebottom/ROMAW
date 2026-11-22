# +===================================================================
#   File:      mac-fsel-reference.py
#
#   Summary:   Original classic PPC raw FSEL guard/alias/payload hardware probe.
#
#   Classes:   None declared.
#
#   Functions: generate, decode
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original classic PPC raw FSEL guard/alias/payload hardware probe."""

import argparse
import json
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import generate_classic


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    guards = (
        0,
        0x8000000000000000,
        1,
        0x8000000000000001,
        0x3FF0000000000000,
        0xBFF0000000000000,
        0x7FF0000000000000,
        0xFFF0000000000000,
        0x7FF8123456789ABC,
        0xFFF8123456789ABC,
        0x7FF0123456789ABC,
        0xFFF0123456789ABC,
        0x7FF0000000000001,
        0xFFF0000000000001,
    )
    code = [0x9421FF60]
    code += [(54 << 26) | (r << 21) | (1 << 16) | (16 + (r - 14) * 8) for r in range(14, 32)]
    for index, guard in enumerate(guards):
        for source in range(32):
            for destination in range(32):
                for record in (0, 1):
                    b, c = 0x7FF0123456789ABC, 0xFFF056789ABCDEF0
                    if source == 2:
                        b = guard
                    if source == 3:
                        c = guard
                    initial = 0x80060000 | ((index + source + record) & 7)
                    if index & 2:
                        initial |= 0x01000080
                    opcode = (
                        (63 << 26) | (destination << 21) | (source << 16) | (2 << 11) | (3 << 6) | (23 << 1) | record
                    )
                    cases.append(dict(opcode=opcode, inputs=[guard, b, c], fpscr=initial))
                    code += [
                        (50 << 26) | (6 << 16) | 24,
                        (63 << 26) | (255 << 17) | (711 << 1),
                        (50 << 26) | (2 << 21) | (6 << 16) | 8,
                        (50 << 26) | (3 << 21) | (6 << 16) | 16,
                        (50 << 26) | (source << 21) | (6 << 16),
                        opcode,
                        (63 << 26) | (4 << 21) | (destination << 11) | (72 << 1),
                        (63 << 26) | (5 << 21) | (583 << 1),
                        (54 << 26) | (4 << 21) | (7 << 16),
                        (54 << 26) | (5 << 21) | (7 << 16) | 8,
                        (14 << 26) | (6 << 21) | (6 << 16) | 32,
                        (14 << 26) | (7 << 21) | (7 << 16) | 16,
                    ]
    code += [(50 << 26) | (r << 21) | (1 << 16) | (16 + (r - 14) * 8) for r in range(14, 32)]
    code.append(0x382100A0)
    generate_classic(
        directory,
        cases,
        code,
        application_name="PPC FSEL Probe",
        result_name="PPC FSEL Results",
        description="raw FSEL guard/alias/payload",
    )


def decode(directory, capture):
    reference = runpy.run_path(str(Path(__file__).with_name("mac-fpu-reference.py")))
    reference["decode"](directory, capture, binary=True)
    fpr = runpy.run_path(str(Path(__file__).with_name("mac-fpr-reference.py")))
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    output = capture.read_bytes()
    for index, case in enumerate(cases):
        value, fpscr = struct.unpack_from(">QQ", output, index * 16)
        a, b, c = case["inputs"]
        magnitude = a & ((1 << 63) - 1)
        yes = (not (a >> 63) or not magnitude) and magnitude <= 0x7FF0000000000000
        expected = c if yes else b
        if value != expected or (fpscr & 0xFFFFFFFF) != fpr["status"](case["fpscr"]):
            raise ValueError(f"Raw FSEL mismatch at case {index}")
    print(f"{len(cases)} hardware FSEL guard/alias/payload/FPSCR cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    args = parser.parse_args()
    if args.decode_binary:
        decode(args.directory, args.decode_binary)
    else:
        generate(args.directory)
