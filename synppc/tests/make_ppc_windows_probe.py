#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_windows_probe.py
#
#   Summary:   Original WindowsLib window lifecycle and opaque-reference checks.
#
#   Classes:   None declared.
#
#   Functions: make_windows_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original WindowsLib window lifecycle and opaque-reference checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_windows_probe(destination):
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(4)  # Macintosh applications initialize the Window Manager first.
    # This probe closes the result, so request an owning reference rather than a borrowed find.
    emit(
        0x387E0090,
        0x3C807077,
        0x60847063,
        0x38A00001,
        0x38DE00B0,
        0x38FE00B4,
        0x39000000,
    )
    call(5)
    check(0)
    emit(0x807E00B0, 0x389E00A0, 0x38BE00B8, 0x38DE00BC)
    call(6)
    check(0)
    emit(0x887E00BC)
    check(2)
    emit(0x807E00B0, 0x389E00C0, 0x38BE00B8, 0x38DE00BC)
    call(6)
    check(-2802)
    emit(0x807E00B8)
    check(0)
    emit(0x387E00B0)
    call(7)
    check(0)
    emit(
        0x38600000,
        0x389E0060,
        0x38BE0070,
        0x38C00000,
        0x38E00000,
        0x3900FFFF,
        0x39200000,
        0x39401234,
    )
    call(0)
    emit(0x907E0080, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    call(11)  # SetPort to the new window.
    # GetPort writes one big-endian guest pointer, preserving adjacent fields.
    emit(0x38601357, 0x907E00CC, 0x907E00D0, 0x907E00D4, 0x387E00D0)
    call(12)
    emit(0x807E00D0, 0x809E0080, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    for offset in (0xCC, 0xD4):
        emit(0x807E0000 | offset)
        check(0x1357)
    # NQD takes a packed register Point but writes a big-endian Point record.
    emit(0x3C60FFFD, 0x60630005)
    call(14)
    emit(0x387E00D0)
    call(13)
    emit(0xA87E00D0)
    check(-3)
    emit(0xA87E00D2)
    check(5)
    emit(0x38600000)
    call(13)
    call(15)
    check(-50)
    emit(0x38600000)
    call(14)
    call(15)
    check(0)
    emit(0x807E0080)
    call(1)
    check(1)
    emit(0x807E0080, 0x389E0084)
    call(2)
    check(0)
    emit(0x807E0084)
    check(1)
    # A failed owner-count query must leave its output unchanged.
    emit(0x38600000, 0x389E0084)
    call(2)
    check(-50)
    emit(0x807E0084)
    check(1)
    emit(0x807E0080, 0x38800000)
    call(2)
    check(-50)
    emit(0x807E0080, 0x3880FFFF)
    call(2)
    check(-50)
    emit(0x3C601234, 0x60635678)
    call(1)
    check(0)
    emit(0x38600000)
    call(1)
    check(0)
    emit(0x807E0080)
    call(3)
    emit(0x807E0080)
    call(1)
    check(0)
    emit(
        0x38600000,
        0x389E0060,
        0x38BE0070,
        0x38C00000,
        0x38E00000,
        0x3900FFFF,
        0x39200000,
        0x39401234,
    )
    call(8)
    emit(0x907E0080, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    call(1)
    check(1)
    emit(0x807E0080)
    call(9)
    emit(0x807E0080)
    call(1)
    check(0)
    emit(0x807E0080)
    call(10)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(256)
    struct.pack_into(">4h", data, 96, 20, 30, 120, 200)
    data[112:118] = b"\x05Probe"
    data[144:155] = b"\x0aWindowsLib"
    data[160:171] = b"\x0aNewCWindow"
    data[192:204] = b"\x0bCloneWindow"
    app = pef(
        code,
        data,
        imports=[
            ("WindowsLib", name, 2)
            for name in [
                "NewCWindow",
                "IsValidWindowPtr",
                "GetWindowOwnerCount",
                "DisposeWindow",
            ]
        ]
        + [
            ("InterfaceLib", name, 2)
            for name in [
                "InitWindows",
                "GetSharedLibrary",
                "FindSymbol",
                "CloseConnection",
            ]
        ]
        + [
            ("WindowsLib", "NewWindow", 2),
            ("WindowsLib", "CloseWindow", 2),
            ("InterfaceLib", "DisposePtr", 2),
            ("InterfaceLib", "SetPort", 2),
            ("InterfaceLib", "GetPort", 2),
            ("NQD", "QDGetPatternOrigin", 2),
            ("NQD", "QDSetPatternOrigin", 2),
            ("InterfaceLib", "QDError", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A0F],
    )
    write_fragment(destination, "PPC Windows Probe", app, 1)


if __name__ == "__main__":
    make_windows_probe(Path(sys.argv[1]))
