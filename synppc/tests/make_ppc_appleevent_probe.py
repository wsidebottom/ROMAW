#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_appleevent_probe.py
#
#   Summary:   Original PPC fixture for AppleEvent handler identity and descriptor lifetime.
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

"""Original PPC fixture for AppleEvent handler identity and descriptor lifetime."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "AEInstallEventHandler",
        "AEGetEventHandler",
        "AERemoveEventHandler",
        "AECreateDesc",
        "AEDisposeDesc",
        "AECreateList",
        "AEPutKeyDesc",
        "AEPutDesc",
        "AESizeOfKeyDesc",
        "AESizeOfNthItem",
        "AECountItems",
        "AEPutPtr",
        "AEGetNthPtr",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def selector():
        emit(0x3C605445, 0x60635354, 0x3C805050, 0x60844321)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x387E00F0, 0x38800FE0, 0x38A00001)
    call("NewRoutineDescriptor")
    emit(0x907E0048)
    selector()
    emit(0x80BE0048, 0x3CC01234, 0x60C65678, 0x38E00000)
    call("AEInstallEventHandler")
    check(0)
    selector()
    emit(0x38BE0050, 0x38DE0054, 0x38E00000)
    call("AEGetEventHandler")
    check(0)
    emit(0x807E0050, 0x809E0048, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0054, 0x3C801234, 0x60845678, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    selector()
    emit(0x80BE0048, 0x38C00000)
    call("AERemoveEventHandler")
    check(0)
    selector()
    emit(0x38BE0050, 0x38DE0054, 0x38E00000)
    call("AEGetEventHandler")
    check(-1717)
    emit(0x807E0048)
    call("DisposeRoutineDescriptor")
    emit(0x3C605445, 0x60635354, 0x389E0068, 0x38A00004, 0x38DE0060)
    call("AECreateDesc")
    check(0)
    emit(0x807E0060, 0x3C805445, 0x60845354, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E0060)
    call("AEDisposeDesc")
    check(0)
    # A valid empty descriptor may have a null data handle (MATM 1.5).
    # Test both record keys and list elements, with null and typed-empty data.
    for record in (False, True):
        emit(0x38600000, 0x38800000, 0x38A00000 | int(record), 0x38DE0090)
        call("AECreateList")
        check(0)
        for index, (keyword, descriptor_type) in enumerate(((0x7A65726F, 0x6E756C6C), (0x656D7074, 0x54455854)), 1):
            emit(0x387E0090)
            if record:
                emit(0x3C800000 | (keyword >> 16), 0x60840000 | (keyword & 0xFFFF))
            else:
                emit(0x38800000)
            emit(0x38BE0080 + (index - 1) * 8)
            call("AEPutKeyDesc" if record else "AEPutDesc")
            check(0)
            emit(0x387E0090)
            if record:
                emit(0x3C800000 | (keyword >> 16), 0x60840000 | (keyword & 0xFFFF))
            else:
                emit(0x38800000 | index)
            emit(0x38BE00A0, 0x38DE00A4)
            call("AESizeOfKeyDesc" if record else "AESizeOfNthItem")
            check(0)
            emit(0x807E00A4)
            check(0)
            emit(
                0x807E00A0,
                0x3C800000 | (descriptor_type >> 16),
                0x60840000 | (descriptor_type & 0xFFFF),
                0x7C032000,
            )
            failures.append(len(code))
            emit(0x40820000)
        emit(0x387E0090, 0x389E00A8)
        call("AECountItems")
        check(0)
        emit(0x807E00A8)
        check(2)
        emit(0x387E0090)
        call("AEDisposeDesc")
        check(0)
    # AEPutPtr copies caller bytes into an independent list element. Read
    # them back after changing the source, and exercise replacement and append.
    emit(0x38600000, 0x38800000, 0x38A00000, 0x38DE0090)
    call("AECreateList")
    check(0)
    for index, value in ((0, 0x12345678), (1, 0xDEADBEEF)):
        emit(0x3C600000 | (value >> 16), 0x60630000 | (value & 0xFFFF), 0x907E0068)
        emit(0x387E0090, 0x38800000 | index, 0x3CA05445, 0x60A55854, 0x38DE0068, 0x38E00004)
        call("AEPutPtr")
        check(0)
        emit(0x38600000, 0x907E0068, 0x38605A5A, 0x907E00C0)
        emit(0x387E0090, 0x38800001, 0x3CA02A2A, 0x60A52A2A)
        emit(0x38DE00B0, 0x38FE00B4, 0x391E00B8, 0x39200004, 0x395E00BC)
        call("AEGetNthPtr")
        check(0)
        emit(0x807E00B8, 0x3C800000 | (value >> 16), 0x60840000 | (value & 0xFFFF), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        emit(0x807E00B4, 0x3C805445, 0x60845854, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        emit(0x807E00BC)
        check(4)
        emit(0x807E00C0)
        check(0x5A5A)
    # An invalid index must leave the existing list intact.
    emit(0x387E0090, 0x38800005, 0x3CA05445, 0x60A55854, 0x38DE0068, 0x38E00004)
    call("AEPutPtr")
    check(-1719)
    emit(0x387E0090, 0x389E00A8)
    call("AECountItems")
    check(0)
    emit(0x807E00A8)
    check(1)
    emit(0x387E0090)
    call("AEDisposeDesc")
    check(0)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    callback = len(code) * 4
    emit(0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    assert 8 + 4 * len(names) <= 72
    data = bytearray(256)
    struct.pack_into(">II", data, 240, callback, 0)
    struct.pack_into(">I", data, 104, 0x12345678)
    struct.pack_into(">IIII", data, 128, 0x6E756C6C, 0, 0x54455854, 0)
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1), 0xA000, 240, 0x4600],
    )
    write_fragment(destination, "PPC AppleEvent Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
