# +===================================================================
#   File:      mac-ni-conversion-reference.py
#
#   Summary:   Original comparison/conversion NI audit probe for physical PowerPC Macs.
#
#   Classes:   None declared.
#
#   Functions: vectors, generate, model, defined_result, check
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original comparison/conversion NI audit probe for physical PowerPC Macs.

NI-off rows are checked against rational/NaN models. NI-on rows are classified
against both ordinary and signed-zero-input models; neither physical Mac is a
750, so those measurements do not choose the 750 implementation policy.
"""

import argparse
import json
from pathlib import Path
import runpy
import struct

from classic_fpu_reference import generate_classic

integer = runpy.run_path(str(Path(__file__).with_name("integer-reference.py")))["expected"]
compare = runpy.run_path(str(Path(__file__).with_name("compare-reference.py")))["expected"]


def vectors():
    positive = [
        0,
        1,
        0x000FFFFFFFFFFFFF,
        0x0010000000000000,
        0x3FE0000000000000,
        0x3FF8000000000000,
        0x41E0000000000000,
        0x7FF0000000000000,
        0x7FF0000000000001,
        0x7FF8000000000123,
    ]
    values = positive + [v | (1 << 63) for v in positive]
    cases = []
    for operation in (0, 32, 14, 15):
        for a in values:
            for b in [0, 1, 0x8000000000000001, 0x0010000000000000] if operation in (0, 32) else [a]:
                for mode in range(4):
                    for ni in (0, 4):
                        for rc in (0,) if operation in (0, 32) else (0, 1):
                            code = (
                                ((63 << 26) | (1 << 16) | (2 << 11) | (operation << 1))
                                if operation in (0, 32)
                                else ((63 << 26) | (4 << 21) | (2 << 11) | (operation << 1) | rc)
                            )
                            cases.append(
                                dict(
                                    opcode=code,
                                    inputs=[a, b, 0],
                                    fpscr=0x15000 | mode | ni,
                                )
                            )
    return cases


def generate(directory):
    cases = vectors()
    body = []
    for case in cases:
        for reg, offset in ((0, 24), (1, 0), (2, 8), (4, 16)):
            body.append((50 << 26) | (reg << 21) | (6 << 16) | offset)
        body.extend(
            [
                (15 << 26) | (8 << 21) | 0x1357,
                (24 << 26) | (8 << 21) | (8 << 16) | 0x9BDF,
                (31 << 26) | (8 << 21) | (0xC0 << 12) | (144 << 1),
                (63 << 26) | (255 << 17) | (711 << 1),
                case["opcode"],
                (63 << 26) | (5 << 21) | (583 << 1),
                (54 << 26) | (5 << 21) | (7 << 16),
                (54 << 26) | (4 << 21) | (7 << 16) | 8,
                (31 << 26) | (8 << 21) | (19 << 1),
                (36 << 26) | (8 << 21) | (7 << 16) | 8,
                (14 << 26) | (6 << 21) | (6 << 16) | 32,
                (14 << 26) | (7 << 21) | (7 << 16) | 16,
            ]
        )
    generate_classic(
        directory,
        cases,
        body,
        application_name="PPC NI Compare Convert Probe",
        result_name="PPC NI Compare Convert Results",
        description="NI comparison/conversion audit",
    )


def model(case, flush):
    code, initial = case["opcode"], case["fpscr"]
    a, b, _ = case["inputs"]
    if flush:
        a = a & (1 << 63) if a & 0x7FFFFFFFFFFFFFFF < 1 << 52 else a
        b = b & (1 << 63) if b & 0x7FFFFFFFFFFFFFFF < 1 << 52 else b
    operation = (code >> 1) & 1023
    if operation in (0, 32):
        condition, status = compare(a, b, operation == 32, False)
        status |= initial & ~0xF000
        cr = (condition << 28) | 0x03000000
        value = 0
    else:
        value, _, _, status = integer(b, 1 if operation == 15 else initial & 3)
        status = (status & ~3) | initial
        cr = 0x10000000 | ((status >> 4) & 0x0F000000 if code & 1 else 0x03000000)
    return status, cr, value


def defined_result(case, data, index):
    _, status, cr, value = struct.unpack_from(">4I", data, index * 16)
    # Integer conversion leaves FPSCR[FPRF] undefined (MPCFPE, FCTIW/FCTIWZ).
    # MFFS's high word, the conversion's high word and nonvolatile CR fields
    # are also excluded. CR0/CR1 and the low integer result remain checked.
    mask = ~0x1F000 if ((case["opcode"] >> 1) & 1023) in (14, 15) else -1
    return status & mask, cr & 0xFF000000, value


def check(directory, capture, reference=None):
    cases = vectors()
    data = capture.read_bytes()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong NI comparison/conversion capture length")
    baseline = reference.read_bytes() if reference else None
    if baseline is not None and len(baseline) != len(data):
        raise ValueError("NI reference capture length differs")
    ordinary = flushed = different = 0
    rows = []
    for index, case in enumerate(cases):
        _, status, cr, value = struct.unpack_from(">4I", data, index * 16)
        actual = defined_result(case, data, index)
        mask = ~0x1F000 if ((case["opcode"] >> 1) & 1023) in (14, 15) else -1
        plain, zeroed = (
            (expected[0] & mask, expected[1], expected[2]) for expected in (model(case, False), model(case, True))
        )
        if baseline is not None and actual != defined_result(case, baseline, index):
            raise ValueError(f"Hardware comparison differs at row {index}")
        if not case["fpscr"] & 4:
            if actual != plain:
                raise ValueError(f"IEEE row {index}: {actual} != {plain}")
        else:
            ordinary += actual == plain
            flushed += actual == zeroed
            different += plain != zeroed
        rows.append(dict(case=case, status=status, cr=cr, value=value))
    if baseline is not None:
        print(f"{len(cases)} defined results matched the reference capture")
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "ni-compare-convert.json").write_text(json.dumps(rows, indent=2))
    print(f"{len(cases)//2} IEEE comparison/conversion rows matched")
    print(
        f"NI rows: {ordinary} ordinary-model matches, {flushed} zero-input-model "
        f"matches; {different} rows distinguish the models"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    if args.reference and not args.decode:
        parser.error("--reference requires --decode")
    if args.decode:
        check(args.directory, args.decode, args.reference)
    else:
        generate(args.directory)
