#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_native_libraries_probe.py
#
#   Summary:   Original SOM allocator and real CFM notification lifecycle checks.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original ROMAW fixture code; private notification ABI checked against local CFM reference.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    imports = [("SOM", name, 1) for name in ("SOMMalloc", "SOMCalloc", "SOMRealloc", "SOMFree")]
    imports += [("InterfaceLib", name, 2) for name in ("GetSharedLibrary", "CloseConnection", "DebugStr", "FindSymbol")]
    imports += [("PrivateInterfaceLib", name, 2) for name in ("FragAddNotifyProc", "FragRemoveNotifyProc")]
    p = Probe(imports)

    def allocator(index):
        p.emit(0x819E0008 + 4 * index, 0x818C0000, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    p.emit(0x38600004, 0x38800008)
    allocator(1)
    p.nonzero("SOM calloc data export invokes native allocator")
    p.emit(0x907E0100)
    for offset in range(0, 32, 4):
        p.emit(0x809E0100, 0x80640000 + offset)
        p.expect(0, "SOM calloc clears guest bytes")
    p.emit(0x807E0100, 0x3C801234, 0x60845678, 0x90830000, 0x3880FFFF)
    allocator(2)
    p.expect(0, "SOM realloc failure returns null")
    p.emit(0x807E0100, 0x80630000)
    p.expect(0x12345678, "SOM realloc failure retains original storage")
    p.emit(0x807E0100)
    allocator(3)
    # Allocator exports are writable variables containing transition vectors.
    p.emit(0x819E0008, 0x80CC0000, 0x38000000, 0x900C0000, 0x806C0000)
    p.expect(0, "SOM allocation function variable remains writable")
    p.emit(0x90CC0000, 0x38600010)
    allocator(0)
    p.nonzero("restored SOM malloc function variable is callable")
    allocator(3)
    p.emit(0x38600000)
    p.call("FragAddNotifyProc")
    p.expect(0xFFFFFFCE, "null CFM notifier rejected")
    for _ in range(2):
        p.emit(0x387E0200)
        p.call("FragAddNotifyProc")
        p.expect(0, "CFM accepts duplicate notification registrations")
    p.emit(0x387E0200)
    p.call("FragRemoveNotifyProc")
    p.expect(0, "CFM removes only one duplicate registration")

    def load():
        p.emit(0x387E0240, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0180, 0x38FE0184, 0x391E0280)
        p.call("GetSharedLibrary")
        p.expect(0, "load original target library and initialize its closure")

    def close():
        p.emit(0x387E0180)
        p.call("CloseConnection")
        p.expect(0, "close actual library closure")

    load()
    for event in (0, 1, 4):
        p.emit(0x807E0120 + 4 * event)
        p.expect(1, "CFM create, attach and initialized notifications")
    p.emit(0x807E010C, 0x809E0180)
    p.equal("notification receives actual owning closure ID")
    close()
    for event in range(5):
        p.emit(0x807E0120 + 4 * event)
        p.expect(1, "CFM detach and release each delivered once")
    p.emit(0x387E0200)
    p.call("FragRemoveNotifyProc")
    p.expect(0, "remove remaining notifier")
    p.emit(0x387E0200)
    p.call("FragRemoveNotifyProc")
    p.expect(0xFFFFF50C, "missing notifier reports the reference not-found error")
    load()
    close()
    for event in range(5):
        p.emit(0x807E0120 + 4 * event)
        p.expect(1, "removed notifier is not called during reload")

    # Private copies share PIC text but own distinct data/TOCs. Register a
    # callback from the older copy after creating the newer one, then close
    # the newer copy. The older callback must remain registered.
    for slot in (0x180, 0x18C):
        p.emit(0x387E03A0, 0x3C807077, 0x60847063, 0x38A00005, 0x38DE0000 | slot, 0x38FE0198, 0x391E0280)
        p.call("GetSharedLibrary")
        p.expect(0, "load two private callback-owner copies")
    p.emit(0x807E0180, 0x389E03C0, 0x38BE01A0, 0x38DE01A4)
    p.call("FindSymbol")
    p.expect(0, "find Register in the older private copy")
    p.emit(0x819E01A0, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
    p.expect(0, "register private callback after PIC sharing")
    p.emit(0x807E0180, 0x389E03D0, 0x38BE01A8, 0x38DE01A4)
    p.call("FindSymbol")
    p.expect(0, "find original callback transition vector")
    p.emit(0x387E018C)
    p.call("CloseConnection")
    p.expect(0, "close the newer private copy")
    p.emit(0x807E01A8)
    p.call("FragRemoveNotifyProc")
    p.expect(0, "closing a PIC peer preserves the older copy's notifier")
    close()
    p.emit(0x387E0360)
    p.call("DebugStr")
    # Callback arguments are connection ID, closure ID and event code. Filter
    # existing native dependencies; event zero identifies the new target.
    p.routines.append(
        (
            0x200,
            [
                0x2C050000,
                0x40820008,
                0x90620110,
                0x80C20110,
                0x7C033000,
                0x4C820020,
                0x54A6103A,
                0x38E20120,
                0x7D07302E,
                0x39080001,
                0x7D07312E,
                0x9082010C,
                0x4E800020,
            ],
        )
    )
    name = b"NotifyTarget"
    p.data[0x240 : 0x241 + len(name)] = bytes([len(name)]) + name
    marker = b"NATIVE_LIBRARIES_OK"
    p.data[0x360 : 0x361 + len(marker)] = bytes([len(marker)]) + marker
    for offset, text in ((0x3A0, b"NotifyOwner"), (0x3C0, b"Register"), (0x3D0, b"Callback")):
        p.data[offset : offset + 1 + len(text)] = bytes([len(text)]) + text
    p.finish(destination, "PPC Native Libraries Probe")
    library = pef(
        [0x38600000, 0x4E800020],
        struct.pack(">2I", 0, 0) + bytes(8),
        exports=[("Token", 1, 8, 1)],
        init=0,
        term=0,
        reloc=[0x4600],
    )
    write_fragment(destination, "NotifyTarget", library, 0)
    register = [
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x387E0008,
        0x819E0020,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x4E800020,
    ]
    owner_data = bytearray(64)
    struct.pack_into(">4I", owner_data, 0, 0, 0, len(register) * 4, 0)
    owner = pef(
        [*register, 0x4E800020],
        owner_data,
        imports=[("PrivateInterfaceLib", "FragAddNotifyProc", 2)],
        exports=[("Register", 2, 0, 1), ("Callback", 2, 8, 1)],
        reloc=[0x4601, 0xA000, 32, 0x4A00],
    )
    write_fragment(destination, "NotifyOwner", owner, 0)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
