# +===================================================================
#   File:      mac-fpu-reference.py
#
#   Summary:   Generate an original standalone PPC Mach-O floating-point hardware probe.
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

"""Generate an original standalone PPC Mach-O floating-point hardware probe.

No compiler, SDK, Apple runtime files or guest libraries are needed. Run the
executable on a PowerPC Mac; capture stdout as bytes or through od -An -v -tx1.
The output contains big-endian result/FPSCR pairs, in cases.json order. Hardware
model matters: a G5 result is not automatically a 750 architectural requirement.
"""

import argparse
import json
from pathlib import Path
import struct


def generate(directory, classic=False):
    directory.mkdir(parents=True, exist_ok=True)
    cases = []
    # Operations deliberately avoid optional square-root instructions.
    operations = [
        (63, 21),
        (59, 21),
        (63, 25),
        (59, 25),
        (63, 18),
        (59, 18),
        (63, 12),
        (59, 24),
        (63, 26),
        (63, 29),
        (59, 29),
    ]
    values = [
        0,
        0x8000000000000000,
        1,
        0x000FFFFFFFFFFFFF,
        0x0010000000000000,
        0x3800000000000000,
        0x3810000000000000,
        0x36A0000000000000,
        0x3FE0000000000000,
        0x3FF0000000000000,
        0x4000000000000000,
        0x7FF0000000000000,
        0x7FF8000012345678,
        0x7FF0000012345678,
    ]
    code = []

    def immediate(reg, value):
        code.extend(
            [
                (15 << 26) | (reg << 21) | (value >> 16),
                (24 << 26) | (reg << 21) | (reg << 16) | (value & 65535),
            ]
        )

    immediate(6, 0)  # Input and output addresses patched below.
    immediate(7, 0)
    for primary, operation in operations:
        for a in values:
            for sign in range(2):
                for rn in range(4):
                    for ni in range(2):
                        inputs = [
                            a ^ (sign << 63),
                            0x3FE0000000000000,
                            0x3FF0000000000000,
                        ]
                        if operation in (12, 24, 26):
                            inputs[1] = inputs[0]
                        status = rn | (ni << 2)
                        opcode = (primary << 26) | (4 << 21) | (operation << 1)
                        if operation in (12, 24, 26):
                            opcode |= 2 << 11
                        elif operation == 25:
                            opcode |= (1 << 16) | (3 << 6)
                        else:
                            opcode |= (1 << 16) | (2 << 11)
                            if operation == 29:
                                opcode |= 3 << 6
                        cases.append(dict(opcode=opcode, inputs=inputs, fpscr=status))
                        for reg in range(4):
                            code.append((50 << 26) | (reg << 21) | (6 << 16) | ((24 if reg == 0 else (reg - 1) * 8)))
                        code.append((63 << 26) | (255 << 17) | (711 << 1))
                        code.append(opcode)
                        code.append((63 << 26) | (5 << 21) | (583 << 1))
                        code.append((54 << 26) | (4 << 21) | (7 << 16))
                        code.append((54 << 26) | (5 << 21) | (7 << 16) | 8)
                        code.append((14 << 26) | (6 << 21) | (6 << 16) | 32)
                        code.append((14 << 26) | (7 << 21) | (7 << 16) | 16)
    if classic:
        from classic_fpu_reference import generate_classic

        generate_classic(directory, cases, code[4:])
        return
    immediate(0, 4)  # Darwin write(1, output, size).
    immediate(3, 1)
    output_fixup = len(code)
    immediate(4, 0)
    immediate(5, len(cases) * 16)
    code.extend([0x44000002, 0x60000000])  # Kernel skips the error-branch slot.
    immediate(0, 1)
    immediate(3, 0)
    code.extend([0x44000002, 0x60000000])
    offset, base = 0x1000, 0x1000
    input_offset = (offset + len(code) * 4 + 7) & ~7
    inputs = b"".join(struct.pack(">4Q", *c["inputs"], c["fpscr"]) for c in cases)
    file_size = input_offset + len(inputs)
    output_offset = (file_size + 4095) & ~4095
    memory_size = (output_offset + len(cases) * 16 + 8191) & ~4095
    for fixup, reg, address in [
        (0, 6, base + input_offset),
        (2, 7, base + output_offset),
        (output_fixup, 4, base + output_offset),
    ]:
        code[fixup] = (15 << 26) | (reg << 21) | (address >> 16)
        code[fixup + 1] = (24 << 26) | (reg << 21) | (reg << 16) | (address & 65535)
    header = struct.pack(">7I", 0xFEEDFACE, 18, 0, 2, 2, 56 + 176, 0)
    segment = struct.pack(">II16s8I", 1, 56, b"__TEXT", base, memory_size, 0, file_size, 7, 7, 0, 0)
    state = [0] * 40
    state[0] = base + offset
    state[3] = base + memory_size - 16
    thread = struct.pack(">4I40I", 5, 176, 1, 40, *state)
    executable = (header + segment + thread).ljust(offset, b"\0")
    executable += struct.pack(f">{len(code)}I", *code)
    executable = executable.ljust(input_offset, b"\0") + inputs
    (directory / "ppc-fpu-probe").write_bytes(executable)
    (directory / "cases.json").write_text(json.dumps(cases), encoding="utf-8")
    print(f"Generated {len(cases)} PPC Mach-O floating-point hardware cases")


def decode(directory, capture, binary=False):
    cases = json.loads((directory / "cases.json").read_text(encoding="utf-8"))
    output = capture.read_bytes() if binary else bytes.fromhex(capture.read_text(encoding="utf-8-sig"))
    if len(output) != len(cases) * 16:
        raise ValueError(f"Expected {len(cases) * 16} bytes; got {len(output)}")
    destination = directory / "hardware.txt"
    with destination.open("w", encoding="utf-8", newline="\n") as stream:
        for index, case in enumerate(cases):
            result, status = struct.unpack_from(">QQ", output, index * 16)
            a, b, c = case["inputs"]
            stream.write(
                f"{case['opcode']:08x} {a:016x} {b:016x} {c:016x} "
                f"{case['fpscr']:08x} {result:016x} {status & 0xffffffff:08x}\n"
            )
    print(f"Decoded {len(cases)} hardware values/FPSCR results to {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--decode-hex", type=Path)
    parser.add_argument("--decode-binary", type=Path)
    parser.add_argument("--classic", action="store_true")
    args = parser.parse_args()
    if args.decode_hex and args.decode_binary:
        parser.error("Choose one capture format")
    if args.decode_binary:
        decode(args.directory, args.decode_binary, binary=True)
    elif args.decode_hex:
        decode(args.directory, args.decode_hex)
    else:
        generate(args.directory, classic=args.classic)
