#!/usr/bin/env python3
# +===================================================================
#   File:      mac-branch-reference.py
#
#   Summary:   Original relative branch comparisons through Apple's PowerPC 68K emulators.
#
#   Classes:   None declared.
#
#   Functions: vectors, expected, generate, check
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Original relative branch comparisons through Apple's PowerPC 68K emulators.

All conditions, displacement widths and directions are exercised.
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
        (condition, size, backward, ccr)
        for condition in range(16)
        if condition != 1
        for size in (1, 2, 4)
        for backward in (0, 1)
        for ccr in range(32)
    ]


def expected(condition, ccr):
    c, v, z, n = (bool(ccr & bit) for bit in (1, 2, 4, 8))
    return int(
        [
            True,
            False,
            not (c or z),
            c or z,
            not c,
            c,
            not z,
            z,
            not v,
            v,
            not n,
            n,
            n == v,
            n != v,
            not z and n == v,
            z or n != v,
        ][condition]
    )


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for condition, size, backward, ccr in cases:
        labels, fixups = {}, []

        def branch(label):
            words.append(0x6000)
            fixups.append((len(words), label))
            words.append(0)

        def marker(value):
            words.extend([0x207C, 0, value])  # MOVEA leaves CCR unchanged.
            branch("common")

        words.append(0x2C0F)
        if backward:
            branch("instruction")
            labels["target"] = len(words)
            marker(1)
        labels["instruction"] = len(words)
        words.extend([0x44FC, ccr])
        opcode = len(words)
        words.append(0x6000 | (condition << 8) | (255 if size == 4 else 0))
        if size != 1:
            words.extend([0] * (size // 2))
        marker(0)
        if not backward:
            labels["target"] = len(words)
            marker(1)
        labels["common"] = len(words)
        words.extend([0x42C0, 0x42C4, 0x220F, 0x2408, 0x9286])
        words.extend([0x0280, 0, 31, 0x0284, 0, 31])
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
        application_name="68K Relative Branch Comparison",
        result_name="68K Branch Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K Branch",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong branch result length")
    for index, case in enumerate(cases):
        condition, _, _, ccr = case
        actual = struct.unpack_from(">4I", data, index * 16)
        result = ccr, 0, expected(condition, ccr), ccr
        if actual != result:
            raise ValueError(f"Branch case {case}: {actual} != {result}")
    print(f"{len(cases)} classic branch decision/SP/CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
