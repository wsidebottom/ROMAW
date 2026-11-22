#!/usr/bin/env python3
# +===================================================================
#   File:      mac-lea-reference.py
#
#   Summary:   Original LEA instruction comparisons through classic Mac Mixed Mode.
#
#   Classes:   None declared.
#
#   Functions: vectors, signed, expected, generate, check
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Original LEA instruction comparisons through classic Mac Mixed Mode.

The PowerPC wrapper calls original 68K machine code. These are Apple emulator
comparisons on PowerPC Macs, not physical MC68040 bus/FPU measurements.
"""

import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    # A7 destinations are covered by the isolated CPU fixture, without OS interrupts.
    operands = [(2, reg, 0) for reg in range(8)]
    operands += [(5, reg, offset) for reg in range(8) for offset in (-32768, -1, 0, 1, 32767)]
    operands += [(6, reg, extension) for reg in range(8) for extension in (0x3A04, 0x3AFF, 0x3B20)]
    operands += [(7, reg, value) for reg in (0, 1) for value in (0, 1, 0x7FFF, 0x8000, 0xFFFFFFFF)]
    operands += [(7, 2, offset) for offset in (-32768, -1, 0, 1, 32767)]
    operands += [(7, 3, extension) for extension in (0x3A04, 0x3AFF, 0x3B20)]
    return [
        (destination, mode, reg, extension, ccr)
        for destination in range(7)
        for mode, reg, extension in operands
        for ccr in range(32)
    ]


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def expected(case):
    destination, mode, reg, extension, ccr = case
    base = 0 if reg == 7 or mode == 7 else 0x12340000 + reg * 256
    if mode == 2:
        address = base
    elif mode == 5:
        address = base + extension
    elif mode == 6 or (mode == 7 and reg == 3):
        address = base + 8 + (-16 if extension & 256 else signed(extension, 8))
        if mode == 7:
            address += 2
    elif reg == 0:
        address = signed(extension, 16)
    elif reg == 1:
        address = extension
    else:
        address = 2 + extension
    unchanged = (address if destination == reg else base) if mode != 7 else 0
    return ccr, 0, address & 0xFFFFFFFF, unchanged & 0xFFFFFFFF


def generate(directory):
    # Save the 68K C ABI's nonvolatile registers; the output pointer is the
    # sole C-stack argument, above the saved registers and return address.
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for destination, mode, reg, extension, ccr in cases:
        for areg in range(7):
            words.extend([0x207C | (areg << 9), 0x1234, areg * 256])
        words.extend([0x7604, 0x2C0F])  # D3=4, D6=original SP.
        if mode == 7 and reg in (2, 3):
            # Retain the tested opcode address in D5 before replacing An.
            words.extend([0x45FA, 8, 0x2A0A])
        words.extend([0x44FC, ccr, 0x41C0 | (destination << 9) | (mode << 3) | reg])
        if mode != 2:
            if mode == 7 and reg == 1:
                words.append((extension >> 16) & 65535)
            words.append(extension & 65535)
            if (mode == 6 or (mode == 7 and reg == 3)) and extension & 256:
                words.append(0xFFF0)
        words.extend([0x42C0, 0x220F, 0x2408 | destination])  # CCR, SP and result An.
        if mode != 7:
            words.append(0x2808 | reg)  # Source An after the instruction.
            if reg == 7:
                words.extend([0x9486, 0x9886])  # Normalize EA and source SP.
        else:
            words.append(0x7800)
            if reg in (2, 3):
                words.append(0x9485)  # Normalize EA against the retained opcode address.
        words.extend(
            [
                0x2E46,
                0x9286,
                0x0280,
                0,
                31,  # Restore SP, normalize SP/CCR.
                0x2247,
                0x22C0,
                0x22C1,
                0x22C2,
                0x22C4,
                0x2E09,
                0x6002,
                0x4E71,
            ]
        )
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
        application_name="68K LEA Comparison",
        result_name="68K LEA Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K LEA",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong LEA result length")
    for index, case in enumerate(cases):
        actual = struct.unpack_from(">4I", data, index * 16)
        result = expected(case)
        if actual != result:
            raise ValueError(f"LEA case {case}: {actual} != {result}")
    print(f"{len(cases)} classic 68K LEA values and CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
