#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_init_probe.py
#
#   Summary:   Original 68K extension INIT must run before its PPC library initializer.
#
#   Classes:   None declared.
#
#   Functions: make_init_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original 68K extension INIT must run before its PPC library initializer."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_init_probe(
    destination, closure_count=4, import_library_count=3, import_symbol_count=5, app_name="PPC INIT Probe"
):
    # Execute the real 68K static-value trap ABI, including failure results.
    init = bytearray()
    errors = []
    for action, value, error in [
        (0x401, 21, 0),
        (0x402, 42, 0),
        (0x401, 99, -5552),
        (0x203, 0, 0),
        (0x402, 7, -5551),
        (0x203, 0, -5551),
        (0x401, 42, 0),
    ]:
        init += bytes.fromhex("558f2f3c") + b"PINI"
        if action != 0x203:
            init += bytes.fromhex("2f3c") + struct.pack(">I", value)
        init += bytes.fromhex("303c") + struct.pack(">H", action) + bytes.fromhex("abf1")
        init += bytes.fromhex("0c5f") + struct.pack(">h", error)  # cmpi.w #error,(sp)+
        errors.append(len(init))
        init += bytes.fromhex("66000000")
    init += bytes.fromhex("4e75")
    bad_init = len(init)
    init += bytes.fromhex("558f2f3c") + b"PERR"
    init += bytes.fromhex("2f3c00000001303c0401abf1548f4e75")
    for branch in errors:
        struct.pack_into(">h", init, branch + 2, bad_init - branch - 2)
    code = [0x3860002A, 0x4E800020]
    failures = []

    def emit(*words):
        code.extend(words)

    def check(value, equal=False):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x41820000 if equal else 0x40820000)

    def call(offset):
        emit(0x819E0000 + offset, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    emit(
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x80630004,
        0x907E0018,
    )  # save closure ID from CFragInitBlock
    emit(0x3C605049, 0x60634E49, 0x389E001C)
    call(16)
    check(0)
    emit(0x807E001C)
    check(42)
    emit(0x3C605045, 0x60635252, 0x389E001C)
    call(16)
    check(-5551)
    # Query the real application closure's imported-library metadata.
    emit(0x387E0070, 0x907E0038, 0x38600000, 0x809E0018, 0x38A00000, 0x38DE0020)
    call(20)
    check(0)
    emit(
        0x819E005C,
        0x818C0030,
        0x807E0060,
        0x389E001C,
        0x38BE0064,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
    )
    check(0)
    emit(0x807E001C)
    check(import_library_count)
    emit(0x807E0064)
    check(import_symbol_count)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(192)
    struct.pack_into(">4I", data, 0, 0, 0, 8, 0)
    library = pef(
        code,
        data,
        imports=[
            ("InterfaceLib", "Gestalt", 2),
            ("PrivateInterfaceLib", "FragGetConnectionInfo", 2),
        ],
        exports=[("GetValue", 2, 0, 1)],
        init=8,
        reloc=[0x4601, 0x4A01],
    )
    write_fragment(destination, "ProbeStartup", library, 0, [(b"INIT", 0, init)])
    code = []
    failures = []
    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(8)
    check(42)
    emit(0x387E0040)
    call(12)
    check(0)
    emit(
        0x3860003C,
        0x907E0050,
        0x387E00A0,
        0x907E0054,
        0x387E01A0,
        0x907E0088,
        0x387E0040,
        0x389E0050,
    )
    call(16)
    check(0)
    emit(0x807E0060, 0x3C804150, 0x6084504C, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x887E00A0)
    check(len(app_name))
    emit(0x887E01A6)
    check(len(app_name))
    emit(0x887E01A7)
    check(ord(app_name[0]))
    # Enumerate the real application closure, with bounded partial output.
    emit(0x38600000, 0x38800000, 0x38BE01D0)
    call(24)
    check(0)
    emit(
        0x38600000,
        0x809E01DC,
        0x38A00000,
        0x38DE01A0,
        0x38E00000,
        0x39000000,
        0x39200000,
    )
    call(20)
    check(0)
    emit(0x807E01A8)
    check(closure_count)
    emit(
        0x38600000,
        0x809E01DC,
        0x38A00000,
        0x38DE01A0,
        0x38E00001,
        0x39000002,
        0x393E01B0,
    )
    call(20)
    check(0)
    emit(0x807E01B0)
    check(0, equal=True)
    emit(0x807E01B4)
    check(0, equal=True)

    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    app = pef(
        code,
        bytes(512),
        imports=[
            ("ProbeStartup", "GetValue", 2),
            ("InterfaceLib", "GetCurrentProcess", 2),
            ("InterfaceLib", "GetProcessInformation", 2),
            ("PrivateInterfaceLib", "FragGetClosureInfo", 2),
            ("PrivateInterfaceLib", "FragGetContextInfo", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A04],
    )
    write_fragment(destination, "PPC INIT Probe", app, 1)


if __name__ == "__main__":
    make_init_probe(Path(sys.argv[1]), *(int(arg) for arg in sys.argv[2:5]), *sys.argv[5:])
