#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_sound_probe.py
#
#   Summary:   Original PPC extended stereo PCM queue and callback timing probe.
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

"""Original PPC extended stereo PCM queue and callback timing probe."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "NewRoutineDescriptor",
        "SndNewChannel",
        "SndDoCommand",
        "TickCount",
        "WaitNextEvent",
        "SndDisposeChannel",
        "DisposeRoutineDescriptor",
        "SndSoundManagerVersion",
        "SndDoImmediate",
        "SndChannelStatus",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + 4 * index, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(7)
    emit(0x3C800321, 0x60848000, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x387E0100, 0x388003C0, 0x38A00001)
    call(0)
    emit(0x907E0124, 0x7C661B78, 0x387E0128, 0x38800005, 0x38A00000)
    call(1)
    check(0)
    emit(0x807E0128, 0x389E0150, 0x38A00000)
    call(2)  # queued pause precedes the sample buffers
    check(0)
    emit(0x807E0128, 0x389E0178, 0x38A00000)
    call(2)  # queued amplitude applies when playback resumes
    check(0)
    call(3)
    emit(0x907E012C, 0x387E0180, 0x907E0134)
    for _ in range(32):
        for offset in (0x130, 0x138):
            emit(0x807E0128, 0x389E0000 | offset, 0x38A00000)
            call(2)
            check(0)
    pause_poll = len(code)
    emit(0x807E0128, 0x38800018, 0x38BE0160)
    call(9)
    check(0)
    emit(0x887E016E, 0x2C030001)
    pause_ready = len(code)
    emit(0x41820000)
    call(3)
    emit(0x809E012C, 0x7C641850, 0x280300B4)
    failures.append(len(code))
    emit(0x40800000)
    emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
    call(4)
    emit(0x48000000 | (((pause_poll - len(code)) * 4) & 0x3FFFFFF))
    code[pause_ready] |= (len(code) - pause_ready) * 4
    call(3)
    emit(0x907E012C)
    paused_wait = len(code)
    emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
    call(4)
    call(3)
    emit(0x809E012C, 0x7C641850, 0x28030008)
    paused_done = len(code)
    emit(0x40800000)
    emit(0x48000000 | (((paused_wait - len(code)) * 4) & 0x3FFFFFF))
    code[paused_done] |= (len(code) - paused_done) * 4
    emit(0x807E0120)
    check(0)  # callbacks cannot run while the queue is paused
    emit(0x807E0128, 0x38800018, 0x38BE0160)
    call(9)
    check(0)
    emit(0x887E016E)  # SCStatus.scChannelPaused
    check(1)
    emit(0x807E0128, 0x389E0158)
    call(8)
    check(0)
    call(3)
    emit(0x907E012C)
    loop = len(code)
    emit(0x807E0120, 0x2C030020)
    done = len(code)
    emit(0x41820000)
    call(3)
    emit(0x809E012C, 0x7C641850, 0x28030078)
    failures.append(len(code))
    emit(0x40800000)
    emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
    call(4)
    emit(0x48000000 | (((loop - len(code)) * 4) & 0x3FFFFFF))
    code[done] |= (len(code) - done) * 4
    call(3)
    emit(0x809E012C, 0x7C641850, 0x28030018)
    failures.append(len(code))
    emit(0x41800000)
    for offset in (0x130, 0x138):
        emit(0x807E0128, 0x389E0000 | offset, 0x38A00000)
        call(2)
        check(0)
    emit(0x807E0128, 0x38800000)  # disposal waits for the final buffer/callback
    call(5)
    check(0)
    emit(0x807E0120)
    check(33)
    emit(0x807E0124)
    call(6)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    callback = len(code) * 4
    emit(0x80620120, 0x38630001, 0x90620120, 0x4E800020)
    data = bytearray(0x1C0 + 4096)
    struct.pack_into(">II", data, 0x100, callback, 0)
    struct.pack_into(">H", data, 0x130, 81)
    struct.pack_into(">H", data, 0x138, 13)
    struct.pack_into(">III", data, 0x180, 0, 2, 44100 << 16)
    struct.pack_into(">H", data, 0x150, 11)  # pauseCmd
    struct.pack_into(">H", data, 0x158, 12)  # resumeCmd
    struct.pack_into(">HH", data, 0x178, 43, 128)  # ampCmd, half amplitude
    data[0x194] = 0xFF
    struct.pack_into(">I", data, 0x196, 1024)
    struct.pack_into(">H", data, 0x1B0, 16)
    for frame in range(1024):
        sample = 4096 if frame & 16 else -4096
        struct.pack_into(">hh", data, 0x1C0 + frame * 4, sample, sample)
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A09, 0xA000, 0x100, 0x4600],
    )
    write_fragment(destination, "PPC Sound Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
