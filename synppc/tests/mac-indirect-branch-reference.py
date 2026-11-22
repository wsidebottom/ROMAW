# +===================================================================
#   File:      mac-indirect-branch-reference.py
#
#   Summary:   Original bounded classic PPC LR/CTR branch hardware probe.
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

"""Original bounded classic PPC LR/CTR branch hardware probe."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory):
    cases, body, inputs = [], [], bytearray()
    for source in ("lr", "ctr"):
        for bo in (0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20):
            if source == "ctr" and not bo & 4:
                continue  # Decrement/test CTR is invalid for bcctr.
            for bi in (0, 2, 28, 31):
                for ctr in (0, 1, 2, 0xFFFFFFFF) if source == "lr" else (0,):
                    for cr in (0, 0xFFFFFFFF):
                        for link in (0, 1):
                            for low in range(4):
                                for backward in (False, True):
                                    xer = ((ctr & 7) << 29) | 0x42
                                    inputs.extend(struct.pack(">8I", xer, cr, ctr, 0, 0, 0, 0, 0))
                                    code = [
                                        0x81660000,
                                        0x7D6103A6,
                                        0x80860004,
                                        (31 << 26) | (4 << 21) | (0x81 << 12) | (144 << 1),
                                        0x80A60008,
                                        0x7CA903A6,
                                        0x7D400026,
                                        0x48000005,
                                        0x7D0802A6,
                                        0,  # addi r9,r8,target offset including low bits.
                                        0x7D2803A6 if source == "lr" else 0x7D2903A6,
                                        0x38600000,
                                    ]
                                    anchor = 8
                                    if backward:
                                        code.append(0x48000018)
                                        target = len(code)
                                        code.extend(
                                            [
                                                0x38600001,
                                                0,
                                                0x60000000,
                                                0x60000000,
                                                0x60000000,
                                            ]
                                        )
                                        taken_exit = target + 1
                                        measured = len(code)
                                        code.extend([0, 0x38600000])
                                        code[taken_exit] = 0x48000000 | ((len(code) - taken_exit) * 4)
                                    else:
                                        measured = len(code)
                                        code.extend([0, 0x38600000, 0x4800000C, 0x60000000])
                                        target = len(code)
                                        code.append(0x38600001)
                                    target_offset = (target - anchor) * 4 + low
                                    code[9] = 0x39280000 | target_offset
                                    code[measured] = (
                                        (19 << 26)
                                        | (bo << 21)
                                        | (bi << 16)
                                        | ((16 if source == "lr" else 528) << 1)
                                        | link
                                    )
                                    next_ctr = ctr if bo & 4 else (ctr - 1) & 0xFFFFFFFF
                                    ctr_ok = source == "ctr" or bool(bo & 4) or (bool(next_ctr) != bool(bo & 2))
                                    cr_ok = bool(bo & 16) or bool(cr & (0x80000000 >> bi)) == bool(bo & 8)
                                    expected_lr = (
                                        (measured + 1 - anchor) * 4
                                        if link
                                        else (target_offset if source == "lr" else 0)
                                    )
                                    code.extend(
                                        [
                                            0x7D2802A6,
                                            (31 << 26) | (9 << 21) | (8 << 16) | (9 << 11) | (40 << 1),
                                            0x7C800026,
                                            (31 << 26) | (4 << 21) | (4 << 16) | (10 << 11) | (316 << 1),
                                            0x7C0102A6,
                                            (31 << 26) | (0 << 21) | (0 << 16) | (11 << 11) | (316 << 1),
                                            (31 << 26) | (4 << 21) | (4 << 16) | (0 << 11) | (444 << 1),
                                            0x7CA902A6,
                                        ]
                                    )
                                    if source == "ctr":
                                        code.append((31 << 26) | (5 << 21) | (8 << 16) | (5 << 11) | (40 << 1))
                                    code.extend(
                                        [
                                            0x90670000,
                                            0x91270004,
                                            0x90A70008,
                                            0x9087000C,
                                            0x38C60020,
                                            0x38E70010,
                                        ]
                                    )
                                    body.extend(code)
                                    cases.append(
                                        dict(
                                            source=source,
                                            bo=bo,
                                            bi=bi,
                                            ctr=ctr,
                                            cr=cr,
                                            link=link,
                                            low=low,
                                            backward=backward,
                                            expected=[
                                                int(ctr_ok and cr_ok),
                                                expected_lr,
                                                (target_offset if source == "ctr" else next_ctr),
                                                0,
                                            ],
                                        )
                                    )
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC Indirect Branch Probe",
        result_name="PPC Indirect Results",
        input_data=bytes(inputs),
        description="indirect-branch",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized indirect branch capture")
    for i, case in enumerate(cases):
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"Indirect case {i} {case}: {actual}, expected {case['expected']}")
    print(f"{len(cases)} hardware LR/CTR branch/low-bit/LK/CR/XER cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory)
