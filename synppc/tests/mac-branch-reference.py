# +===================================================================
#   File:      mac-branch-reference.py
#
#   Summary:   Original bounded classic PPC direct-branch hardware probe.
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

"""Original bounded classic PPC direct-branch hardware probe."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory):
    cases, body, inputs = [], [], bytearray()
    for displacement in (12, 16, 32, 64, -8, -12, -32, -64):
        for link in (0, 1):
            for flags in range(8):
                xer = (flags << 29) | 0x42
                inputs.extend(struct.pack(">8I", xer, flags * 0x13579BDF & 0xFFFFFFFF, 0, 0, 0, 0, 0, 0))
                code = [
                    0x81660000,
                    0x7D6103A6,
                    0x80860004,
                    (31 << 26) | (4 << 21) | (0x81 << 12) | (144 << 1),
                    0x7D400026,
                    0x38600000,
                    0x48000005,
                    0x7D0802A6,
                ]
                anchor = len(code) - 1

                def branch(source, target, lk=0):
                    code[source] = 0x48000000 | ((target - source) * 4 & 0x03FFFFFC) | lk

                if displacement > 0:
                    measured = len(code)
                    code.extend([0, 0x3860FFFF, 0])
                    failed_exit = len(code) - 1
                    while len(code) < measured + displacement // 4:
                        code.append(0x60000000)
                    target = len(code)
                    code.append(0x38600001)
                    branch(measured, target, link)
                    branch(failed_exit, len(code))
                else:
                    skip = len(code)
                    code.append(0)
                    target = len(code)
                    code.extend([0x38600001, 0])
                    taken_exit = len(code) - 1
                    while len(code) < target - displacement // 4:
                        code.append(0x60000000)
                    measured = len(code)
                    code.extend([0, 0x3860FFFF])
                    branch(skip, measured)
                    branch(measured, target, link)
                    branch(taken_exit, len(code))
                expected_lr = (measured + 1 - anchor) * 4 if link else 0
                # Normalize LR to the measured code's anchor; compare every
                # CR and XER bit with its pre-branch value without altering it.
                code.extend(
                    [
                        0x7D2802A6,
                        (31 << 26) | (9 << 21) | (8 << 16) | (9 << 11) | (40 << 1),
                        0x7C800026,
                        (31 << 26) | (4 << 21) | (4 << 16) | (10 << 11) | (316 << 1),
                        0x7CA102A6,
                        (31 << 26) | (5 << 21) | (5 << 16) | (11 << 11) | (316 << 1),
                        0x90670000,
                        0x91270004,
                        0x90870008,
                        0x90A7000C,
                        0x38C60020,
                        0x38E70010,
                    ]
                )
                body.extend(code)
                cases.append(
                    dict(
                        displacement=displacement,
                        link=link,
                        xer=xer,
                        expected=[1, expected_lr, 0, 0],
                    )
                )
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC Branch Hardware Probe",
        result_name="PPC Branch Results",
        input_data=bytes(inputs),
        description="direct-branch",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized branch capture")
    for i, case in enumerate(cases):
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"Branch case {i}: {actual}, expected {case['expected']}")
    print(f"{len(cases)} hardware direct-branch/relative-LR/CR/XER cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory)
