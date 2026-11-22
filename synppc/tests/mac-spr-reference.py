# +===================================================================
#   File:      mac-spr-reference.py
#
#   Summary:   Original classic PPC user SPR move hardware probe; no privileged SPRs.
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

"""Original classic PPC user SPR move hardware probe; no privileged SPRs."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory):
    cases, body, inputs = [], [], bytearray()
    registers = (0, 3, 4, 5, 9, 10, 11)
    values = (
        0,
        1,
        2,
        3,
        0x7F,
        0xFF,
        0x100,
        0x7FFFFFFF,
        0x80000000,
        0xFFFFFFFF,
        0x12345678,
        0x87654321,
        0xE000007F,
        0x60000001,
        0x20000042,
        0xC000007F,
    )
    for spr in (1, 8, 9):
        for source in registers:
            for dest in registers:
                for requested in values:
                    # Reserved XER data bits are not an architecture oracle.
                    value = requested & 0xE000007F if spr == 1 else requested
                    i = len(cases)
                    cr = i * 0x9E3779B9 & 0xFFFFFFFF
                    xer = ((i & 7) << 29) | (i & 0x7F)
                    lr = (i * 0x13579BD) & 0xFFFFFFFF
                    ctr = (i * 0x87654321) & 0xFFFFFFFF
                    expected = value & 0xE000007F if spr == 1 else value
                    inputs.extend(struct.pack(">8I", cr, xer, lr, ctr, value, expected, 0, 0))
                    code = [
                        0x7D800026,
                        0x80860000,
                        0x7C8FF120,
                        0x81660004,
                        0x7D6103A6,
                        0x81260008,
                        0x7D2803A6,
                        0x80A6000C,
                        0x7CA903A6,
                        (32 << 26) | (source << 21) | (6 << 16) | 16,
                        (31 << 26) | (source << 21) | (spr << 16) | (467 << 1),
                        (31 << 26) | (dest << 21) | (spr << 16) | (339 << 1),
                        (31 << 26) | (dest << 21) | (3 << 16) | (dest << 11) | (444 << 1),
                        0x90670000,
                        0x7C800026,
                        0x90870004,
                        0x7CA102A6,
                        0x90A70008,
                        0x7D2802A6,
                        (32 << 26) | (0 << 21) | (6 << 16) | (20 if spr == 8 else 8),
                        (31 << 26) | (9 << 21) | (9 << 16) | (0 << 11) | (316 << 1),
                        0x7CA902A6,
                        (32 << 26) | (0 << 21) | (6 << 16) | (20 if spr == 9 else 12),
                        (31 << 26) | (5 << 21) | (5 << 16) | (0 << 11) | (316 << 1),
                        (31 << 26) | (9 << 21) | (9 << 16) | (5 << 11) | (444 << 1),
                        0x9127000C,
                        0x7D8FF120,
                        0x38C60020,
                        0x38E70010,
                    ]
                    body.extend(code)
                    cases.append(
                        dict(
                            spr=spr,
                            source=source,
                            dest=dest,
                            value=value,
                            requested=requested,
                            expected=[expected, cr, expected if spr == 1 else xer, 0],
                        )
                    )
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC SPR Hardware Probe",
        result_name="PPC SPR Results",
        input_data=bytes(inputs),
        description="user SPR",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized user SPR capture")
    excluded = 0
    for i, case in enumerate(cases):
        if case["spr"] == 1 and case["value"] & ~0xE000007F:
            excluded += 1
            continue  # Old captures wrote nonzero reserved XER data bits.
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"SPR case {i} {case}: got {actual}")
    print(
        f"{len(cases) - excluded} hardware LR/CTR/XER move/alias/CR/invariant cases matched; "
        f"{excluded} reserved XER writes excluded"
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
