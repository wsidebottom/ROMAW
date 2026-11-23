#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_native_libraries_probe.py
#
#   Summary:   Original SOM allocation, CFM lifecycle and Icon Services name/ownership checks.
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
    imports.append(("PrivateInterfaceLib", "FragRelease", 2))
    imports += [("InterfaceLib", name, 2) for name in ("PtrToHand", "DisposeHandle")]
    imports += [
        ("IconServicesLib", name, 2)
        for name in (
            "GetIconRefByName",
            "CompositeIconRef",
            "GetIconRef",
            "GetIconRefOwners",
            "ReleaseIconRef",
            "IsValidIconRef",
            "RegisterIconRefFromIconFamily",
            "UnregisterIconRef",
        )
    ]
    p = Probe(imports)
    p.data.extend(bytes(1536 - len(p.data)))

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

    for normal_term in (0, 1):
        load()
        p.emit(0x807E0180, 0x389E03E0, 0x38BE01B0, 0x38DE01B4)
        p.call("FindSymbol")
        p.expect(0, "find the target's real termination-counter export")
        p.emit(0x809E01B0, 0x381E03F0, 0x90040000, 0x38000000, 0x901E03F0)
        p.emit(0x38600001, 0x809E0180, 0x38A00001)
        p.call("FragRelease")
        p.expect(-2800, "private release rejects an unknown context without closing the connection")
        p.emit(0x38600000, 0x809E0180, 0x38A00000 | normal_term)
        p.call("FragRelease")
        p.expect(0, "private release closes the real connection by value")
        p.emit(0x807E03F0)
        p.expect(normal_term, "normal release invokes the actual terminator; abnormal release omits it")
        p.emit(0x387E0180)
        p.call("CloseConnection")
        p.expect(-2801, "private release leaves a stale connection invalid")

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
    # Private name lookup must share actual public IconRef identity and retain
    # counts, including custom families whose caller-owned Handle is disposed.
    p.emit(0x3C606D61, 0x60636373, 0x3C80646F, 0x60846375, 0x38BE0480)
    p.call("GetIconRefByName")
    p.expect(0, "lookup the native generic document icon")
    p.emit(0x38608000, 0x3C806D61, 0x60846373, 0x3CA0646F, 0x60A56375, 0x38DE0484)
    p.call("GetIconRef")
    p.expect(0, "public lookup resolves the same document icon")
    p.emit(0x807E0480, 0x809E0484)
    p.equal("public and private lookups share icon identity")
    p.emit(0x3C60A5A5, 0x6063A5A5, 0x907E0494)
    p.emit(0x807E0480, 0x389E0495)
    p.call("GetIconRefOwners")
    p.expect(0, "read native icon ownership count")
    p.emit(0xA07E0495)
    p.expect(2, "both lookup references are retained")
    for offset in (0x494, 0x497):
        p.emit(0x887E0000 | offset)
        p.expect(0xA5, "owner count output preserves adjacent bytes")
    for slot in (0x480, 0x484):
        p.emit(0x807E0000 | slot)
        p.call("ReleaseIconRef")
        p.expect(0, "release one lookup reference")

    family = b"icns" + struct.pack(">I", 80) + b"ics#" + struct.pack(">I", 72) + bytes([0xAA, 0x55] * 16 + [255] * 32)
    p.data[0x400:0x450] = family
    p.emit(0x387E0400, 0x389E0490, 0x38A00050)
    p.call("PtrToHand")
    p.expect(0, "create original caller-owned icon family")
    p.emit(0x3C606E70, 0x60636963, 0x3C804465, 0x60846D6F, 0x80BE0490, 0x38DE0488)
    p.call("RegisterIconRefFromIconFamily")
    p.expect(0, "register original custom icon family")
    p.emit(0x807E0490)
    p.call("DisposeHandle")
    p.emit(0x3C606E70, 0x60636963, 0x3C804465, 0x60846D6F, 0x38BE048C)
    p.call("GetIconRefByName")
    p.expect(0, "name lookup retains its owned family copy")
    p.emit(0x807E0488, 0x809E048C)
    p.equal("custom registration and name lookup share identity")
    p.emit(0x807E0488, 0x809E048C, 0x38BE04A0)
    p.call("CompositeIconRef")
    p.expect(-4, "unavailable compositor reports unimpErr without fake success")
    p.emit(0x807E04A0)
    p.expect(0, "unavailable composition clears output")
    p.emit(0x807E0488, 0x389E0495)
    p.call("GetIconRefOwners")
    p.expect(0, "read owners after failed composition")
    p.emit(0xA07E0495)
    p.expect(2, "failed composition does not retain input references")
    for foreground in (False, True):
        p.emit(0x38600000 if foreground else 0x807E0488, 0x809E048C if foreground else 0x38800000, 0x38BE04A0)
        p.call("CompositeIconRef")
        p.expect(-2580, "composition rejects a missing input icon")
    p.emit(0x807E0488, 0x809E048C, 0x38A00000)
    p.call("CompositeIconRef")
    p.expect(-50, "composition requires a valid output pointer")
    p.emit(0x3C606E70, 0x60636963, 0x3C804465, 0x60846D6F)
    p.call("UnregisterIconRef")
    p.expect(0, "unregister custom name without disposing callers' references")
    p.emit(0x3C606E70, 0x60636963, 0x3C804465, 0x60846D6F, 0x38BE0480)
    p.call("GetIconRefByName")
    p.expect(-2581, "unregistered name reports noSuchIconErr")
    p.emit(0x807E0480)
    p.expect(0, "failed name lookup clears its output")
    p.emit(0x807E0488)
    p.call("IsValidIconRef")
    p.expect(1, "unregistration preserves retained references")
    for slot in (0x488, 0x48C):
        p.emit(0x807E0000 | slot)
        p.call("ReleaseIconRef")
        p.expect(0, "release custom reference")
    p.emit(0x807E0488)
    p.call("IsValidIconRef")
    p.expect(0, "last release disposes an unregistered icon")
    p.emit(0x3C606D61, 0x60636373, 0x3C80646F, 0x60846375, 0x38A00000)
    p.call("GetIconRefByName")
    p.expect(-50, "name lookup requires an output pointer")
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
    p.data[0x3E0:0x3E6] = b"\x05Token"
    p.finish(destination, "PPC Native Libraries Probe")
    library = pef(
        [
            0x38600000,
            0x4E800020,
            0x80820010,
            0x2C040000,
            0x41820010,
            0x80640000,
            0x38630001,
            0x90640000,
            0x38600000,
            0x4E800020,
        ],
        struct.pack(">4I", 0, 0, 8, 0) + bytes(8),
        exports=[("Token", 1, 16, 1)],
        init=0,
        term=8,
        reloc=[0x4601],
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
