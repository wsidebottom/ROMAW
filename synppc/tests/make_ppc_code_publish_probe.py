#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_code_publish_probe.py
#
#   Summary:   Publish and replace a hot guest function through MakeDataExecutable.
#
#   Classes:   None declared.
#
#   Functions: make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Publish and replace a hot guest function through MakeDataExecutable."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def publish(offset, length):
        emit(0x387E0000 | offset, 0x38800000 | length)
        call(0)

    def check(value):
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x93A10034, 0x7C5E1378)
    call(1)  # GetMainDevice supplies a real guest graphics-device handle.
    emit(0x907E0040, 0x38800008, 0x38A00000, 0x38C00000)
    call(2)
    check(1)
    emit(0x807E0040, 0x38800000, 0x38A00000, 0x38C00000)
    call(2)
    check(0)
    emit(0x38600000, 0x38800008, 0x38A00000, 0x38C00000)
    call(2)
    check(0)
    for value in (17, 29):
        # Replace the first instruction of a previously compiled RAM function.
        emit(0x3C603860, 0x60630000 | value, 0x907E0080)
        publish(128, 20)
        emit(0x3BA00020)
        loop = len(code)
        emit(0x399E0080, 0x7D8903A6, 0x4E800421)
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)
        emit(0x37BDFFFF)
        emit(0x40820000 | (((loop - len(code)) * 4) & 0xFFFF))
    # Empty ranges do not dereference their pointer.
    emit(0x3860FFFF, 0x38800000)
    emit(0x819E0008, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    emit(0x83A10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040)
    emit(0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(160)
    data[128:148] = struct.pack(">5I", 0x38600000, 0x38630000, 0x38630000, 0x38630000, 0x4E800020)
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in ("MakeDataExecutable", "GetMainDevice", "HasDepth")],
        main=0,
        reloc=[0x4600, 0x4A02],
    )
    write_fragment(destination, "PPC Code Publish Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
