#!/usr/bin/env python3
# +===================================================================
#   File:      mac-movep-reference.py
#
#   Summary:   Original MOVEP comparisons through Apple's PowerPC 68K emulators.
#
#   Classes:   None declared.
#
#   Functions: vectors, generate, check
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Original MOVEP comparisons through Apple's PowerPC 68K emulators."""
import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    return [
        (reg, address, wide, writing, alignment, displacement, ccr)
        for reg in range(8)
        for address in range(2)
        for wide in range(2)
        for writing in range(2)
        for alignment in range(2)
        for displacement in (-129, 128)
        for ccr in range(32)
    ]


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x286F, 48, 0x2A4F]
    cases = vectors()
    for reg, address, wide, writing, alignment, displacement, ccr in cases:
        # A4=output, A5=caller SP; each case uses a private stack scratch area.
        words.extend([0x4FEF, 0xFFF4, 0x45EF, alignment])
        words.extend([0x41EA | (address << 9), (-displacement) & 65535])
        words.extend([0x24BC, 0x125A, 0x345A, 0x257C, 0x565A, 0x785A, 4])
        words.extend([0x203C | (reg << 9), 0xA1B2, 0xC3D4])
        words.extend(
            [
                0x44FC,
                ccr,
                0x0108 | (reg << 9) | address | (wide << 6) | (writing << 7),
                displacement & 65535,
            ]
        )
        # MOVEA snapshots the tested data register without changing its CCR.
        words.extend([0x2C40 | reg, 0x42C0, 0x0280, 0, 31])
        words.extend([0x28C0, 0x28CE, 0x28DA, 0x28DA, 0x2E4D])
    words.extend([0x4CDF, 0x7CFC, 0x4E75])
    body = [
        0x7CC33378,  # mr r3,r6: raw 68K procedure address.
        0x388000C1,  # C-stack, void result, one four-byte argument.
        0x7CE53B78,  # mr r5,r7: output pointer.
        0x819E0024,  # CallUniversalProc, appended import slot 7.
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
    ]
    generate_classic(
        directory,
        cases,
        body,
        application_name="68K MOVEP Comparison",
        result_name="68K MOVEP Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K MOVEP",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong MOVEP result length")
    for index, case in enumerate(cases):
        _, _, wide, writing, _, _, ccr = case
        memory = bytearray.fromhex("125a345a565a785a")
        value = 0xA1B2C3D4
        if writing:
            source = value.to_bytes(4, "big") if wide else bytes.fromhex("c3d4")
            memory[0 : len(source) * 2 : 2] = source
        else:
            value = 0x12345678 if wide else 0xA1B21234
        result = (ccr, value, *struct.unpack(">2I", memory))
        actual = struct.unpack_from(">4I", data, index * 16)
        if actual != result:
            raise ValueError(f"MOVEP case {case}: {actual} != {result}")
    print(f"{len(cases)} classic MOVEP register/memory/CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
