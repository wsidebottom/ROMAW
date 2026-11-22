#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_speech_probe.py
#
#   Summary:   Original PPC Speech Manager guest/backend integration probe (muted).
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

"""Original PPC Speech Manager guest/backend integration probe (muted)."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "CountVoices",
        "GetIndVoice",
        "GetVoiceDescription",
        "NewSpeechChannel",
        "SetSpeechInfo",
        "GetSpeechInfo",
        "SpeakText",
        "SpeechBusy",
        "TickCount",
        "WaitNextEvent",
        "DisposeSpeechChannel",
        "GetSpeechRate",
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "SpeakBuffer",
        "SpeechManagerVersion",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + 4 * index, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(15)
    emit(0x3C800150, 0x60848000, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E0110)
    call(0)
    check(0)
    emit(0xA07E0110, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x38600001, 0x389E0114)
    call(1)
    check(0)
    emit(0x387E0114, 0x389E0200, 0x38A0016A)
    call(2)
    check(0)
    emit(0x807E0200)
    check(362)
    emit(0x387E0114, 0x389E0100)
    call(3)
    check(0)
    emit(0x387E01A0, 0x388003C0, 0x38A00001)
    call(12)
    emit(0x907E01A8, 0x807E0100, 0x3C807364, 0x60846362, 0x38BE01A8)
    call(4)
    check(0)
    emit(0x807E0100, 0x3C807265, 0x60846663, 0x38BE0194)
    call(4)
    check(0)
    emit(0x387E01B0, 0x3C800000, 0x6084FFC0, 0x38A00001)
    call(12)
    emit(0x907E01B8, 0x807E0100, 0x3C807464, 0x60846362, 0x38BE01B8)
    call(4)
    check(0)
    # Volume is a Fixed scalar passed by address, zero means muted.
    emit(0x807E0100, 0x3C80766F, 0x60846C6D, 0x38BE0120)
    call(4)
    check(0)
    emit(0x807E0100, 0x3C80766F, 0x60846C6D, 0x38BE0124)
    call(5)
    check(0)
    emit(0x807E0124)
    check(0)
    emit(0x807E0100, 0x389E0124)
    call(11)
    check(0)
    emit(0xA07E0124)
    check(180)
    emit(0x807E0100, 0x389E0180, 0x38A0000C, 0x38C00000)
    call(14)
    check(0)
    emit(0x807E0100, 0x389E0180, 0x38A0000C, 0x38C00002)
    call(14)
    check(-242)  # pending text cannot be replaced in non-interrupting mode
    emit(0x807E0100, 0x389E0180, 0x38A0000C, 0x38C00004)
    call(14)
    check(-248)  # preflight is explicitly unsupported, not silently ignored
    call(8)
    emit(0x907E0128)
    loop = len(code)
    emit(0x807E01AC, 0x2C030001)
    done = len(code)
    emit(0x41820000)
    call(8)
    emit(0x809E0128, 0x7C641850, 0x280300F0)
    failures.append(len(code))
    emit(0x40800000)
    emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
    call(9)
    emit(0x48000000 | (((loop - len(code)) * 4) & 0x3FFFFFF))
    code[done] |= (len(code) - done) * 4
    call(7)
    check(0)
    emit(0x807E01BC)
    check(2)
    emit(0x807E01B8)
    call(13)
    emit(0x807E01A8)
    call(13)
    emit(0x807E0100)
    call(10)
    check(0)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    callback = len(code) * 4
    emit(0x2C040051)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x80A20100, 0x7C032800)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x38600001, 0x906201AC, 0x4E800020)
    text_callback = len(code) * 4
    emit(0x2C040051)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x810201BC, 0x39080001, 0x910201BC, 0x2C080001)
    text_done = len(code)
    emit(0x40820000)
    emit(0x39020180, 0x91050000, 0x3900000C, 0x91060000, 0x39000002, 0x91070000)
    code[text_done] |= (len(code) - text_done) * 4
    emit(0x4E800020)
    for branch in failures:
        code[branch] = (code[branch] & 0xFFFF0000) | ((bad - branch) * 4 & 0xFFFF)
    data = bytearray(0x400)
    struct.pack_into(">II", data, 0x1A0, callback, 0)
    struct.pack_into(">II", data, 0x1B0, text_callback, 0)
    struct.pack_into(">I", data, 0x194, 0x51)
    data[0x180:0x18C] = b"Speech test."
    struct.pack_into(">I", data, 0x124, 0xDEADBEEF)
    app = pef(
        code,
        data,
        imports=[
            (
                ("SpeechLib" if index < 8 or 10 <= index <= 11 or index in (14, 15) else "InterfaceLib"),
                name,
                2,
            )
            for index, name in enumerate(names)
        ],
        main=0,
        reloc=[0x4600, 0x4A0F, 0xA000, 0x1A0, 0x4600, 0xA000, 0x1B0, 0x4600],
    )
    write_fragment(destination, "PPC Speech Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
