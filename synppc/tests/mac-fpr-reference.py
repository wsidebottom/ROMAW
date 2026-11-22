# +===================================================================
#   File:      mac-fpr-reference.py
#
#   Summary:   Original classic PPC raw FPR/FPSCR move probe, preserving nonvolatile FPRs.
#
#   Classes:   None declared.
#
#   Functions: result, status, generate, decode
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original classic PPC raw FPR/FPSCR move probe, preserving nonvolatile FPRs."""

import argparse
import json
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import generate_classic

VALUES = (
    0,
    0x8000000000000000,
    1,
    0x000FFFFFFFFFFFFF,
    0x0010000000000000,
    0x7FEFFFFFFFFFFFFF,
    0x7FF0000000000000,
    0xFFF0000000000000,
    0x7FF8123456789ABC,
    0x7FF0123456789ABC,
    0xFFF0123456789ABC,
    0x3FF123456789ABCD,
)


def result(operation, source):
    if operation == 40:
        return source ^ (1 << 63)
    if operation == 264:
        return source & ((1 << 63) - 1)
    if operation == 136:
        return source | (1 << 63)
    return source


def status(value):
    value &= ~0x60000000
    if value & 0x01F80700:
        value |= 0x20000000
    if (value >> 25) & (value >> 3) & 31:
        value |= 0x40000000
    return value


def generate(directory, fpscr=False):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    # The shared classic wrapper uses only volatile FPRs. This probe touches
    # all 32, so save and restore the ABI's f14-f31 before any OS call.
    code = [0x9421FF60]
    code += [(54 << 26) | (r << 21) | (1 << 16) | (16 + (r - 14) * 8) for r in range(14, 32)]
    if fpscr:
        code += [0x7D000026, 0x91010008]  # Save nonvolatile CR fields.
    for operation in (583,) if fpscr else (72, 40, 264, 136):
        for source in range(32):
            for destination in range(32):
                for record in (0, 1):
                    index = (source * 7 + destination * 3 + record) % len(VALUES)
                    initial = 0x80060000 | (index & 7)
                    if index & 2:
                        initial |= 0x01000080
                    if fpscr:
                        initial = (1 << source) ^ (0xFFFFFFFF if record else 0)
                    opcode = (
                        (63 << 26) | (destination << 21) | (0 if fpscr else source << 11) | (operation << 1) | record
                    )
                    cases.append(
                        dict(
                            opcode=opcode,
                            inputs=[0, VALUES[index], 0x13579BDF if fpscr else 0],
                            fpscr=initial,
                        )
                    )
                    code += [
                        (50 << 26) | (6 << 16) | 24,  # lfd f0,FPSCR(r6).
                        (63 << 26) | (255 << 17) | (711 << 1),
                        # MFFS has no source FPR. A following load into f0
                        # would not change the FPSCR already set by MTFSF.
                        (50 << 26) | ((0 if fpscr else source) << 21) | (6 << 16) | 8,
                    ]
                    if fpscr:
                        code += [0x81060014, 0x7D0FF120]  # Set initial CR.
                    code += [
                        opcode,
                        (63 << 26) | (4 << 21) | (destination << 11) | (72 << 1),
                        (63 << 26) | (5 << 21) | (583 << 1),
                        (54 << 26) | (4 << 21) | (7 << 16),
                        (54 << 26) | (5 << 21) | (7 << 16) | 8,
                    ]
                    if fpscr:
                        # Replace the undefined high FPR word with exact CR.
                        code += [0x7D000026, 0x91070000]
                    code += [
                        (14 << 26) | (6 << 21) | (6 << 16) | 32,
                        (14 << 26) | (7 << 21) | (7 << 16) | 16,
                    ]
    if fpscr:
        code += [0x81010008, 0x7D0FF120]
    code += [(50 << 26) | (r << 21) | (1 << 16) | (16 + (r - 14) * 8) for r in range(14, 32)]
    code.append(0x382100A0)
    generate_classic(
        directory,
        cases,
        code,
        application_name="PPC FPSCR Move Probe" if fpscr else "PPC FPR Move Probe",
        result_name="PPC FPSCR Move Results" if fpscr else "PPC FPR Move Results",
        description="FPSCR move" if fpscr else "raw FPR move/sign",
    )


def decode(directory, capture):
    reference = runpy.run_path(str(Path(__file__).with_name("mac-fpu-reference.py")))
    reference["decode"](directory, capture, binary=True)
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    output = capture.read_bytes()
    for index, case in enumerate(cases):
        value, fpscr = struct.unpack_from(">QQ", output, index * 16)
        operation = (case["opcode"] >> 1) & 1023
        if operation == 583:
            expected = status(case["fpscr"] & ~0x800)
            cr = case["inputs"][2]
            if case["opcode"] & 1:
                cr = (cr & ~0x0F000000) | ((expected >> 4) & 0x0F000000)
            if value >> 32 != cr:
                raise ValueError(f"MFFS CR mismatch at case {index}")
            value &= 0xFFFFFFFF  # UISA leaves the upper MFFS word undefined.
        else:
            expected = result(operation, case["inputs"][1])
        expected_status = status(case["fpscr"] & (~0x800 if operation == 583 else -1))
        if value != expected or (fpscr & 0xFFFFFFFF) != expected_status:
            raise ValueError(f"Raw FPR/FPSCR move mismatch at case {index}")
    print(f"{len(cases)} hardware raw FPR/FPSCR move cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    parser.add_argument("--fpscr", action="store_true")
    args = parser.parse_args()
    if args.decode_binary:
        decode(args.directory, args.decode_binary)
    else:
        generate(args.directory, args.fpscr)
