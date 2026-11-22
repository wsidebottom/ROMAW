#!/usr/bin/env python3
# +===================================================================
#   File:      mac-control-transfer-reference.py
#
#   Summary:   Original JMP/JSR comparisons through Apple's PowerPC 68K emulators.
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

"""Original JMP/JSR comparisons through Apple's PowerPC 68K emulators.

Real-Mac cases use address registers A0-A6 and PC-relative targets. Absolute
and source-SP forms are covered by the isolated CPU fixture.
"""
import argparse
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc/tests"))
from classic_fpu_reference import generate_classic


def vectors():
    operands = [(2, reg, 0) for reg in range(7)]
    operands += [(5, reg, offset) for reg in range(7) for offset in (-32768, -1, 0, 1, 32767)]
    operands += [(6, reg, extension) for reg in range(7) for extension in (0x3A04, 0x3AFF, 0x3B20, 0x3B22)]
    operands += [(7, 2, 0), (7, 3, 0x3A00), (7, 3, 0x3B20)]
    return [
        (call, mode, reg, extension, ccr) for call in range(2) for mode, reg, extension in operands for ccr in range(32)
    ]


def generate(directory):
    words = [0x48E7, 0x3F3E, 0x2E2F, 48]
    cases = vectors()
    for call, mode, reg, extension, ccr in cases:
        labels, fixups = {}, []

        def lea(areg, label):
            words.append(0x41FA | (areg << 9))
            fixups.append((len(words), label, 0, "word"))
            words.append(0)

        def branch(label):
            words.append(0x6000)
            fixups.append((len(words), label, 0, "word"))
            words.append(0)

        words.append(0x2C0F)  # D6 = caller SP.
        lea(2, "return")
        words.append(0x2A0A)  # D5 = expected return address.
        words.append(0x7604)  # D3 = scaled index value 4.
        if mode == 6 and extension == 0x3B22:
            # Store a real target pointer in this case's dedicated data slot.
            lea(2, "target")
            words.extend([0x45EA, 0xFFE0])  # LEA -32(A2),A2.
            lea(3, "pointer")
            words.append(0x268A)  # MOVE.L A2,(A3).
            lea(reg, "pointer")
            words.extend([0xD1FC | (reg << 9), 0xFFFF, 0xFFE8])
        elif mode != 7:
            lea(reg, "target")
            offset = (
                0
                if mode == 2
                else (
                    extension
                    if mode == 5
                    else (
                        -8
                        if extension & 256
                        else 8 + (extension & 255 if extension & 255 < 128 else (extension & 255) - 256)
                    )
                )
            )
            adjust = (-offset) & 0xFFFFFFFF
            words.extend([0xD1FC | (reg << 9), adjust >> 16, adjust & 65535])
        elif reg == 3 and not extension & 256:
            words.append(0x7600)  # Brief PC index uses D3=0 and live displacement.
        words.extend([0x44FC, ccr, (0x4E80 if call else 0x4EC0) | (mode << 3) | reg])
        if mode == 5:
            words.append(extension & 65535)
        elif mode == 6:
            words.append(extension)
            if extension & 256:
                words.append(16 if extension == 0x3B22 else 0xFFF0)
                if extension == 0x3B22:
                    words.append(32)
        elif mode == 7 and reg == 2:
            fixups.append((len(words), "target", 0, "word"))
            words.append(0)
        elif mode == 7 and reg == 3:
            index = len(words)
            words.append(extension)
            if extension & 256:
                # PC base is the first extension, not its following BD word.
                fixups.append((len(words), "target", -6, "word"))
                words.append(0)
            else:
                fixups.append((index, "target", 0, "brief"))
        labels["return"] = len(words)
        words.append(0x42C4)  # Snapshot return CCR before any arithmetic.
        branch("common")
        labels["target"] = len(words)
        words.extend([0x42C0, 0x42C4, 0x220F])  # Snapshot CCR before SP data moves.
        if call:
            words.extend([0x2417, 0x44C0, 0x4E75])  # Saved PC; restore CCR, then RTS.
        else:
            words.append(0x7400)  # No saved PC for JMP.
            branch("common")
        labels["common"] = len(words)
        words.append(0x9286)  # Normalize entry SP against caller SP.
        if call:
            words.append(0x9485)  # Normalize saved PC against expected return.
        words.extend([0x0280, 0, 31, 0x0284, 0, 31])
        words.extend([0x2247, 0x22C0, 0x22C1, 0x22C2, 0x22C4, 0x2E09])
        words.append(0x6006)
        labels["pointer"] = len(words)
        words.extend([0, 0, 0x4E71])
        for index, label, adjust, kind in fixups:
            displacement = (labels[label] - index) * 2 + adjust
            if kind == "brief":
                assert -128 <= displacement <= 127
                words[index] |= displacement & 255
            else:
                assert -32768 <= displacement <= 32767
                words[index] = displacement & 65535
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
        application_name="68K JMP-JSR Comparison",
        result_name="68K JMP-JSR Results",
        input_data=struct.pack(">" + "H" * len(words), *words),
        description="68K JMP-JSR",
        extra_imports=("CallUniversalProc",),
    )


def check(path):
    data = path.read_bytes()
    cases = vectors()
    if len(data) != len(cases) * 16:
        raise ValueError("Wrong JMP/JSR result length")
    for index, case in enumerate(cases):
        call, _, _, _, ccr = case
        actual = struct.unpack_from(">4I", data, index * 16)
        expected = ccr, (0xFFFFFFFC if call else 0), 0, ccr
        if actual != expected:
            raise ValueError(f"JMP/JSR case {case}: {actual} != {expected}")
    print(f"{len(cases)} classic JMP/JSR stack/PC/CCR results matched")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode", type=Path)
    args = parser.parse_args()
    if args.decode:
        check(args.decode)
    else:
        generate(args.directory)
