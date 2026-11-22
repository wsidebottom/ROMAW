#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_resource_probe.py
#
#   Summary:   Original unprepared resource PEF; verifies preparation and repeated calls.
#
#   Classes:   None declared.
#
#   Functions: make_resource_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original unprepared resource PEF; verifies preparation and repeated calls."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_resource_probe(destination, mode="valid"):
    fragment_data = bytearray(24)
    struct.pack_into(">II", fragment_data, 8, 24, 0)
    embedded = pef(
        [
            0x80A20010,
            0x2C050042,
            0x4082000C,
            0x7C632214,
            0x4E800020,
            0,
            0x38600042,
            0x90620010,
            0x38600000,
            0x4E800020,
        ],
        fragment_data,
        main=0,
        init=8,
        reloc=[0x4600, 0x4600],
    )
    if mode == "truncated":
        embedded = bytearray(embedded)
        # Claim a code payload outside the resource allocation. Nearby guest
        # memory must not be treated as part of this resource's PEF container.
        struct.pack_into(">I", embedded, 40 + 20, len(embedded) + 4096)
    descriptor = bytearray(32)
    struct.pack_into(">HBB", descriptor, 0, 0xAAFE, 7, 0)
    struct.pack_into(">IBBHI", descriptor, 12, 0x3F0, 0, 1, 7, 32)
    payload = descriptor + embedded
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(7)
    emit(0x808002A6, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    # The first override map in a preceding run replaces a lower map.
    emit(0x80600A50, 0x907E0064, 0x80830000, 0xA0A40016, 0x90BE0068)
    call(5)
    emit(0x809E0064, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x809E0064, 0x80840000, 0x80640010, 0x907E006C)
    emit(0xA0A40016, 0x60A50002, 0xB0A40016)
    call(5)
    emit(0x809E0064, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x809E0064, 0x80840000, 0x80BE0068, 0xB0A40016)
    emit(0x38600000)
    call(5)
    check(0)
    call(6)
    check(-193)
    emit(0x38600000 | len(payload))
    call(0)
    emit(0x907E0040, 0x80830000, 0x387E0080, 0x38A00000 | len(payload))
    call(1)
    for a, b in [(17, 25), (5, 6)]:
        emit(0x807E0040, 0x80630000, 0x388003F0, 0x38A00000 | a, 0x38C00000 | b)
        call(2)
        check(a + b)
        emit(0x807E0040, 0x80630000, 0xA8630012)
        check(4)
    emit(0x807E0040)
    call(3)
    # DeferUserFn invokes resident 68K code immediately with A0 = argument.
    emit(0x8060063C, 0x3C800003, 0x60849822, 0x38BE0050, 0x38DE0060)
    call(4)
    check(0)
    emit(0x807E0060, 0x3C801234, 0x60845678, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x8060063C, 0x3C800003, 0x60849822, 0x38A00000, 0x38DE0060)
    call(4)
    check(-50)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(128) + payload
    data[80:88] = bytes.fromhex("20bc123456784e75")
    app = pef(
        code,
        data,
        imports=[
            (
                ("PrivateInterfaceLib" if name == "GetPageableSystemZone" else "InterfaceLib"),
                name,
                2,
            )
            for name in [
                "NewHandleClear",
                "BlockMoveData",
                "CallUniversalProc",
                "DisposeHandle",
                "CallOSTrapUniversalProc",
                "GetOverrideMap",
                "ResError",
                "GetPageableSystemZone",
            ]
        ],
        main=0,
        reloc=[0x4600, 0x4A07],
    )
    write_fragment(destination, "PPC Resource Probe", app, 1)


if __name__ == "__main__":
    make_resource_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
