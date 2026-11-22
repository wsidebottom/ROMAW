#!/usr/bin/env python3
# +===================================================================
#   File:      mac-bsr-reference.py
#
#   Summary:   Original BSR comparisons through Apple's PowerPC 68K emulators.
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

"""Original BSR comparisons through Apple's PowerPC 68K emulators.

Byte/word/long relative calls run in both directions with varied padding.
"""
import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    return [
        (size, backward, padding, ccr)
        for size in (1, 2, 4)
        for backward in (0, 1)
        for padding in (0, 8, 32, 64)
        for ccr in range(32)
    ]


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for size, backward, padding, ccr in cases:
        labels, fixups = {}, []

        def branch(label):
            words.append(0x6000)
            fixups.append((len(words), label))
            words.append(0)

        def target():
            labels["target"] = len(words)
            words.extend([0x42C0, 0x220F, 0x2417, 0x44C0, 0x4E75])

        words.extend([0x2C0F, 0x45FA])
        fixups.append((len(words), "return"))
        words.extend([0, 0x2A0A])
        if backward:
            branch("instruction")
            target()
            words.extend([0x4E71] * (padding // 2))
        labels["instruction"] = len(words)
        words.extend([0x44FC, ccr])
        opcode = len(words)
        words.append(0x6100 if size != 4 else 0x61FF)
        if size != 1:
            words.extend([0] * (size // 2))
        labels["return"] = len(words)
        words.append(0x42C4)
        if not backward:
            branch("common")
            words.extend([0x4E71] * (padding // 2))
            target()
        labels["common"] = len(words)
        words.extend([0x9286, 0x9485, 0x0280, 0, 31, 0x0284, 0, 31])
        words.extend([0x2247, 0x22C0, 0x22C1, 0x22C2, 0x22C4, 0x2E09, 0x6002, 0x4E71])
        displacement = (labels["target"] - opcode - 1) * 2
        if size == 1:
            assert -128 <= displacement <= 127 and displacement not in (0, -1)
            words[opcode] |= displacement & 255
        elif size == 2:
            words[opcode + 1] = displacement & 65535
        else:
            words[opcode + 1] = (displacement >> 16) & 65535
            words[opcode + 2] = displacement & 65535
        for index, label in fixups:
            words[index] = ((labels[label] - index) * 2) & 65535
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
        application_name="68K BSR Comparison",
        result_name="68K BSR Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K BSR",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong BSR result length")
    for index, case in enumerate(cases):
        actual = struct.unpack_from(">4I", data, index * 16)
        expected = case[3], 0xFFFFFFFC, 0, case[3]
        if actual != expected:
            raise ValueError(f"BSR case {case}: {actual} != {expected}")
    print(f"{len(cases)} classic BSR stack/PC/CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
