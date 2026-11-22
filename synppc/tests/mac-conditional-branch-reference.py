# +===================================================================
#   File:      mac-conditional-branch-reference.py
#
#   Summary:   Original bounded classic PPC conditional-branch hardware probe.
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

"""Original bounded classic PPC conditional-branch hardware probe."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory):
    cases, body, inputs = [], [], bytearray()
    for bo in (0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20):
        for bi in (0, 2, 28, 31):
            for ctr in (0, 1, 2, 0xFFFFFFFF):
                for cr in (0, 0xFFFFFFFF):
                    for link in (0, 1):
                        for displacement in (16, -16):
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
                                0x38600000,
                                0x48000005,
                                0x7D0802A6,
                            ]
                            anchor = len(code) - 1

                            def branch(source, target, measured=False):
                                delta = (target - source) * 4
                                code[source] = (
                                    (16 << 26) | (bo << 21) | (bi << 16) | (delta & 0xFFFC) | link
                                    if measured
                                    else 0x48000000 | (delta & 0x03FFFFFC)
                                )

                            if displacement > 0:
                                measured = len(code)
                                code.extend([0, 0x38600000, 0])
                                failed_exit = len(code) - 1
                                while len(code) < measured + displacement // 4:
                                    code.append(0x60000000)
                                target = len(code)
                                code.append(0x38600001)
                                branch(measured, target, True)
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
                                code.extend([0, 0x38600000])
                                branch(skip, measured)
                                branch(measured, target, True)
                                branch(taken_exit, len(code))
                            next_ctr = ctr if bo & 4 else (ctr - 1) & 0xFFFFFFFF
                            ctr_ok = bool(bo & 4) or (bool(next_ctr) != bool(bo & 2))
                            cr_ok = bool(bo & 16) or bool(cr & (0x80000000 >> bi)) == bool(bo & 8)
                            expected_lr = (measured + 1 - anchor) * 4 if link else 0
                            # Preserve all nonvolatile CR fields. Fold CR/XER
                            # differences with OR, so either change is detected.
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
                                    bo=bo,
                                    bi=bi,
                                    ctr=ctr,
                                    cr=cr,
                                    link=link,
                                    displacement=displacement,
                                    expected=[
                                        int(ctr_ok and cr_ok),
                                        expected_lr,
                                        next_ctr,
                                        0,
                                    ],
                                )
                            )
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC Conditional Branch Probe",
        result_name="PPC Conditional Results",
        input_data=bytes(inputs),
        description="conditional-branch",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized conditional branch capture")
    excluded = 0
    for i, case in enumerate(cases):
        # Old captures included BO=21, whose ignored z bit is reserved in
        # pre-V2.00 encoding. Its observed alias is not an architecture oracle.
        if case["bo"] == 21:
            excluded += 1
            continue
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"Conditional case {i}: {actual}, expected {case['expected']}")
    print(
        f"{len(cases) - excluded} hardware conditional-branch/CTR/LR/CR/XER cases matched; "
        f"{excluded} undefined BO encodings excluded"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory)
