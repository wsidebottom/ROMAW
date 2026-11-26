#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_native_memory_probe.py
#
#   Summary:   Generate a fat application checking native Memory Manager and classic low-memory imports.
#
#   Classes:   None.
#
#   Functions: make_probe.
#
#   Origin:    Original ROMAW PPC and 68K fixture instructions; no Apple code.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "MMGetDefaultHeap",
        "MMNewHeap",
        "MMAllocateClearIn",
        "MMBlockSize",
        "MMReallocate",
        "MMGetHeap",
        "MMFree",
        "MMDisposeHeap",
        "MMGetFreeSpace",
    ]
    temporaries = ["TempFreeMem", "TempMaxMem", "TempNewHandle", "TempHLock", "TempHUnlock", "TempDisposeHandle"]
    lowmem = [
        "LMGetSysZone",
        "LMGetTicks",
        "LMGetTime",
        "LMGetTimeDBRA",
        "LMGetMBarHeight",
        "LMGetTEScrpLength",
        "LMSetTEScrpLength",
        "LMSetCurActivate",
        "LMGetHiliteMode",
        "LMSetHiliteMode",
    ]
    imports = [("Memory Manager", name, 2) for name in names]
    imports += [("InterfaceLib", name, 2) for name in [*temporaries, "DebugStr", *lowmem, "SystemZone"]]
    names += [*temporaries, "DebugStr", *lowmem, "SystemZone"]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0100 + 4 * names.index(name), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    def equals(offset):
        emit(0x809E0000 + offset, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("LMGetSysZone")
    check(0, True)
    emit(0x907E00E0, 0x808002A6, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    call("SystemZone")
    equals(0xE0)
    # Clocks can advance while the guest crosses a native service boundary.
    for name, load in (("LMGetTicks", 0x8080016A), ("LMGetTime", 0x8080020C)):
        emit(load - 0x200000, 0x907E00E8)
        call(name)
        emit(load, 0x7C032040)
        failures.append(len(code))
        emit(0x41810000)
        emit(0x809E00E8, 0x7C032040)
        failures.append(len(code))
        emit(0x41800000)
    for name, load in (
        ("LMGetTimeDBRA", 0xA8800D00),
        ("LMGetMBarHeight", 0xA8800BAA),
        ("LMGetTEScrpLength", 0xA0800AB0),
        ("LMGetHiliteMode", 0x88800938),
    ):
        call(name)
        emit(load, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    # Preserve live state when checking the classic setters. These pointers
    # belong to the actual Window Manager, rather than synthetic UI objects.
    for name, load in (
        ("LMSetTEScrpLength", 0xA0600AB0),
        ("LMSetCurActivate", 0x80600A64),
        ("LMSetHiliteMode", 0x88600938),
    ):
        emit(load, 0x907E00E4)
        call(name)
        emit(load)
        equals(0xE4)
    call("MMGetDefaultHeap")
    check(0, True)
    emit(0x907E0080, 0x38600001, 0x38801000, 0x38A02000, 0x38C00000)
    call("MMNewHeap")
    check(0, True)
    emit(0x907E0084, 0x7C641B78, 0x38600020)
    call("MMAllocateClearIn")
    check(0, True)
    emit(0x907E0088)
    for offset in range(0, 32, 4):
        emit(0x809E0088, 0x80640000 + offset)
        check(0)
    emit(0x807E0088)
    call("MMBlockSize")
    check(32)
    emit(0x807E0088)
    call("MMGetHeap")
    equals(0x84)
    emit(0x807E0088, 0x3C801234, 0x60845678, 0x90830000, 0x38800040)
    call("MMReallocate")
    check(0, True)
    emit(0x907E0088, 0x80630000, 0x3C801234, 0x60845678, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E0088)
    call("MMBlockSize")
    check(64)
    emit(0x807E0084, 0x389E0090, 0x38BE0094)
    call("MMGetFreeSpace")
    emit(0x807E0090)
    check(0, True)
    emit(0x807E0094)
    check(0, True)
    emit(0x807E0088)
    call("MMFree")
    emit(0x807E0084)
    call("MMDisposeHeap")
    call("MMGetDefaultHeap")
    equals(0x80)
    call("TempFreeMem")
    check(0, True)
    emit(0x387E00D0)
    call("TempMaxMem")
    check(0, True)
    emit(0x38600020, 0x389E00D4)
    call("TempNewHandle")
    check(0, True)
    emit(0x907E00D8)
    for operation in ("TempHLock", "TempHUnlock", "TempDisposeHandle"):
        emit(0x807E00D8, 0x389E00D4)
        call(operation)
        emit(0xA87E00D4)
        check(0)
    emit(0x387E00A0)
    call("DebugStr")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    # Keep the enlarged import table separate from heap-check scratch data.
    data = bytearray(512)
    message = b"PPC_NATIVE_MEMORY_OK"
    data[0xA0 : 0xA1 + len(message)] = bytes([len(message)]) + message
    image = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0xA000, 0x100, 0x4A00 | (len(imports) - 1)])
    # A valid classic alternative exits directly. A successful PPC completion
    # and marker therefore establish preference over the classic CODE path.
    classic = [
        (b"CODE", 0, struct.pack(">4I4H", 448, 0, 8, 32, 0, 0x3F3C, 1, 0xA9F0)),
        (b"CODE", 1, struct.pack(">3H", 0, 1, 0xA9F4)),
    ]
    write_fragment(destination, "PPC Native Memory Probe", image, 1, classic)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
