#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_memory_probe.py
#
#   Summary:   Original PPC C allocator and real CFM closure metadata regression fixture.
#
#   Classes:   None declared.
#
#   Functions: make_memory_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC C allocator and real CFM closure metadata regression fixture."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_memory_probe(destination, closure_count=4):
    imports = [("StdCLib", name, 2) for name in ["malloc", "calloc", "realloc", "free"]]
    imports += [("InterfaceLib", name, 2) for name in ["GetZone", "SetZone", "SystemZone", "PtrZone", "GetPtrSize"]]
    imports += [
        ("PrivateInterfaceLib", name, 2)
        for name in [
            "FragGetContextInfo",
            "FragGetClosureInfo",
            "FragGetConnectionInfo",
        ]
    ]
    names = [name for _, name, _ in imports]
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def pointer(offset):
        emit(0x809E0000 + offset, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def magic():
        emit(0x807E0048, 0x80630000, 0x3C801234, 0x60845678, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("GetZone")
    emit(0x907E0040)
    call("SystemZone")
    emit(0x907E0044)
    emit(0x38600020)
    call("malloc")
    check(0, True)
    emit(0x907E0048)
    emit(0x7063000F)
    check(0)
    emit(0x807E0048, 0x3C801234, 0x60845678, 0x90830000)
    emit(0x3880FFFF)
    call("realloc")
    check(0)
    magic()
    emit(0x807E0044)
    call("SetZone")
    emit(0x807E0048, 0x38800040)
    call("realloc")
    check(0, True)
    emit(0x907E0048)
    # C allocation alignment may produce an interior pointer, so validate heap
    # membership by the real zone bounds rather than passing it to PtrZone.
    emit(0x809E0040, 0x7C032040)
    failures.append(len(code))
    emit(0x41800000)
    emit(0x80840000, 0x7C032040)
    failures.append(len(code))
    emit(0x40800000)
    call("GetZone")
    pointer(68)
    magic()
    emit(0x807E0040)
    call("SetZone")
    emit(0x3860FFFF, 0x38800010)
    call("calloc")
    check(0)
    emit(0x38600008, 0x38800004)
    call("calloc")
    check(0, True)
    emit(0x907E004C)
    for offset in range(0, 32, 4):
        emit(0x807E004C, 0x80630000 + offset)
        check(0)
    emit(0x807E004C)
    call("free")
    emit(0x38600000)
    call("free")
    emit(0x38600000, 0x38800000)
    call("realloc")
    check(0)
    emit(0x38600000)
    call("malloc")
    check(0, True)
    call("free")
    # Check the exact root size for either the stand-alone or grouped application.
    emit(0x38600000, 0x38800000, 0x38BE0080)
    call("FragGetContextInfo")
    check(0)
    emit(
        0x38600000,
        0x809E008C,
        0x38A00000,
        0x38DE00A0,
        0x38E00000,
        0x39000000,
        0x39200000,
    )
    call("FragGetClosureInfo")
    check(0)
    emit(0x807E00A8)
    check(closure_count)
    emit(
        0x38600000,
        0x809E008C,
        0x38A00000,
        0x38DE00A0,
        0x38E00000,
        0x3900000A,
        0x393E00C0,
    )
    call("FragGetClosureInfo")
    check(0)
    emit(0x807E00C0)
    pointer(140)
    sentinel = 192 + closure_count * 4
    emit(0x807E0000 | sentinel, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E0154, 0x907E0118, 0x38600000, 0x809E00C4, 0x38A00000, 0x38DE0100)
    call("FragGetConnectionInfo")
    check(0)
    emit(0x807E0100)
    pointer(196)
    emit(0x887E0154)
    check(0, True)
    for context, identifier, count, expected in [
        (1, 0, 0, -2800),
        (0, 1, 0, -2801),
        (0, 0, -1, -50),
    ]:
        emit(
            0x38600000 | context,
            0x38800000 | identifier,
            0x38A00000,
            0x38DE00A0,
            0x38E00000,
            0x39000000 | (count & 0xFFFF),
            0x393E00C0,
        )
        call("FragGetClosureInfo")
        check(expected)
    emit(0x807E0048)
    call("free")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(640)
    struct.pack_into(">I", data, sentinel, 0xDEADBEEF)
    app = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A00 | (len(imports) - 1)])
    write_fragment(destination, "PPC Memory Probe", app, 1)


if __name__ == "__main__":
    make_memory_probe(Path(sys.argv[1]), *(int(arg) for arg in sys.argv[2:]))
