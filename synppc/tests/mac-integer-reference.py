# +===================================================================
#   File:      mac-integer-reference.py
#
#   Summary:   Original classic 603e integer hardware probe and bounded capture decoder.
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

"""Original classic 603e integer hardware probe and bounded capture decoder."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from classic_fpu_reference import generate_classic


def generate(directory, alu=False, carry=False):
    values = [0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFE, 0x10000, 0x8000]
    cases, body, inputs = [], [], bytearray()
    # XER/CR0/CR7 and r0-r12 are volatile under the classic PPC ABI. Preserve
    # CR2-CR4 by initializing only fields 0 and 7, and capture the full old CR.
    operations = [266, 778, 40, 552, 104, 616] if alu else [235, 747, 75, 11, 491, 1003, 459, 971]
    if carry:
        values = values[:6]
        operations = [op | oe for op in (10, 8, 138, 136, 234, 202, 232, 200) for oe in (0, 512)]
    for xo in operations:
        for a in values:
            for b in values:
                for flags in range(8):
                    for record in range(2):
                        for rd in [3, 4, 5]:
                            xer = (flags << 29) | 0x42
                            cr = ((flags * 0x13579BDF) & 0xF000000F) | 1
                            op = (
                                (31 << 26)
                                | (rd << 21)
                                | (3 << 16)
                                | (0 if (xo & 511) in (104, 234, 202, 232, 200) else (4 << 11))
                                | (xo << 1)
                                | record
                            )
                            cases.append(dict(opcode=op, a=a, b=b, xer=xer, cr=cr))
                            inputs.extend(struct.pack(">8I", a, b, cr, xer, 0, 0, 0, 0))
                            body.extend(
                                [
                                    0x80660000,  # lwz r3,0(r6)
                                    0x80860004,  # lwz r4,4(r6)
                                    0x81060008,  # lwz r8,8(r6)
                                    0x8126000C,  # lwz r9,12(r6)
                                    0x7D2103A6,  # mtxer r9
                                    (31 << 26) | (8 << 21) | (0x81 << 12) | (144 << 1),
                                    0x7D400026,  # mfcr r10, before the operation
                                    0x38A01234,  # li r5,0x1234 (independent output)
                                    op,
                                    0x7D000026,  # mfcr r8
                                    0x7D2102A6,  # mfxer r9
                                    (36 << 26) | (rd << 21) | (7 << 16),
                                    0x91270004,  # stw r9,4(r7)
                                    0x91070008,  # stw r8,8(r7)
                                    0x9147000C,  # stw r10,12(r7)
                                    0x38C60020,  # addi r6,r6,32
                                    0x38E70010,  # addi r7,r7,16
                                ]
                            )
    generate_classic(
        directory,
        cases,
        body,
        application_name=(
            "PPC Carry Hardware Probe" if carry else "PPC ALU Hardware Probe" if alu else "PPC Integer Hardware Probe"
        ),
        result_name=("PPC Carry Results" if carry else "PPC ALU Results" if alu else "PPC Integer Results"),
        input_data=bytes(inputs),
        description="integer",
    )


def decode(directory, capture):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Truncated or oversized hardware capture")
    lines = []
    for i, c in enumerate(cases):
        result, xer, cr, before = struct.unpack_from(">4I", data, i * 16)
        if (before & 0xF000000F) != c["cr"]:
            raise ValueError(f"Case {i}: missing capture or invalid CR initialization")
        fields = [c["opcode"], c["a"], c["b"], c["xer"], result, xer, cr, before]
        lines.append(" ".join(f"{v:08x}" for v in fields))
    (directory / "integer-hardware.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Decoded {len(cases)} integer hardware cases")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--alu", action="store_true", help="Generate add/subtract/negate cases")
    group.add_argument("--carry", action="store_true", help="Generate carry arithmetic cases")
    args = parser.parse_args()
    if args.decode:
        decode(args.directory, args.decode)
    else:
        generate(args.directory, args.alu, args.carry)
