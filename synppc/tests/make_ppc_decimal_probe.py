#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_decimal_probe.py
#
#   Summary:   Generate original PPC checks for MathLib decimal formatting and double argument slots.
#
#   Classes:   None.
#
#   Functions: make_decimal_probe
#
#   Origin:    Original Executor regression fixture; no Apple executable code.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+
"""Check num2dec/dec2str through real PPC imports and packed guest records."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_decimal_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def word(offset, value):
        emit(0x807E0000 | offset, 0x3C800000 | (value >> 16), 0x60840000 | (value & 65535), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    # The double in f1 also occupies the r4/r5 slots; the output pointer is r6.
    emit(0x387E0040, 0xC83E0050, 0x3880FFFF, 0x38A0FFFF, 0x38DE0070)
    call(0)
    emit(0xA87E0072)
    check(-4)
    emit(0x887E0074)
    check(7)
    word(117, 0x35373830)
    word(121, 0x30303000)
    emit(0x387E0040, 0x389E0070, 0x38BE00B0)
    call(1)
    word(176, 0x3537382E)
    word(180, 0x30303030)
    emit(0x887E00B8)
    check(0)
    emit(0x38600006, 0x907E0040)  # float style 0, unused 0, six significant digits
    emit(0x387E0040, 0xC83E0058, 0x38DE0070)
    call(0)
    emit(0xA87E0072)
    check(-3)
    emit(0x887E0074)
    check(6)
    word(117, 0x31323334)
    emit(0x387E0040, 0x389E0070, 0x38BE00B0)
    call(1)
    word(176, 0x20312E32)
    word(180, 0x33343530)
    word(184, 0x652B3200)
    emit(0x387E0110, 0x389E0108, 0x38BE0070, 0x38DE010A)
    call(2)
    emit(0xA87E0108)
    check(6)
    emit(0xA87E010A)
    check(0)
    emit(0x387E0070)
    call(3)
    emit(0xD83E0060)
    word(96, 0xC0290000)
    word(100, 0)
    word(156, 0xDEADBEEF)
    word(256, 0xDEADBEEF)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(320)
    data[272:280] = b" -12.5x\0"
    struct.pack_into(">BBh", data, 64, 1, 0, 4)
    struct.pack_into(">dd", data, 80, 578.0, 123.45)
    for offset in (156, 256):
        struct.pack_into(">I", data, offset, 0xDEADBEEF)
    fragment = pef(
        code,
        data,
        imports=[("MathLib", name, 2) for name in ["num2dec", "dec2str", "str2dec", "dec2num"]],
        main=0,
        reloc=[0x4600, 0x4A03],
    )
    write_fragment(destination, "PPC Decimal Probe", fragment, 1)


if __name__ == "__main__":
    make_decimal_probe(Path(sys.argv[1]))
