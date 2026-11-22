#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_movie_probe.py
#
#   Summary:   Original built-in Movie Toolbox probe with three generated RGB frames.
#
#   Classes:   None declared.
#
#   Functions: raw_movie, make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original built-in Movie Toolbox probe with three generated RGB frames."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def raw_movie():
    def atom(kind, payload):
        return struct.pack(">I4s", len(payload) + 8, kind) + payload

    description = bytearray(86)
    struct.pack_into(">I4s", description, 0, 86, b"raw ")
    struct.pack_into(">H", description, 14, 1)
    struct.pack_into(">HH", description, 32, 2, 2)
    struct.pack_into(">H", description, 82, 24)
    stbl = b"".join(
        [
            atom(b"stsd", struct.pack(">II", 0, 1) + description),
            atom(b"stts", struct.pack(">4I", 0, 1, 3, 30)),
            atom(b"stsc", struct.pack(">5I", 0, 1, 1, 3, 1)),
            atom(b"stsz", struct.pack(">3I", 0, 12, 3)),
            atom(b"stco", struct.pack(">3I", 0, 1, 8)),
        ]
    )
    mdia = atom(b"mdhd", struct.pack(">6I", 0, 0, 0, 60, 90, 0))
    mdia += atom(b"hdlr", bytes(4) + b"mhlrvide" + bytes(12))
    dref = atom(b"dref", struct.pack(">II", 0, 1) + atom(b"url ", struct.pack(">I", 1)))
    mdia += atom(
        b"minf",
        atom(b"vmhd", struct.pack(">I4H", 1, 0, 0, 0, 0)) + atom(b"dinf", dref) + atom(b"stbl", stbl),
    )
    matrix = struct.pack(">9I", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)
    mvhd = bytearray(100)
    struct.pack_into(">IIIH", mvhd, 12, 60, 90, 0x10000, 0x100)
    mvhd[36:72] = matrix
    struct.pack_into(">I", mvhd, 96, 2)
    tkhd = bytearray(84)
    struct.pack_into(">I", tkhd, 0, 7)
    struct.pack_into(">I", tkhd, 12, 1)
    struct.pack_into(">I", tkhd, 20, 90)
    tkhd[40:76] = matrix
    struct.pack_into(">II", tkhd, 76, 2 << 16, 2 << 16)
    frames = b"".join(bytes(color) * 4 for color in [(255, 0, 0), (0, 255, 0), (0, 0, 255)])
    return atom(b"mdat", frames) + atom(
        b"moov",
        atom(b"mvhd", mvhd) + atom(b"trak", atom(b"tkhd", tkhd) + atom(b"mdia", mdia)),
    )


def make_probe(destination):
    names = [
        "InitWindows",
        "GetCurrentProcess",
        "GetProcessInformation",
        "HSetVol",
        "FSOpen",
        "NewGWorld",
        "SetGWorld",
        "GetCPixel",
        "DisposeGWorld",
        "EnterMovies",
        "NewMovieFromFile",
        "CloseMovieFile",
        "UpdateMovie",
        "PrerollMovie",
        "IsMovieDone",
        "DisposeMovie",
        "ExitMovies",
        "Gestalt",
        "GetGWorld",
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
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    for selector, expected in [(b"qtim", 0x02508000), (b"ttsc", 1)]:
        value = int.from_bytes(selector, "big")
        emit(0x3C600000 | (value >> 16), 0x60630000 | (value & 65535), 0x389E0190)
        call("Gestalt")
        check(0)
        emit(
            0x807E0190,
            0x3C800000 | (expected >> 16),
            0x60840000 | (expected & 65535),
            0x7C032000,
        )
        failures.append(len(code))
        emit(0x40820000)
    emit(0x387E0070)
    call("GetCurrentProcess")
    check(0)
    emit(0x3860003C, 0x907E0080, 0x387E00C0, 0x907E00B8, 0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check(0)
    emit(0x38600000, 0xA89E00C0, 0x80BE00C2)
    call("HSetVol")
    check(0)
    emit(0x387E0150, 0x38800000, 0x38BE0110)
    call("FSOpen")
    check(0)
    emit(0x387E0194, 0x389E0198)
    call("GetGWorld")
    emit(0x387E0114, 0x38800020, 0x38BE0130, 0x38C00000, 0x38E00000, 0x39000000)
    call("NewGWorld")
    check(0)
    emit(0x807E0114, 0x38800000)
    call("SetGWorld")
    call("EnterMovies")
    check(0)
    emit(0x387E0118, 0xA89E0110, 0x38A00000, 0x38C00000, 0x38E00001, 0x39000000)
    call("NewMovieFromFile")
    check(0)
    emit(0xA87E0110)
    call("CloseMovieFile")
    check(0)  # Movie must own its data after the source file closes.
    for index in range(3):
        emit(0x807E0118, 0x38800000 | (index * 30), 0x3CA00001)
        call("PrerollMovie")
        check(0)
        emit(0x807E0118)
        call("UpdateMovie")
        check(0)
        emit(0x38600001, 0x38800001, 0x38BE0140)
        call("GetCPixel")
        for component in range(3):
            emit(0xA07E0140 + component * 2)
            # Compare unsigned words through cmpwi's signed immediate.
            emit(0x28030000 | (65535 if component == index else 0))
            failures.append(len(code))
            emit(0x40820000)
    emit(0x807E0118, 0x3880005A, 0x3CA00001)
    call("PrerollMovie")
    check(0)
    emit(0x807E0118)
    call("IsMovieDone")
    check(1)
    emit(0x807E0118)
    call("DisposeMovie")
    call("ExitMovies")
    # Subsequent checks run in the same QuickDraw session.
    emit(0x807E0194, 0x809E0198)
    call("SetGWorld")
    emit(0x807E0114)
    call("DisposeGWorld")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    for branch in failures:
        code[branch] |= (len(code) - branch) * 4
    emit(0)
    data = bytearray(0x200)
    struct.pack_into(">4h", data, 0x130, 0, 0, 2, 2)
    data[0x150:0x15A] = b"\x09Probe.mov"
    app = pef(
        code,
        data,
        imports=[("QuickTimeLib" if 9 <= index <= 16 else "InterfaceLib", name, 2) for index, name in enumerate(names)],
        main=0,
        reloc=[0x4600, 0x4A00 + len(names) - 1],
    )
    write_fragment(destination, "PPC Movie Probe", app, 1)
    (destination / "Probe.mov").write_bytes(raw_movie())


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
