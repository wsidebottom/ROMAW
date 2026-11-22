#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_wait_probe.py
#
#   Summary:   Original Carbon wrapper routing check, without Apple runtime binaries.
#
#   Classes:   None declared.
#
#   Functions: make_carbon_wait_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original Carbon wrapper routing check, without Apple runtime binaries."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_carbon_wait_probe(destination):
    write_carbon_fallback(destination)
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x38600060)
    call(1)
    emit(0x907E0090, 0x387E0100, 0x38800060)
    call(2)
    emit(0x38600000, 0x389E0080, 0x38A00000, 0x38C00000)
    call(0)
    check(1)
    emit(0xA87E0080)
    check(1)
    emit(0x807E0082, 0x3C801234, 0x60845678, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0090, 0x38800060)
    call(2)
    for trap, target in [(0x170, 3), (0x171, 4)]:
        emit(0x38600000 | trap)
        call(1)
        emit(0x907E0090, 0x387E0120, 0x38800000 | trap)
        call(2)
        emit(0x38600000, 0x907E0082, 0x389E0080)
        call(target)
        check(1)
        emit(0xA87E0080)
        check(1)
        emit(0x807E0082, 0x3C801234, 0x60845678, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        if trap == 0x171:
            # A native wait must poll an installed EventAvail hook; the hook's
            # counter survives the subsequent GetNextEvent record overwrite.
            emit(0x38600000, 0x907E0094, 0x389E0080, 0x38A00000, 0x38C00000)
            call(0)
            check(1)
            emit(0x807E0094)
            check(1)
        emit(0x807E0090, 0x38800000 | trap)
        call(2)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(352)
    # Pascal WaitNextEvent: event at 12(sp), Boolean result at 18(sp).
    # Store a message marker so the wrapper cannot mask a skipped trap patch.
    patch = bytes.fromhex("206f000c217c1234567800021f7c00010012225f4fef000e4ed1")
    data[256 : 256 + len(patch)] = patch
    # GetNextEvent/EventAvail have six argument bytes and a Boolean result.
    patch = bytes.fromhex("206f000452a8001430bc0001217c1234567800021f7c0001000a225f4fef00064ed1")
    data[288 : 288 + len(patch)] = patch
    app = pef(
        code,
        data,
        imports=[
            ("CarbonLib", "WaitNextEvent", 2),
            ("InterfaceLib", "GetToolTrapAddress", 2),
            ("InterfaceLib", "SetToolTrapAddress", 2),
            ("InterfaceLib", "GetNextEvent", 2),
            ("InterfaceLib", "EventAvail", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A04],
    )
    write_fragment(destination, "PPC Carbon Wait Probe", app, 1)


if __name__ == "__main__":
    make_carbon_wait_probe(Path(sys.argv[1]))
