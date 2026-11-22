# +===================================================================
#   File:      mac-cr-transfer-reference.py
#
#   Summary:   Original classic PPC condition-register transfer hardware probe.
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

"""Original classic PPC condition-register transfer hardware probe."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory):
    cases, body, inputs = [], [], bytearray()
    # Keep SP, TOC, the input/output pointers, anchor and nonvolatile GPRs intact.
    registers = (0, 3, 4, 5, 9, 10, 11)

    def case(name, reg, mask, cr, xer, value):
        ctr = len(cases) * 0x9E3779B9 & 0xFFFFFFFF
        inputs.extend(struct.pack(">8I", cr, xer, ctr, value, 0, 0, 0, 0))
        code = [
            0x7D800026,  # mfcr r12; restore the caller's CR after every case.
            0x80860000,
            0x81660004,
            0x7D6103A6,
            0x80A60008,
            0x7CA903A6,
            0x48000005,
            0x7D0802A6,
            0x7C8FF120,
        ]
        if name == "mfcr":
            code.extend(
                [
                    (31 << 26) | (reg << 21) | (19 << 1),
                    (31 << 26) | (reg << 21) | (3 << 16) | (reg << 11) | (444 << 1),
                ]
            )
            expected_cr, expected_xer = cr, xer
        elif name == "mtcrf":
            code.extend(
                [
                    (32 << 26) | (reg << 21) | (6 << 16) | 12,
                    (31 << 26) | (reg << 21) | (mask << 12) | (144 << 1),
                    0x7C600026,
                ]
            )
            bit_mask = sum(15 << (28 - i * 4) for i in range(8) if mask & (128 >> i))
            expected_cr = (cr & ~bit_mask) | (value & bit_mask)
            expected_xer = xer
        else:
            code.extend([(31 << 26) | (reg << 23) | (512 << 1), 0x7C600026])
            shift = 28 - reg * 4
            expected_cr = (cr & ~(15 << shift)) | (xer >> 28) << shift
            expected_xer = xer & 0x0FFFFFFF
        code.extend(
            [
                0x7C8102A6,
                0x7CA902A6,
                0x7D2802A6,
                (31 << 26) | (9 << 21) | (8 << 16) | (9 << 11) | (40 << 1),
                0x90670000,
                0x90870004,
                0x90A70008,
                0x9127000C,
                0x7D8FF120,
                0x38C60020,
                0x38E70010,
            ]
        )
        body.extend(code)
        cases.append(
            dict(
                name=name,
                reg=reg,
                mask=mask,
                cr=cr,
                xer=xer,
                value=value,
                expected=[expected_cr, expected_xer, ctr, 0],
            )
        )

    patterns = (0, 0xFFFFFFFF, 0x01234567, 0x89ABCDEF)
    for reg in registers:
        for mask in range(256):
            for cr in patterns:
                case(
                    "mtcrf",
                    reg,
                    mask,
                    cr,
                    ((mask & 7) << 29) | (mask & 0x7F),
                    cr ^ 0xFFFFFFFF,
                )
        for cr in patterns:
            for flags in range(8):
                case("mfcr", reg, 0, cr, (flags << 29) | 0x42, 0)
    for field in range(8):
        for flags in range(8):
            for count in (0, 1, 0x7F):
                for cr in patterns:
                    case("mcrxr", field, 0, cr, (flags << 29) | count, 0)
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC CR Transfer Probe",
        result_name="PPC CR Transfer Results",
        input_data=bytes(inputs),
        description="condition-register transfer",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized CR transfer capture")
    for i, case in enumerate(cases):
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"CR transfer case {i} {case}: got {actual}")
    print(f"{len(cases)} hardware MFCR/MTCRF/MCRXR/CR/XER/CTR/LR cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory)
