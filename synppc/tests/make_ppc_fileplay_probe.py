#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_fileplay_probe.py
#
#   Summary:   Original PPC AIFF File Manager, Sound Manager and completion probe.
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

"""Original PPC AIFF File Manager, Sound Manager and completion probe."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "GetCurrentProcess",
        "GetProcessInformation",
        "HSetVol",
        "FSOpen",
        "FSClose",
        "SetFPos",
        "GetFPos",
        "SndNewChannel",
        "SndStartFilePlay",
        "SndPauseFilePlay",
        "SndStopFilePlay",
        "SndChannelStatus",
        "SndDisposeChannel",
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "TickCount",
        "WaitNextEvent",
    ]
    code, failures = [], []

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

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x40820000)

    def start(channel, completion, asynchronous):
        emit(
            0x807E0128 if channel else 0x38600000,
            0xA89E0110,
            0x38A00000,
            0x38C00000,
            0x38E00000,
            0x39000000,
            0x813E0134 if completion else 0x39200000,
            0x39400000 | asynchronous,
        )
        call("SndStartFilePlay")
        check(0)

    def wait_ticks(ticks):
        call("TickCount")
        emit(0x907E0138)
        loop = len(code)
        emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
        call("WaitNextEvent")
        call("TickCount")
        emit(0x809E0138, 0x7C641850, 0x28030000 | ticks)
        emit(0x41800000 | (((loop - len(code)) * 4) & 0xFFFF))

    def wait_completion(count):
        call("TickCount")
        emit(0x907E0138)
        loop = len(code)
        emit(0x807E0130, 0x2C030000 | count)
        done = len(code)
        emit(0x41820000)
        call("TickCount")
        emit(0x809E0138, 0x7C641850, 0x280300B4)
        failures.append(len(code))
        emit(0x40800000)
        emit(0x38600000, 0x389E0140, 0x38A00001, 0x38C00000)
        call("WaitNextEvent")
        emit(0x48000000 | (((loop - len(code)) * 4) & 0x3FFFFFF))
        code[done] |= (len(code) - done) * 4

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x387E0070)
    call("GetCurrentProcess")
    check(0)
    emit(0x3860003C, 0x907E0080, 0x387E00C0, 0x907E00B8, 0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check(0)
    emit(0x38600000, 0xA89E00C0, 0x80BE00C2)
    call("HSetVol")
    check(0)
    emit(0x387E01C0, 0x38800000, 0x38BE0110)
    call("FSOpen")
    check(0)
    emit(0xA87E0110, 0x38800001, 0x38A00011)
    call("SetFPos")
    check(0)
    start(False, False, 0)  # Internally owned synchronous channel.
    emit(0xA87E0110, 0x389E0114)
    call("GetFPos")
    check(0)
    emit(0x807E0114)
    check(17)
    emit(0x387E01A0, 0x38800028, 0x38A00001)
    call("NewRoutineDescriptor")
    emit(0x907E0134, 0x387E0128, 0x38800005, 0x38A00000, 0x38C00000)
    call("SndNewChannel")
    check(0)
    start(True, True, 1)
    emit(0x807E0128)
    call("SndPauseFilePlay")
    check(0)
    emit(0x807E0128, 0x38800018, 0x38BE0160)
    call("SndChannelStatus")
    check(0)
    emit(0x887E016E)
    check(1)
    wait_ticks(8)
    emit(0x807E0130)
    check(0)
    emit(0x807E0128)
    call("SndPauseFilePlay")
    check(0)
    wait_completion(1)
    emit(0x807E0130)
    check(1)
    emit(0x807E0128, 0x38800018, 0x38BE0160)
    call("SndChannelStatus")
    check(0)
    emit(0x887E016C)
    check(0)
    start(True, True, 1)
    emit(0x807E0128, 0x38800001)
    call("SndStopFilePlay")
    check(0)
    wait_ticks(60)
    emit(0x807E0130)
    check(1)
    emit(
        0x807E0128,
        0x38800000,
        0x38A00064,
        0x38C00000,
        0x38E00000,
        0x39000000,
        0x813E0134,
        0x39400001,
    )
    call("SndStartFilePlay")  # Application resource fork, not a data-fork file.
    check(0)
    wait_completion(2)
    emit(0x807E0130)
    check(2)
    emit(0xA87E0110)
    call("FSClose")
    check(0)
    for filename, completed in [(0x1D0, 3), (0x1E0, 4)]:
        emit(0x387E0000 | filename, 0x38800000, 0x38BE0110)
        call("FSOpen")
        check(0)
        start(True, True, 1)
        wait_completion(completed)
        emit(0xA87E0110)
        call("FSClose")
        check(0)
    emit(0x807E0128, 0x38800001)
    call("SndDisposeChannel")
    check(0)
    emit(0x807E0134)
    call("DisposeRoutineDescriptor")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    callback = len(code) * 4
    emit(0x80820128, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x80620130, 0x38630001, 0x90620130, 0x4E800020)
    for branch in failures:
        code[branch] |= ((bad - branch) * 4) & 0xFFFF
    data = bytearray(0x200)
    struct.pack_into(">II", data, 0x1A0, callback, 0)
    data[0x1C0:0x1CB] = b"\x0aProbe.aiff"
    data[0x1D0:0x1DB] = b"\x0aProbe.mac3"
    data[0x1E0:0x1EB] = b"\x0aProbe.mac6"
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", n, 2) for n in names],
        main=0,
        reloc=[0x4600, 0x4A10, 0xA000, 0x1A0, 0x4600],
    )
    sound = struct.pack(">HHHIHHHI", 1, 1, 5, 0x80, 1, 0x8051, 0, 20)
    sound += struct.pack(">IIIIIBB", 0, 4410, 22050 << 16, 0, 0, 0, 60)
    sound += bytes([136]) * 4410
    write_fragment(
        destination,
        "PPC Fileplay Probe",
        app,
        1,
        extra_resources=[(b"snd ", 100, sound)],
    )
    pcm = b"".join(struct.pack(">hh", 2048 if i & 16 else -2048, 2048 if i & 16 else -2048) for i in range(4410))
    common = struct.pack(">hIh", 2, 4410, 16) + bytes.fromhex("400dac44000000000000")
    chunks = b"COMM" + struct.pack(">I", len(common)) + common
    chunks += b"SSND" + struct.pack(">III", len(pcm) + 8, 0, 0) + pcm
    (destination / "Probe.aiff").write_bytes(b"FORM" + struct.pack(">I", len(chunks) + 4) + b"AIFF" + chunks)
    seed, encoded = 0x12345678, bytearray()
    for _ in range(2048):
        seed ^= (seed << 13) & 0xFFFFFFFF
        seed ^= seed >> 17
        seed ^= (seed << 5) & 0xFFFFFFFF
        encoded.append(seed & 255)
    for codec, packets, filename in [
        (b"MAC3", 1024, "Probe.mac3"),
        (b"MAC6", 2048, "Probe.mac6"),
    ]:
        common = struct.pack(">hIh", 1, packets, 8)
        common += bytes.fromhex("400dac44000000000000") + codec + b"\0\0"
        chunks = b"FVER" + struct.pack(">II", 4, 0xA2805140)
        chunks += b"COMM" + struct.pack(">I", len(common)) + common
        chunks += b"SSND" + struct.pack(">III", len(encoded) + 8, 0, 0) + encoded
        (destination / filename).write_bytes(b"FORM" + struct.pack(">I", len(chunks) + 4) + b"AIFC" + chunks)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
