# +===================================================================
#   File:      mac-compare-reference.py
#
#   Summary:   Original classic PPC comparison/CR/FPSCR hardware probe.
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

"""Original classic PPC comparison/CR/FPSCR hardware probe.

NI denormal inputs are excluded: the 603 is not an oracle for the 750's
implementation-dependent non-IEEE operand handling.
"""

import argparse
import json
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import generate_classic


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    positive = (
        0,
        1,
        0x000FFFFFFFFFFFFF,
        0x0010000000000000,
        0x3FF0000000000000,
        0x7FEFFFFFFFFFFFFF,
        0x7FF0000000000000,
        0x7FF0000000000001,
        0x7FF7FFFFFFFFFFFF,
        0x7FF8000000000123,
    )
    values = positive + tuple(x | (1 << 63) for x in positive)
    cases = []
    # Save caller CR, including the nonvolatile CR2..CR4 fields.
    code = [0x7D000026, 0x91010024]
    for a in values:
        for b in values:
            for ordered in (0, 1):
                for field in range(8):
                    for seed in (0x00070000, 0x01070080, 0x00070084, 0x110F0047):
                        if seed & 4 and any(0 < (x & ((1 << 63) - 1)) < (1 << 52) for x in (a, b)):
                            continue
                        initial = seed | (field & 3)
                        opcode = (63 << 26) | (field << 23) | (1 << 16) | (2 << 11) | (64 if ordered else 0)
                        cases.append(dict(opcode=opcode, inputs=[a, b, 0x13579BDF], fpscr=initial))
                        code += [
                            (50 << 26) | (6 << 16) | 24,
                            (63 << 26) | (255 << 17) | (711 << 1),
                            (50 << 26) | (1 << 21) | (6 << 16),
                            (50 << 26) | (2 << 21) | (6 << 16) | 8,
                            0x81060014,  # lwz r8,20(r6): initial CR.
                            0x7D0FF120,  # mtcrf 255,r8.
                            opcode,
                            0x7D200026,  # mfcr r9.
                            0x91270004,  # stw r9,4(r7).
                            0x39200000,
                            0x91270000,
                            (63 << 26) | (5 << 21) | (583 << 1),
                            (54 << 26) | (5 << 21) | (7 << 16) | 8,
                            (14 << 26) | (6 << 21) | (6 << 16) | 32,
                            (14 << 26) | (7 << 21) | (7 << 16) | 16,
                        ]
    code += [0x81010024, 0x7D0FF120]
    generate_classic(
        directory,
        cases,
        code,
        application_name="PPC Compare Probe",
        result_name="PPC Compare Results",
        description="floating comparison/CR/FPSCR",
    )


def decode(directory, capture):
    root = Path(__file__).parent
    runpy.run_path(str(root / "mac-fpu-reference.py"))["decode"](directory, capture, binary=True)
    oracle = runpy.run_path(str(root / "compare-reference.py"))["expected"]
    summary = runpy.run_path(str(root / "mac-fpr-reference.py"))["status"]
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    output = capture.read_bytes()
    for index, case in enumerate(cases):
        opcode, initial = case["opcode"], summary(case["fpscr"])
        a, b, cr = case["inputs"]
        condition, flags = oracle(a, b, bool(opcode & 64), bool(initial & 0x80))
        raised = flags & 0x01080000
        expected = initial | raised
        if raised & ~initial:
            expected |= 0x80000000
        expected = summary(expected) & ~0xF000 | (condition << 12)
        shift = 28 - 4 * ((opcode >> 23) & 7)
        cr = (cr & ~(15 << shift)) | (condition << shift)
        value, fpscr = struct.unpack_from(">QQ", output, index * 16)
        if value != cr or (fpscr & 0xFFFFFFFF) != expected:
            raise ValueError(
                f"Comparison mismatch at {index}: "
                f"{value:08x}/{fpscr & 0xFFFFFFFF:08x} "
                f"expected {cr:08x}/{expected:08x}"
            )
    print(f"{len(cases)} hardware comparison/CR/FPSCR cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    args = parser.parse_args()
    if args.decode_binary:
        decode(args.directory, args.decode_binary)
    else:
        generate(args.directory)
