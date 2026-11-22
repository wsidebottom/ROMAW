#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_dialog_probe.py
#
#   Summary:   Original dialog/control accessor ABI and initialized fallback checks.
#
#   Classes:   None declared.
#
#   Functions: make_dialog_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original dialog/control accessor ABI and initialized fallback checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_dialog_probe(destination):
    write_carbon_fallback(destination)
    dialog_id = 1129
    names = [
        "InitWindows",
        "GetNewDialog",
        "DisposeDialog",
        "GetDialogWindow",
        "GetDialogFromWindow",
        "GetDialogTextEditHandle",
        "GetDialogDefaultItem",
        "SetPortDialogPort",
        "GetControlBounds",
        "SetDialogDefaultItem",
        "GetDialogItemAsControl",
        "GetQDGlobalsThePort",
        "SetPort",
        "CarbonFallbackToken",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def same_dialog():
        emit(0x809E0100, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    call("GetQDGlobalsThePort")
    emit(0x907E0108, 0x38600000 | dialog_id, 0x38800000, 0x38A0FFFF)
    call("GetNewDialog")
    check(0, True)
    emit(0x907E0100)
    call("GetDialogWindow")
    same_dialog()
    call("GetDialogFromWindow")
    same_dialog()
    call("GetDialogTextEditHandle")
    emit(0x809E0100, 0x808400A0, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0100, 0x38800001)
    call("SetDialogDefaultItem")
    check(0)
    emit(0x807E0100)
    call("GetDialogDefaultItem")
    check(1)
    emit(0x807E0100)
    call("SetPortDialogPort")
    call("GetQDGlobalsThePort")
    same_dialog()
    emit(0x807E0100, 0x38800001, 0x38BE0104)
    call("GetDialogItemAsControl")
    check(0)
    emit(0x38601357, 0x907E010C, 0x907E0118, 0x807E0104, 0x389E0110)
    call("GetControlBounds")
    emit(0x389E0110, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    for offset, expected in [(272, 8), (274, 8), (276, 28), (278, 68)]:
        emit(0xA87E0000 + offset)
        check(expected)
    for offset in (268, 280):
        emit(0x807E0000 + offset)
        check(0x1357)
    emit(0x807E0100)
    call("DisposeDialog")
    emit(0x807E0108)
    call("SetPort")
    for name in ("GetDialogWindow", "GetDialogFromWindow", "GetDialogTextEditHandle"):
        emit(0x38600000)
        call(name)
        check(0)
    call("CarbonFallbackToken")
    check(73)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, bytes(320), imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    dialog = struct.pack(">4hhBBBBIh", 60, 60, 140, 220, 0, 0, 0, 0, 0, 0, dialog_id) + b"\0\0"
    items = struct.pack(">hI4hBB", 0, 0, 8, 8, 28, 68, 4, 2) + b"OK"
    write_fragment(
        destination, "PPC Carbon Dialog Probe", app, 1, [(b"DLOG", dialog_id, dialog), (b"DITL", dialog_id, items)]
    )


if __name__ == "__main__":
    make_dialog_probe(Path(sys.argv[1]))
