#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_tsm_probe.py
#   Summary:   Generate original PPC checks for Roman Text Services document state.
#   Classes:   None declared.
#   Functions: make_tsm_probe
#   Origin:    Original regression fixture using the repository PEF generator.
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

"""Check TSM registration, no-input-method documents and document lifetimes."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_tsm_probe(destination):
    names = [
        "InitTSMAwareApplication",
        "CloseTSMAwareApplication",
        "NewTSMDocument",
        "DeleteTSMDocument",
        "ActivateTSMDocument",
        "DeactivateTSMDocument",
        "FixTSMDocument",
        "TSMGetActiveDocument",
        "UseInputWindow",
        "IsTSMDocumentUsingInputWindow",
        "TSMEvent",
        "CallUniversalProc",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + 4 * names.index(name), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def new(out=72, types=64, count=1):
        emit(
            0x38600000 | count,
            0x389E0000 | types if types else 0x38800000,
            0x38BE0000 | out if out else 0x38A00000,
            0x3CC01234,
            0x60C65678,
        )
        call("NewTSMDocument")

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x38601234, 0x907E0048, 0x907E0050)
    new()
    check(-2504)
    emit(0x807E0048)
    check(0)
    emit(0x807E0050)
    check(0x1234)
    call("InitTSMAwareApplication")
    check(0)
    call("InitTSMAwareApplication")
    check(-2503)
    for out, types, count, error in ((72, 68, 1, -2518), (72, 64, 0, -50), (72, 0, 1, -50), (0, 64, 1, -50)):
        new(out, types, count)
        check(error)
    new()
    check(-2517)  # Roman script creates a document without opening an input method.
    emit(0x807E0048, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x80630000, 0x80630000, 0x3C801234, 0x60845678, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    new(out=76)
    check(-2517)
    emit(0x807E0048, 0x809E004C, 0x7C032000)
    failures.append(len(code))
    emit(0x41820000)
    call("CloseTSMAwareApplication")
    check(-2511)  # Open documents prevent unregistering.
    for offset in (72, 76):
        emit(0x807E0000 | offset)
        call("ActivateTSMDocument")
        check(0)
        call("TSMGetActiveDocument")
        emit(0x809E0000 | offset, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    emit(0x807E0048)
    call("FixTSMDocument")
    check(-2507)
    emit(0x807E004C)
    call("FixTSMDocument")
    check(-2514)  # No input-method component has been opened.
    emit(0x807E004C, 0x38800001)
    call("UseInputWindow")
    check(0)
    emit(0x807E004C)
    call("IsTSMDocumentUsingInputWindow")
    check(1)
    emit(0x807E0048)
    call("IsTSMDocumentUsingInputWindow")
    check(0)
    # Mixed Mode TSM queries must see the same live document IDs and policy.
    for offset, expected in ((72, 0), (76, 1)):
        emit(0x80601750, 0x38800398, 0x38A0001D, 0x80DE0000 | offset)
        call("CallUniversalProc")
        check(expected)
    emit(0x38600000, 0x38800001)
    call("UseInputWindow")
    check(0)
    emit(0x807E0048)
    call("IsTSMDocumentUsingInputWindow")
    check(1)
    emit(0x387E0060)
    call("TSMEvent")
    check(0)
    emit(0x887E0060)
    check(0x5A)
    emit(0x807E004C)
    call("DeactivateTSMDocument")
    check(0)
    call("TSMGetActiveDocument")
    check(0)
    emit(0x807E0048)
    call("ActivateTSMDocument")
    check(0)
    for offset in (72, 76):
        emit(0x807E0000 | offset)
        call("DeleteTSMDocument")
        check(0)
    call("TSMGetActiveDocument")
    check(0)
    emit(0x807E0048)
    call("DeleteTSMDocument")
    check(-2505)
    call("CloseTSMAwareApplication")
    check(0)
    call("CloseTSMAwareApplication")
    check(-2504)
    call("InitTSMAwareApplication")
    check(0)
    emit(0x38600000)
    call("IsTSMDocumentUsingInputWindow")
    check(0)  # Re-registration resets the application policy.
    new()
    check(-2517)  # Launch cleanup also disposes documents left open by a client.
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(128)
    data[64:68] = b"tsvc"
    data[68:72] = b"bad!"
    data[96:112] = b"\x5a" * 16
    app = pef(
        code, data, imports=[("InterfaceLib", n, 2) for n in names], main=0, reloc=[0x4600, 0x4A00 | (len(names) - 1)]
    )
    write_fragment(destination, "PPC TSM Probe", app, 1)


if __name__ == "__main__":
    make_tsm_probe(Path(sys.argv[1]))
