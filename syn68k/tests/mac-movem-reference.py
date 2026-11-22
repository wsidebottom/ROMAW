#!/usr/bin/env python3
# +===================================================================
#   File:      mac-movem-reference.py
#
#   Summary:   Original MOVEM comparisons through Apple's PowerPC 68K emulators.
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

"""Original MOVEM comparisons through Apple's PowerPC 68K emulators."""
import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    return [
        (wide, load, automatic, mask, alignment, ccr)
        for wide in range(2)
        for load in range(2)
        for automatic in range(2)
        for mask in (*range(16), 16, 17, 18, 19, 28)
        for alignment in range(2)
        for ccr in range(32)
    ]


INITIAL_MEMORY = [
    0x80011234,
    0xFFFFABCD,
    0x7FFF8000,
    0x4321FEDC,
    0x5A5A5A5A,
    0x5A5A5A5A,
]
INITIAL_REGISTERS = [0xA1B2C3D4, 0x55667788, 0x87654321, 0x1234FEDC]


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x286F, 48, 0x2A4F]
    cases = vectors()
    for wide, load, automatic, mask, alignment, ccr in cases:
        words.extend([0x4FEF, 0xFFC0, 0x4DEF, 16 + alignment])
        for offset, value in enumerate(INITIAL_MEMORY):
            words.extend([0x2D7C, value >> 16, value & 65535, offset * 4])
        for opcode, value in zip((0x203C, 0x223C, 0x207C, 0x227C), INITIAL_REGISTERS):
            words.extend([opcode, value >> 16, value & 65535])
        predec = automatic and not load
        mode = (3 if load else 4) if automatic else 2
        register_mask = sum(((mask >> bit) & 1) << reg for bit, reg in enumerate((0, 1, 8, 9, 10)))
        if predec:
            register_mask = int(f"{register_mask:016b}"[::-1], 2)
        words.extend([0x45EE, 16 if predec else 0, 0x44FC, ccr])
        words.extend([0x4880 | (load << 10) | (wide << 6) | (mode << 3) | 2, register_mask])
        words.extend([0x42C2, 0x0282, 0, 31, 0x28C2, 0x260A])
        if not (load and not automatic and mask & 16):
            words.append(0x968E)  # Normalize a retained base against A6.
        words.extend([0x28C3, 0x28C0, 0x28C1, 0x28C8, 0x28C9])
        if not load and mask & 16:
            # Normalize the stored base-register value, preserving word wrap.
            # The CCR and registers have already been captured above.
            size = 4 if wide else 2
            start = 16 - mask.bit_count() * size if predec else 0
            slot = start + (mask & 15).bit_count() * size
            words.extend(
                [
                    0x242E if wide else 0x342E,
                    slot,
                    0x948E if wide else 0x944E,
                    0x2D42 if wide else 0x3D42,
                    slot,
                ]
            )
        for offset in range(0, 24, 4):
            words.extend([0x28EE, offset])
        words.extend([0x2E4D, 0x6002, 0x4E71])
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
        [case for case in cases for _ in range(3)],
        body,
        application_name="68K MOVEM Comparison",
        result_name="68K MOVEM Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K MOVEM",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 48:
        raise ValueError("Wrong MOVEM result length")
    for index, case in enumerate(cases):
        wide, load, automatic, mask, _, ccr = case
        size = 4 if wide else 2
        count = mask.bit_count()
        memory = bytearray(struct.pack(">6I", *INITIAL_MEMORY))
        registers = INITIAL_REGISTERS.copy()
        predec = automatic and not load
        cursor = 16 - count * size if predec else 0
        loaded_base = None
        for reg in range(5):
            if mask & (1 << reg):
                if load:
                    value = int.from_bytes(memory[cursor : cursor + size], "big", signed=not wide)
                    if reg == 4:
                        loaded_base = value & 0xFFFFFFFF
                    else:
                        registers[reg] = value & 0xFFFFFFFF
                else:
                    source = (16 - size if predec else 0) if reg == 4 else registers[reg]
                    memory[cursor : cursor + size] = (source & ((1 << (size * 8)) - 1)).to_bytes(size, "big")
                cursor += size
        address = 16 - count * size if predec else count * size if automatic else 0
        if load and not automatic and loaded_base is not None:
            address = loaded_base
        expected = (ccr, address, *registers, *struct.unpack(">6I", memory))
        actual = struct.unpack_from(">12I", data, index * 48)
        if actual != expected:
            raise ValueError(f"MOVEM case {case}: {actual} != {expected}")
    print(f"{len(cases)} classic MOVEM register/memory/address/CCR cases matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
