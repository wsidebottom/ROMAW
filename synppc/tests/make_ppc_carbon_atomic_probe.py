#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_atomic_probe.py
#
#   Summary:   Original PPC atomic ABI, byte order, unaligned and previous-value checks.
#
#   Classes:   None declared.
#
#   Functions: make_atomic_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC atomic ABI, byte order, unaligned and previous-value checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_atomic_probe(destination):
    write_carbon_fallback(destination)
    family = []
    for suffix, width in [("8", 1), ("16", 2), ("", 4)]:
        mask = (1 << (8 * width)) - 1
        sign = 1 << (8 * width - 1)
        for op in ("And", "Or", "Xor"):
            initial = mask if op == "And" else sign | 3
            operand = 0x12345678
            following = {"And": initial & operand, "Or": initial | operand, "Xor": initial ^ operand}[op] & mask
            family.append(("Bit" + op + "Atomic" + suffix, width, initial, operand, initial, following))
        family.append(("AddAtomic" + suffix, width, mask, 0x10003, 0xFFFFFFFF, (mask + 0x10003) & mask))
        if suffix:
            family.append(("IncrementAtomic" + suffix, width, sign - 1, None, sign - 1, sign))
            family.append(
                ("DecrementAtomic" + suffix, width, sign, None, (sign - (1 << (width * 8))) & 0xFFFFFFFF, sign - 1)
            )
    names = ["IncrementAtomic", "DecrementAtomic", "TestAndSet", "TestAndClear", "CarbonFallbackToken"]
    names += [name for name, *_ in family] + ["CompareAndSwap"]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x3C800000 | (value >> 16), 0x60840000 | (value & 65535), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def constant(value):
        value &= 0xFFFFFFFF
        emit(0x3C600000 | (value >> 16), 0x60630000 | (value & 65535))

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    for name, offset, previous, following in [
        ("IncrementAtomic", 256, 0x7FFFFFFF, 0x80000000),
        ("DecrementAtomic", 256, 0x80000000, 0x7FFFFFFF),
        ("IncrementAtomic", 265, 0xFFFFFFFF, 0),
        ("DecrementAtomic", 265, 0, 0xFFFFFFFF),
    ]:
        emit(0x387E0000 + offset)
        call(name)
        check(previous)
        emit(0x807E0000 + offset)
        check(following)
    for name, bit, previous, following in [
        ("TestAndSet", 0, 0, 0x81),
        ("TestAndSet", 0, 1, 0x81),
        ("TestAndClear", 7, 1, 0x80),
        ("TestAndClear", 15, 0, 0x80),
        ("TestAndSet", 8, 1, 0x80),
        ("TestAndClear", 0, 1, 0),
    ]:
        emit(0x38600000 | bit, 0x389E0110)
        call(name)
        check(previous)
        emit(0x887E0110)
        check(following)
    for offset in (264, 269, 271, 273):
        emit(0x887E0000 + offset)
        check(0xA5)
    for name, width, initial, operand, previous, following in family:
        offset = 274 if width == 1 else 276 if width == 2 else 256
        constant(initial)
        emit({1: 0x987E0000, 2: 0xB07E0000, 4: 0x907E0000}[width] + offset)
        if operand is None:
            emit(0x387E0000 + offset)
        else:
            constant(operand)
            emit(0x389E0000 + offset)
        call(name)
        check(previous)
        emit({1: 0x887E0000, 2: 0xA07E0000, 4: 0x807E0000}[width] + offset)
        check(following)
    constant(0x81234567)
    emit(0x907E0100)
    for old, replacement, matched, following in [
        (0, 0xFEDCBA98, 0, 0x81234567),
        (0x81234567, 0xFEDCBA98, 1, 0xFEDCBA98),
        (0xFEDCBA98, 0, 1, 0),
    ]:
        constant(old)
        emit(0x3C800000 | (replacement >> 16), 0x60840000 | (replacement & 65535), 0x38BE0100)
        call("CompareAndSwap")
        check(matched)
        emit(0x807E0100)
        check(following)
    for offset in (275, 278):
        emit(0x887E0000 + offset)
        check(0xA5)
    call("CarbonFallbackToken")
    check(73)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(320)
    struct.pack_into(">I", data, 256, 0x7FFFFFFF)
    data[264:274] = bytes([0xA5]) * 10
    struct.pack_into(">I", data, 265, 0xFFFFFFFF)
    data[272] = 1
    data[275] = data[278] = 0xA5
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Atomic Probe", app, 1)


if __name__ == "__main__":
    make_atomic_probe(Path(sys.argv[1]))
