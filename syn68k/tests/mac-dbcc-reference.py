#!/usr/bin/env python3
# +===================================================================
#   File:      mac-dbcc-reference.py
#
#   Summary:   Original DBcc comparisons through Apple's PowerPC 68K emulators.
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

"""Original DBcc comparisons through Apple's PowerPC 68K emulators.

All conditions and data-register counters execute original 68K code.
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
        (condition, reg, value, backward, ccr)
        for condition in range(16)
        for reg in range(8)
        for value in (0, 1, 0x8000, 0xFFFFFFFF)
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
    for condition, reg, value, backward, ccr in cases:
        labels, fixups = {}, []

        def branch(label):
            words.append(0x6000)
            fixups.append((len(words), label))
            words.append(0)

        def marker(value):
            words.extend([0x247C, 0, value])  # MOVEA preserves CCR/counter.
            branch("common")

        # Preserve the output pointer before testing D7 as a counter.
        words.extend([0x2847, 0x2A4F])  # A4=output, A5=caller SP.
        words.extend([0x203C | (reg << 9), value >> 16, value & 65535])
        if backward:
            branch("instruction")
            labels["target"] = len(words)
            marker(1)
        labels["instruction"] = len(words)
        words.extend([0x44FC, ccr, 0x50C8 | (condition << 8) | reg])
        fixups.append((len(words), "target"))
        words.append(0)
        marker(0)
        if not backward:
            labels["target"] = len(words)
            marker(1)
        labels["common"] = len(words)
        # Retain counter in A0 before snapshotting flags into D0.
        words.extend([0x2040 | reg, 0x42C0, 0x220F, 0x260D, 0x9283, 0x2408, 0x260A])
        words.extend([0x0280, 0, 31, 0x224C, 0x22C0, 0x22C1, 0x22C2, 0x22C3, 0x2E09])
        words.extend([0x6002, 0x4E71])
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
        application_name="68K DBcc Comparison",
        result_name="68K DBcc Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K DBcc",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong DBcc result length")
    for index, case in enumerate(cases):
        condition, _, counter, _, ccr = case
        marker = 0
        if not expected(condition, ccr):
            counter = (counter & 0xFFFF0000) | ((counter - 1) & 65535)
            marker = int(counter & 65535 != 65535)
        actual = struct.unpack_from(">4I", data, index * 16)
        result = ccr, 0, counter, marker
        if actual != result:
            raise ValueError(f"DBcc case {case}: {actual} != {result}")
    print(f"{len(cases)} classic DBcc counter/decision/SP/CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
