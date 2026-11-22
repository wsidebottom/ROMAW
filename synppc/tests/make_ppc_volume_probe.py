#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_volume_probe.py
#
#   Summary:   Compare extended guest volume capacity with the real Windows host API.
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

"""Compare extended guest volume capacity with the real Windows host API."""
import ctypes
import os
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    total = ctypes.c_ulonglong()
    apps = Path(os.environ["USERPROFILE"]) / ".romaw/Apps"
    if not ctypes.windll.kernel32.GetDiskFreeSpaceExW(str(apps), None, ctypes.byref(total), None):
        raise ctypes.WinError()
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index=0):
        emit(0x819E0008 + 4 * index, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    def equal(left, right):
        emit(0x807E0000 | left, 0x809E0000 | right, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def query(expected):
        emit(0x387E0080)
        call()
        check(expected)
        emit(0xA87E0090)
        check(expected)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    query(0)
    equal(250, 64)  # Both halves must match, including capacities above 4 GB.
    equal(254, 68)
    # Compare the complete unsigned free-byte count against total capacity.
    emit(0x807E0102, 0x809E00FA, 0x7C032040)
    failures.append(len(code))
    emit(0x41810000)
    emit(0x40820014, 0x807E0106, 0x809E00FE, 0x7C032040)
    failures.append(len(code))
    emit(0x41810000)
    # Invalid versions/volumes must return an error without replacing capacity.
    emit(0x38600001, 0x907E0098)
    query(-50)
    equal(250, 64)
    equal(254, 68)
    emit(0x38600000, 0x907E0098, 0x38607FFF, 0xB07E009C)
    query(-35)
    equal(250, 64)
    equal(254, 68)
    emit(0x38600000, 0xB07E009C, 0x3860FFF0, 0x907E0092)
    query(-50)
    for pointer in (0, -8):
        emit(0x38600000 | (pointer & 0xFFFF))
        call()
        check(-50)
    emit(0xA07E007E)
    check(0x5A5A)
    emit(0xA07E010A)
    check(0x5A5A)
    # Iconographer calls the synchronous volume query even for a volume
    # without a usable Desktop database. Verify the error reaches the guest.
    emit(0x38600000, 0x907E0092, 0x38607FFF, 0xB07E0096, 0x387E0080)
    call(1)
    check(-35)
    emit(0xA87E0090)
    check(-35)
    # A missing icon suite must be safe to dispose with its data.
    emit(0x38600000, 0x38800001)
    call(2)
    check(-50)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(288)
    data[64:72] = struct.pack(">Q", total.value)
    data[126:128] = data[266:268] = b"ZZ"
    data[156:158] = struct.pack(">h", 1)
    app = pef(
        code,
        data,
        imports=[
            ("InterfaceLib", "PBXGetVolInfoSync", 2),
            ("InterfaceLib", "PBHGetVolParmsSync", 2),
            ("InterfaceLib", "DisposeIconSuite", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A02],
    )
    write_fragment(destination, "PPC Volume Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
