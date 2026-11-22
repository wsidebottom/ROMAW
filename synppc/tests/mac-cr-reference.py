# +===================================================================
#   File:      mac-cr-reference.py
#
#   Summary:   Original bounded classic PPC condition-register hardware probe.
#
#   Classes:   None declared.
#
#   Functions: result, generate, decode
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original bounded classic PPC condition-register hardware probe."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic

OPERATIONS = {
    "crand": 257,
    "crandc": 129,
    "creqv": 289,
    "crnand": 225,
    "crnor": 33,
    "cror": 449,
    "crorc": 417,
    "crxor": 193,
}


def result(name, cr, dest, left, right):
    if name == "mcrf":
        shift = 28 - dest * 4
        value = cr >> (28 - left * 4) & 15
        return (cr & ~(15 << shift)) | value << shift
    a, b = cr >> (31 - left) & 1, cr >> (31 - right) & 1
    value = {
        "crand": a & b,
        "crandc": a & (b ^ 1),
        "creqv": (a ^ b) ^ 1,
        "crnand": (a & b) ^ 1,
        "crnor": (a | b) ^ 1,
        "cror": a | b,
        "crorc": a | (b ^ 1),
        "crxor": a ^ b,
    }[name]
    return (cr & ~(1 << (31 - dest))) | value << (31 - dest)


def generate(directory):
    cases, body, inputs = [], [], bytearray()

    def case(name, dest, left, right, cr):
        i = len(cases)
        xer = ((i & 7) << 29) | (i & 0x7F)
        ctr = i * 0x9E3779B9 & 0xFFFFFFFF
        inputs.extend(struct.pack(">8I", cr, xer, ctr, 0, 0, 0, 0, 0))
        opcode = (
            (19 << 26) | (dest << 23) | (left << 18)
            if name == "mcrf"
            else (19 << 26) | (dest << 21) | (left << 16) | (right << 11) | (OPERATIONS[name] << 1)
        )
        body.extend(
            [
                0x7D800026,  # mfcr r12: restore nonvolatile CR fields every case.
                0x80860000,
                0x81660004,
                0x7D6103A6,
                0x80A60008,
                0x7CA903A6,
                0x48000005,
                0x7D0802A6,
                0x7C8FF120,  # mtcrf 255,r4.
                opcode,
                0x7C800026,
                0x7CA102A6,
                0x7C0902A6,
                0x7D2802A6,
                (31 << 26) | (9 << 21) | (8 << 16) | (9 << 11) | (40 << 1),
                0x90870000,
                0x90A70004,
                0x90070008,
                0x9127000C,
                0x7D8FF120,  # mtcrf 255,r12.
                0x38C60020,
                0x38E70010,
            ]
        )
        cases.append(
            dict(
                name=name,
                dest=dest,
                left=left,
                right=right,
                cr=cr,
                expected=[result(name, cr, dest, left, right), xer, ctr, 0],
            )
        )

    for name in OPERATIONS:
        for dest in range(32):
            pairs = [
                (dest, dest),
                (dest, (dest + 1) % 32),
                ((dest + 1) % 32, dest),
                ((dest + 1) % 32, (dest + 1) % 32),
                ((dest + 1) % 32, (dest + 2) % 32),
                (31 - dest, (dest + 17) % 32),
                (0, 31),
                (31, 0),
            ]
            for left, right in pairs:
                for bits in range(4):
                    cr = len(cases) * 0x9E3779B9 & 0xFFFFFFFF
                    cr = (cr & ~(1 << (31 - left))) | (bits & 1) << (31 - left)
                    cr = (cr & ~(1 << (31 - right))) | (bits >> 1) << (31 - right)
                    case(name, dest, left, right, cr)
    for dest in range(8):
        for source in range(8):
            for cr in (0, 0xFFFFFFFF, 0x01234567, 0x89ABCDEF):
                case("mcrf", dest, source, 0, cr)
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC CR Hardware Probe",
        result_name="PPC CR Results",
        input_data=bytes(inputs),
        description="condition-register",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized condition-register capture")
    for i, case in enumerate(cases):
        actual = list(struct.unpack_from(">4I", data, i * 16))
        if actual != case["expected"]:
            raise ValueError(f"CR case {i} {case}: got {actual}")
    print(f"{len(cases)} hardware CR logic/field/alias/XER/CTR/LR cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory)
