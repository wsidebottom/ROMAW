# +===================================================================
#   File:      mac-mcrfs-reference.py
#
#   Summary:   Original classic PPC MCRFS field-copy and exception-clear hardware probe.
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

"""Original classic PPC MCRFS field-copy and exception-clear hardware probe."""

import argparse
import json
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import generate_classic


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    code = [0x7D000026, 0x91010024]  # Preserve the caller's nonvolatile CR.
    for source in range(8):
        for destination in range(8):
            for bit in range(32):
                for complement in (0, 0xFFFFFFFF):
                    initial = (1 << bit) ^ complement
                    opcode = (63 << 26) | (destination << 23) | (source << 18) | (64 << 1)
                    cases.append(dict(opcode=opcode, inputs=[0, 0, 0x13579BDF], fpscr=initial))
                    code += [
                        (50 << 26) | (6 << 16) | 24,
                        0x39200010,  # Sixteen iterations warm native blocks.
                        (31 << 26) | (9 << 21) | (9 << 16) | (467 << 1),
                    ]
                    loop = len(code)
                    code += [
                        (63 << 26) | (255 << 17) | (711 << 1),
                        0x81060014,
                        0x7D0FF120,  # Load initial CR.
                        opcode,
                        0x7D000026,
                        # MTCRF, MCRFS, MFCR and MFFS form one native block.
                        (63 << 26) | (5 << 21) | (583 << 1),
                        0x91070004,  # Record full CR.
                        0x39000000,
                        0x91070000,
                        (54 << 26) | (5 << 21) | (7 << 16) | 8,
                    ]
                    code.append(0x42000000 | ((loop - len(code)) * 4 & 0xFFFC))
                    code += [
                        (14 << 26) | (6 << 21) | (6 << 16) | 32,
                        (14 << 26) | (7 << 21) | (7 << 16) | 16,
                    ]
    code += [0x81010024, 0x7D0FF120]
    generate_classic(
        directory,
        cases,
        code,
        application_name="PPC FPSCR Clear Probe",
        result_name="PPC FPSCR Clear Results",
        description="MCRFS field copy/exception clearing",
    )


def decode(directory, capture):
    root = Path(__file__).parent
    runpy.run_path(str(root / "mac-fpu-reference.py"))["decode"](directory, capture, binary=True)
    summary = runpy.run_path(str(root / "mac-fpr-reference.py"))["status"]
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    data = capture.read_bytes()
    for index, case in enumerate(cases):
        opcode = case["opcode"]
        incoming = summary(case["fpscr"] & ~0x800)
        source, destination = (opcode >> 18) & 7, (opcode >> 23) & 7
        nibble = (incoming >> (28 - 4 * source)) & 15
        shift = 28 - 4 * destination
        cr = (case["inputs"][2] & ~(15 << shift)) | (nibble << shift)
        expected = incoming
        for bit in range(source * 4, source * 4 + 4):
            if bit == 0 or 3 <= bit <= 12 or 21 <= bit <= 23:
                expected &= ~(0x80000000 >> bit)
        expected = summary(expected)
        value, fpscr = struct.unpack_from(">QQ", data, index * 16)
        if value != cr or fpscr & 0xFFFFFFFF != expected:
            raise ValueError(
                f"MCRFS mismatch at {index}: "
                f"{value:08x}/{fpscr & 0xFFFFFFFF:08x} "
                f"expected {cr:08x}/{expected:08x}"
            )
    print(f"{len(cases)} MCRFS CR/FPSCR hardware cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    args = parser.parse_args()
    if args.decode_binary:
        decode(args.directory, args.decode_binary)
    else:
        generate(args.directory)
