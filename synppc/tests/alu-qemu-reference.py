# +===================================================================
#   File:      alu-qemu-reference.py
#
#   Summary:   Run original PPC integer words in a standalone Linux ELF on QEMU's 750.
#
#   Classes:   None declared.
#
#   Functions: build_reference
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Run original PPC integer words in a standalone Linux ELF on QEMU's 750.

No assembler, compiler, guest libraries or Apple binaries are required. The ELF
only records GPR/XER/CR values and writes them with Linux's write/exit syscalls.
Undefined integer division results are excluded from this cross-engine oracle.
"""

import argparse
from pathlib import Path
import struct
import subprocess


def build_reference(directory):
    directory.mkdir(parents=True, exist_ok=True)
    operations = [
        266,
        10,
        138,
        234,
        202,
        40,
        8,
        136,
        232,
        200,
        104,
        235,
        491,
        459,
        24,
        536,
        792,
        824,
        11,
        75,
        7,  # MULLI uses a primary opcode rather than an extended opcode.
        26,
        954,
        922,
        19,
    ]
    values = [
        0,
        1,
        2,
        0xFFFFFFFF,
        0xFFFFFFFE,
        0x80000000,
        0x80000001,
        0x7FFFFFFF,
        0x12345678,
        0x87654321,
    ]
    unary = {234, 202, 232, 200, 104, 26, 954, 922, 19}
    cases = []
    code = []

    def immediate(reg, value):
        code.extend(
            [
                (15 << 26) | (reg << 21) | (value >> 16),
                (24 << 26) | (reg << 21) | (reg << 16) | (value & 65535),
            ]
        )

    immediate(6, 0)  # Patched to the output buffer after the code size is known.
    for oi, op in enumerate(operations):
        for ai, a in enumerate(values):
            for bi, b in enumerate(values + ([0x8000, 0x7FFF] if op == 7 else [])):
                for oe in range(2):
                    for rc in range(2):
                        if op in (24, 536, 792, 824, 11, 75, 26, 954, 922, 19) and oe:
                            continue
                        if op == 7 and (oe or rc) or op == 19 and rc:
                            continue
                        flags = (ai + bi + oi + oe * 2 + rc * 4) % 8
                        alias = (ai * 3 + bi + oi + oe + rc) % (6 if op == 7 else 5)
                        dst = 3 if alias in (0, 3) else 1 if alias in (1, 4) else 2
                        other = 1 if alias >= 3 else 2
                        divisor = a if other == 1 else b
                        if op in (491, 459) and (
                            not divisor or (op == 491 and a == 0x80000000 and divisor == 0xFFFFFFFF)
                        ):
                            continue
                        cases.append((op, a, b, flags, alias, oe, rc))
                        immediate(1, a)
                        immediate(2, b)
                        immediate(3, 0x91003333)
                        if op == 7 and alias == 5:
                            immediate(0, a)
                        immediate(5, 0x13579BDF)
                        code.append((31 << 26) | (5 << 21) | (255 << 12) | (144 << 1))
                        immediate(5, (flags << 29) | 0x42)
                        code.append((31 << 26) | (5 << 21) | (1 << 16) | (467 << 1))
                        code.append(
                            (31 << 26)
                            | (dst << 21)
                            | (1 << 16)
                            | ((0 if op in unary else other) << 11)
                            | (oe << 10)
                            | (op << 1)
                            | rc
                        )
                        if op in (24, 536, 792, 824):
                            code[-1] = (
                                (31 << 26)
                                | (1 << 21)
                                | (dst << 16)
                                | (((b & 31) if op == 824 else other) << 11)
                                | (op << 1)
                                | rc
                            )
                        if op == 7:
                            code[-1] = (7 << 26) | (dst << 21) | ((0 if alias == 5 else 1) << 16) | (b & 65535)
                        if op in (26, 954, 922):
                            code[-1] = (31 << 26) | (1 << 21) | (dst << 16) | (op << 1) | rc
                        if op == 19:
                            code[-1] = (31 << 26) | (dst << 21) | (19 << 1)
                        code.append((36 << 26) | (dst << 21) | (6 << 16))
                        code.append((31 << 26) | (4 << 21) | (1 << 16) | (339 << 1))
                        code.append((36 << 26) | (4 << 21) | (6 << 16) | 4)
                        code.append((31 << 26) | (4 << 21) | (19 << 1))
                        code.append((36 << 26) | (4 << 21) | (6 << 16) | 8)
                        code.append((14 << 26) | (6 << 21) | (6 << 16) | 12)
    # write(1, buffer, size); exit(0). r1 need not be a stack for these syscalls.
    immediate(0, 4)
    immediate(3, 1)
    buffer_fixup = len(code)
    immediate(4, 0)
    immediate(5, len(cases) * 12)
    code.append(0x44000002)
    immediate(0, 1)
    immediate(3, 0)
    code.append(0x44000002)
    address = 0x10000
    offset = 0x100
    file_size = offset + len(code) * 4
    buffer = address + file_size
    for index, reg in ((0, 6), (buffer_fixup, 4)):
        code[index] = (15 << 26) | (reg << 21) | (buffer >> 16)
        code[index + 1] = (24 << 26) | (reg << 21) | (reg << 16) | (buffer & 65535)
    header = b"\x7fELF\x01\x02\x01" + bytes(9)
    header += struct.pack(">HHIIIIIHHHHHH", 2, 20, 1, address + offset, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    header += struct.pack(
        ">IIIIIIII",
        1,
        0,
        address,
        address,
        file_size,
        file_size + len(cases) * 12,
        7,
        4096,
    )
    executable = directory / "alu-750.elf"
    executable.write_bytes(header.ljust(offset, b"\0") + struct.pack(f">{len(code)}I", *code))
    output = subprocess.check_output(["qemu-ppc", "-cpu", "750", str(executable)])
    if len(output) != len(cases) * 12:
        raise ValueError(f"QEMU wrote {len(output)} bytes for {len(cases)} cases")
    destination = directory / "alu-750.txt"
    with destination.open("w", encoding="utf-8", newline="\n") as stream:
        for case, result in zip(cases, struct.iter_unpack(">III", output), strict=True):
            op, a, b, flags, alias, oe, rc = case
            value, xer, cr = result
            stream.write(f"{op} {a:08x} {b:08x} {flags} {alias} {oe} {rc} " f"{value:08x} {xer:08x} {cr:08x}\n")
    print(f"Generated {len(cases)} independent QEMU 750 integer cases: {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    build_reference(parser.parse_args().directory)
