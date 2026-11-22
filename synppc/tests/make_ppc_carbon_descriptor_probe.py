#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_descriptor_probe.py
#
#   Summary:   Original PPC descriptor data, replacement and initialized PEF fallback checks.
#
#   Classes:   None declared.
#
#   Functions: make_descriptor_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC descriptor data, replacement and initialized PEF fallback checks."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_descriptor_probe(destination):
    write_carbon_fallback(destination)
    names = [
        "AECreateDesc",
        "AEDisposeDesc",
        "AEGetDescData",
        "AEGetDescDataSize",
        "AEReplaceDescData",
        "CarbonFallbackToken",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + names.index(name) * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        value &= 0xFFFFFFFF
        emit(0x3C800000 | (value >> 16), 0x60840000 | (value & 65535), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def read_word(offset, expected):
        emit(0x807E0000 | offset)
        check(expected)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x3C605445, 0x60635854, 0x389E00C0, 0x38A00005, 0x38DE0080)
    call("AECreateDesc")
    check(0)
    emit(0x387E0080)
    call("AEGetDescDataSize")
    check(5)
    # Only the requested prefix is written; both surrounding bytes survive.
    emit(0x387E0080, 0x389E00A1, 0x38A00003)
    call("AEGetDescData")
    check(0)
    read_word(160, 0x5A8000FF)
    read_word(164, 0x5A5A5A5A)
    # A larger output cap copies the complete descriptor, not the cap.
    emit(0x387E0080, 0x389E00A1, 0x38A0000C)
    call("AEGetDescData")
    check(0)
    read_word(164, 0x34125A5A)
    emit(0x387E0080, 0x38800000, 0x38A00000)
    call("AEGetDescData")
    check(0)
    emit(0x387E0080, 0x389E00A0, 0x38A0FFFF)
    call("AEGetDescData")
    check(-50)
    # Replacement uses the real descriptor manager and changes type and size.
    emit(0x3C605445, 0x60635354, 0x389E00C8, 0x38A00004, 0x38DE0080)
    call("AEReplaceDescData")
    check(0)
    read_word(128, 0x54455354)
    emit(0x387E0080)
    call("AEGetDescDataSize")
    check(4)
    emit(0x387E0080, 0x389E00A0, 0x38A00004)
    call("AEGetDescData")
    check(0)
    read_word(160, 0x12345678)
    emit(0x387E0080)
    call("AEDisposeDesc")
    check(0)
    # Both a typeNull/no-handle descriptor and a typed empty handle are empty.
    emit(0x387E0088)
    call("AEGetDescDataSize")
    check(0)
    emit(0x387E0088, 0x389E00A0, 0x38A00004)
    call("AEGetDescData")
    check(0)
    read_word(160, 0x12345678)
    emit(0x3C605445, 0x60635854, 0x38800000, 0x38A00000, 0x38DE0080)
    call("AECreateDesc")
    check(0)
    emit(0x387E0080)
    call("AEGetDescDataSize")
    check(0)
    emit(0x387E0080, 0x38800000, 0x38A00001)
    call("AEGetDescData")
    check(0)
    emit(0x387E0080)
    call("AEDisposeDesc")
    check(0)
    call("CarbonFallbackToken")
    check(73)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(256)
    data[136:144] = b"null\0\0\0\0"
    data[160:176] = b"\x5a" * 16
    data[192:197] = b"\x80\0\xff\x34\x12"
    struct.pack_into(">I", data, 200, 0x12345678)
    imports = [("CarbonLib", name, 2) for name in names]
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Carbon Descriptor Probe", app, 1)


if __name__ == "__main__":
    make_descriptor_probe(Path(sys.argv[1]))
