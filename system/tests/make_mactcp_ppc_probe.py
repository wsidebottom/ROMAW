#!/usr/bin/env python3
# +===================================================================
#   File:      make_mactcp_ppc_probe.py
#
#   Summary:   Build an original PowerPC MacTCP driver and callback ABI probe.
#
#   Classes:   None.
#
#   Functions: make_probe.
#
#   Origin:    ROMAW original PowerPC fixture using the repository PEF builder.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
"""Generate a real PPC application; no Apple code is embedded."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "synppc" / "tests"))
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "OpenDriver",
        "CloseDriver",
        "GetDCtlEntry",
        "PBControlSync",
        "PBControlAsync",
        "PBStatusAsync",
        "PBKillIOSync",
        "PBKillIOAsync",
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "SystemTask",
        "TickCount",
        "DebugStr",
    ]
    code, failures = [], []
    stage = 0

    def emit(*words):
        code.extend(words)

    def call(name):
        index = names.index(name)
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def li(reg, value):
        emit(0x38000000 | reg << 21 | (value & 65535))

    def addr(reg, offset):
        emit(0x38000000 | reg << 21 | 30 << 16 | offset)

    def load(reg, offset, base=30):
        emit(0x80000000 | reg << 21 | base << 16 | offset)

    def store(reg, offset, base=30, half=False):
        emit((0xB0000000 if half else 0x90000000) | reg << 21 | base << 16 | offset)

    def check(value=0):
        nonlocal stage
        stage += 1
        li(31, stage)
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def epilogue():
        emit(0x83A10070, 0x83E10074, 0x83C10078, 0x80010084, 0x7C0803A6, 0x38210080, 0x4E800020)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0x93A10070, 0x93E10074, 0x93C10078, 0x7C5E1378)
    addr(3, 0x80)
    addr(4, 0x90)
    call("OpenDriver")
    check()
    emit(0xA87E0090)
    call("GetDCtlEntry")
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)

    # Both callback entry points are PPC transition vectors.
    for vector, slot in [(0x100, 0x140), (0x110, 0x144), (0x120, 0x148)]:
        addr(3, vector)
        li(4, 0x3EC0 if vector == 0x120 else 0xC1)  # Pascal UDP notify or C completion.
        li(5, 1)
        call("NewRoutineDescriptor")
        store(3, slot)

    # Initialize packed parameter blocks with halfword stores where required.
    emit(0xA07E0090)
    store(3, 0x218, half=True)
    store(3, 0x298, half=True)
    addr(3, 0x200)
    call("PBControlSync")
    check()
    load(3, 0x21C)
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)

    load(3, 0x140)
    store(3, 0x28C)
    load(3, 0x144)
    emit(0x5464843E)  # srwi r4,r3,16.
    store(4, 0x2AE, half=True)
    store(3, 0x2B0, half=True)
    addr(3, 0x800)
    emit(0x5464843E)
    store(4, 0x2A2, half=True)
    store(3, 0x2A4, half=True)
    addr(3, 0x280)
    call("PBControlAsync")
    check()

    call("TickCount")
    emit(0x3BA3012C)  # addi r29,r3,300.
    loop = len(code)
    call("SystemTask")
    load(3, 0xA0)
    load(4, 0xB0)
    emit(0x7C632214)  # add r3,r3,r4.
    emit(0x2C030002)
    done = len(code)
    emit(0x41820000)
    call("TickCount")
    emit(0x7C63E850)  # subf r3,r3,r29.
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x40810000)  # ble failure.
    emit(0x48000000 | (((loop - len(code)) * 4) & 0x3FFFFFF))
    code[done] |= (len(code) - done) * 4
    load(3, 0xA0)
    check(1)
    load(3, 0xB0)
    check(1)
    load(3, 0xD0)
    check()

    # Exercise Pascal UDP notification arguments and opaque high-bit user data.
    emit(0xA07E0090)
    for offset in (0x418, 0x518, 0x618):
        store(3, offset, half=True)
    addr(3, 0x1000)
    store(3, 0x420)
    load(3, 0x148)
    store(3, 0x428)
    li(3, 0x9234)
    store(3, 0x42E, half=True)
    li(3, 0x5678)
    store(3, 0x430, half=True)
    addr(3, 0x400)
    call("PBControlSync")
    check()
    load(3, 0x41C)
    store(3, 0x51C)
    store(3, 0x61C)
    emit(0xA07E042C)
    store(3, 0x526, half=True)
    addr(3, 0x850)
    store(3, 0x528)
    addr(3, 0x800)
    emit(0x5464843E)
    store(4, 0x852, half=True)
    store(3, 0x854, half=True)
    load(3, 0x140)
    store(3, 0x50C)
    addr(3, 0x500)
    call("PBControlAsync")
    check()
    call("TickCount")
    emit(0x3BA3012C)
    udp_loop = len(code)
    call("SystemTask")
    load(3, 0xC0)
    emit(0x2C030001)
    udp_done = len(code)
    emit(0x41820000)
    call("TickCount")
    emit(0x7C63E850, 0x2C030000)
    failures.append(len(code))
    emit(0x40810000)
    emit(0x48000000 | (((udp_loop - len(code)) * 4) & 0x3FFFFFF))
    code[udp_done] |= (len(code) - udp_done) * 4
    load(3, 0xD0)
    check()
    load(3, 0xA0)
    check(2)
    addr(3, 0x600)
    call("PBControlSync")
    check()
    emit(0xA07E062C)
    check(16)
    load(4, 0x628)
    load(3, 0, 4)
    emit(0x3C801122, 0x60843344, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    li(3, 22)
    store(3, 0x61A, half=True)
    addr(3, 0x600)
    call("PBControlSync")
    check()
    li(3, 24)
    store(3, 0x41A, half=True)
    addr(3, 0x400)
    call("PBControlSync")
    check()

    for slot in (0x140, 0x144, 0x148):
        load(3, slot)
        call("DisposeRoutineDescriptor")
    emit(0xA87E0090)
    call("CloseDriver")
    check()
    addr(3, 0x900)
    call("DebugStr")
    li(3, 0)
    epilogue()
    bad = len(code)
    addr(3, 0x940)
    call("DebugStr")
    emit(0x7FE3FB78)  # mr r3,r31: diagnostic stage.
    epilogue()
    for branch in failures:
        code[branch] |= ((bad - branch) * 4) & 65535

    # Callbacks preserve the ABI's nonvolatile registers and use their own TOC.
    callback_failures = []

    def callback_check(value):
        emit(0x2C070000 | (value & 65535))
        callback_failures.append(len(code))
        emit(0x40820000)

    io_callback = len(code) * 4
    emit(0xA8E30010)  # lha r7,16(r3).
    callback_check(0)
    emit(0x80E200A0, 0x38E70001, 0x90E200A0, 0x4E800020)
    echo_callback = len(code) * 4
    emit(0xA8E30010)
    callback_check(0)
    emit(0xA0E3003A)
    callback_check(24)
    emit(0x80E30044)
    callback_check(0x1234)
    emit(0x80E3003C, 0x80E70008, 0x3D001122, 0x61083344, 0x7C074000)
    callback_failures.append(len(code))
    emit(0x40820000)
    emit(0x80E200B0, 0x38E70001, 0x90E200B0, 0x4E800020)
    udp_callback = len(code) * 4
    emit(0x7C872378)  # mr r7,r4: event.
    callback_check(1)
    emit(0x3CE09234, 0x60E75678, 0x7C053800)
    callback_failures.append(len(code))
    emit(0x40820000)
    emit(0x80E2041C, 0x7C033800)
    callback_failures.append(len(code))
    emit(0x40820000)
    emit(0x7CC73378)  # mr r7,r6: ICMP report.
    callback_check(0)
    emit(0x80E200C0, 0x38E70001, 0x90E200C0, 0x4E800020)
    cb_bad = len(code)
    emit(0x38E00001, 0x90E200D0, 0x4E800020)
    for branch in callback_failures:
        code[branch] |= ((cb_bad - branch) * 4) & 65535

    data = bytearray(0x3000)
    data[0x80:0x85] = b"\x04.IPP"
    struct.pack_into(">II", data, 0x100, io_callback, 0)
    struct.pack_into(">II", data, 0x110, echo_callback, 0)
    struct.pack_into(">II", data, 0x120, udp_callback, 0)
    struct.pack_into(">H", data, 0x41A, 20)
    struct.pack_into(">I", data, 0x424, 4096)
    struct.pack_into(">H", data, 0x51A, 23)
    struct.pack_into(">I", data, 0x522, 0x7F000001)
    data[0x52C] = 1
    struct.pack_into(">H", data, 0x61A, 21)
    struct.pack_into(">H", data, 0x620, 2)
    struct.pack_into(">H", data, 0x850, 16)
    struct.pack_into(">H", data, 0x21A, 15)
    struct.pack_into(">HIH", data, 0x29A, 17, 0x7F000001, 16)
    struct.pack_into(">H", data, 0x2A6, 2)
    struct.pack_into(">I", data, 0x2B2, 0x1234)
    struct.pack_into(">4I", data, 0x800, *([0x11223344] * 4))
    for offset, message in [(0x900, b"PPC MacTCP Probe passed"), (0x940, b"PPC MacTCP Probe failed")]:
        data[offset : offset + len(message) + 1] = bytes([len(message)]) + message
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1), 0xA000, 0x100, 0x4600, 0xA000, 0x110, 0x4600, 0xA000, 0x120, 0x4600],
    )
    write_fragment(destination, "PPC MacTCP Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
