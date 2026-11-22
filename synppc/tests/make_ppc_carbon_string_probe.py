#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_string_probe.py
#
#   Summary:   Original PPC C/Pascal conversion, alias ABI and PEF fallback checks.
#
#   Classes:   None declared.
#
#   Functions: make_string_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC C/Pascal conversion, alias ABI and PEF fallback checks."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_string_probe(destination):
    write_carbon_fallback(destination)
    names = ["CopyPascalStringToC", "CopyCStringToPascal", "c2pstrcpy", "p2cstrcpy", "CarbonFallbackToken"]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check_word(offset, value):
        emit(0x807E0000 + offset, 0x3C800000 | (value >> 16), 0x60840000 | (value & 65535), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x38601357, 0x907E00FC, 0x907E0200)
    for name in ("CopyCStringToPascal", "c2pstrcpy"):
        emit(*((0x387E0300, 0x389E0100) if name.startswith("Copy") else (0x387E0100, 0x389E0300)))
        call(name)
        check_word(256, 0x03418042)
        check_word(260, 0xA5A5A5A5)
    for name in ("CopyPascalStringToC", "p2cstrcpy"):
        emit(*((0x387E0310, 0x389E0100) if name.startswith("Copy") else (0x387E0100, 0x389E0310)))
        call(name)
        check_word(256, 0x41008000)  # Copy the embedded NUL and following high byte.
    emit(0x387E0480, 0x389E0480)
    call("c2pstrcpy")
    check_word(1152, 0x04575859)
    check_word(1156, 0x5A000000)
    emit(0x387E0480, 0x389E0480)
    call("p2cstrcpy")
    check_word(1152, 0x5758595A)
    check_word(1156, 0)
    emit(0x387E0320, 0x389E0100)
    call("CopyCStringToPascal")
    check_word(256, 0xFF414141)
    check_word(508, 0x41414141)
    for offset in (252, 512):
        check_word(offset, 0x1357)
    for name in ("CopyCStringToPascal", "CopyPascalStringToC"):
        emit(0x38600000, 0x389E0100)
        call(name)
        emit(0x887E0100, 0x2C030000)
        failures.append(len(code))
        emit(0x40820000)
    call("CarbonFallbackToken")
    emit(0x2C030049)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(1440)
    data[256:512] = bytes([0xA5]) * 256
    data[768:772] = b"A\x80B\0"
    data[784:788] = b"\x03A\0\x80"
    data[800:1101] = b"A" * 300 + b"\0"
    data[1152:1157] = b"WXYZ\0"
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon String Probe", app, 1)


if __name__ == "__main__":
    make_string_probe(Path(sys.argv[1]))
