#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_creation_probe.py
#
#   Summary:   Original allocator-class fixtures and real constructor/release differential tests.
#
#   Classes:   None declared.
#
#   Functions: write_creation_reference, make_creation_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original allocator-class fixtures and real constructor/release differential tests."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def write_creation_reference(destination):
    # Original instructions in a deliberately sparse module with the 1.4
    # section extents. Its initializer owns the class/default words. Exported
    # Create returns null and GetTypeID returns 9, so delegation cannot pass
    # the native construction/class-7 tests. No Apple code is included.
    code = [0] * (0x5DEE0 // 4)
    code[:2] = [0x38600049, 0x4E800020]
    code[2:15] = [
        0x38600007,
        0x9062089C,
        0x38600200,
        0x90620A68,
        0x38600000,
        0x90620A6C,
        0x90626F28,
        0x38620100,
        0x90623164,
        0x38620040,
        0x90620120,
        0x38600000,
        0x4E800020,
    ]
    code[16:18] = [0x38620200, 0x4E800020]
    for offset, words in (
        (0x1733C, [0x38600009, 0x4E800020]),
        (0x17344, [0x80623164, 0x4E800020]),
        (0x173F4, [0x38600000, 0x4E800020]),
    ):
        code[offset // 4 : offset // 4 + len(words)] = words
    data = bytearray(0x72A8)
    struct.pack_into(">10I", data, 0, 0x173F4, 0, 0x1733C, 0, 0x17344, 0, 0, 0, 8, 0)
    struct.pack_into(">2I", data, 64, 64, 0)
    struct.pack_into(">I", data, 272, 0xFEDCBA98)
    names = ["CFAllocatorCreate", "CFAllocatorGetTypeID", "CFAllocatorGetDefault", "CFCreateFallbackToken"]
    image = pef(
        code,
        data,
        exports=[(name, 2, i * 8, 1) for i, name in enumerate(names)],
        init=32,
        reloc=[0x4604, 0xA000, 64, 0x4600],
    )
    write_fragment(destination, "CoreFoundationLib", image, 0)


def make_creation_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if mode not in ("valid", "retain-invalid", "retain-failure", "allocate-invalid", "allocate-failure"):
        raise ValueError(mode)
    if not real:
        write_carbon_fallback(destination)
        write_creation_reference(destination)
    imports = [
        (library, name, 2)
        for name in ("CFAllocatorCreate", "CFAllocatorGetTypeID", "CFAllocatorGetDefault", "CFGetTypeID")
    ]
    if real:
        imports += [
            (library, name, 2)
            for name in ("CFAllocatorAllocate", "CFAllocatorDeallocate", "CFRelease", "CFGetRetainCount")
        ]
        imports += [
            ("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "GetGWorld")
        ]
    else:
        imports += [("CoreFoundationLib", "CFCreateFallbackToken", 2), ("CarbonLib", "CarbonFallbackToken", 2)]
    if mode.endswith("failure"):
        imports += [("InterfaceLib", "ROMAWCarbonCreationFailure", 2)]
    p = Probe(imports)

    def event(words, number, counter):
        words += [
            0x80C20000 | counter,
            0x38C60001,
            0x90C20000 | counter,
            0x80C2035C,
            0x54C62036,
            0x60C60000 | number,
            0x90C2035C,
        ]

    def epilogue(words):
        words += [0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020]

    # Context is 36 bytes at 448; callback descriptors are distinct from it.
    struct.pack_into(">I", p.data, 452, 0x80000001)
    for field, descriptor in ((456, 640), (460, 688), (468, 656), (476, 672)):
        p.emit(0x387E0000 | descriptor, 0x907E0000 | field)
    if mode != "valid":
        which = 640 if mode.startswith("retain-") else 656
        if mode.startswith("allocate-"):
            p.emit(0x38600000, 0x907E01C8)
        if mode.endswith("failure"):
            callback = Probe(imports)
            callback.call("ROMAWCarbonCreationFailure")
            epilogue(callback.code)
            p.routines.append((which, callback.code))
        p.emit(0x38600221, 0x389E01C0)
        p.call("CFAllocatorCreate")
        p.emit(0)
    else:
        retain = [0x90620344]
        event(retain, 2, 832)
        retain += [0x3C608000, 0x60630002, 0x4E800020]
        release = [0x90620354]
        event(release, 4, 848)
        release += [0x4E800020]
        p.routines += [(640, retain), (688, release)]
        if real:
            allocate = Probe(imports)
            event(allocate.code, 1, 840)
            allocate.emit(0x80DE0360, 0x2C060000)
            failure = len(allocate.code)
            allocate.emit(0x40820000)
            allocate.emit(0x7C852378, 0x7C641B78, 0x807E0080)
            allocate.call("CFAllocatorAllocate")
            epilogue(allocate.code)
            allocate.code[failure] |= (len(allocate.code) - failure) * 4
            allocate.emit(0x38600000)
            epilogue(allocate.code)
            deallocate = Probe(imports)
            event(deallocate.code, 3, 844)
            deallocate.emit(0x7C641B78, 0x807E0080)
            deallocate.call("CFAllocatorDeallocate")
            epilogue(deallocate.code)
            p.routines += [(656, allocate.code), (672, deallocate.code)]
            for offset, name in (
                (256, b"CoreFoundationLib"),
                (288, b"CFAllocatorCreate"),
                (320, b"CFAllocatorGetTypeID"),
                (360, b"CFRelease"),
            ):
                p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
            p.call("GetSharedLibrary")
            p.expect(0, "constructor oracle CFM connection")
            for name, slot in ((288, 152), (320, 156), (360, 180)):
                p.emit(0x807E0090, 0x389E0000 | name, 0x38BE0000 | slot, 0x38DE00A0)
                p.call("FindSymbol")
                p.expect(0, "constructor/type oracle export")
                p.emit(0x887E00A0)
                p.expect(2, "constructor/type oracle is a descriptor")
                p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
                p.nonzero("constructor/type oracle executes original PPC code")
        else:
            allocate = []
            event(allocate, 1, 840)
            allocate += [0x386203A0, 0x4E800020]
            p.routines.append((656, allocate))

        def original(slot):
            p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

        p.call("CFAllocatorGetDefault")
        p.nonzero("live parent allocator")
        p.emit(0x907E0080)
        p.call("CFAllocatorGetTypeID")
        p.emit(0x907E0084)
        if real:
            original(156)
            p.emit(0x809E0084)
            p.equal("native allocator type matches the live class registry")
        else:
            p.expect(7, "native allocator type reads initialized class state instead of the original getter")
        for owner in (0, 1, 2):
            p.emit(0x38600000)
            for offset in range(832, 864, 4):
                p.emit(0x907E0000 | offset)
            for oracle in (True, False) if real else (False,):
                p.emit(0x38600000 if owner == 0 else 0x807E0080 if owner == 1 else 0x38600221, 0x389E01C0)
                if oracle:
                    original(152)
                else:
                    p.call("CFAllocatorCreate")
                p.nonzero("allocator constructor creates a real object")
                p.emit(0x907E00A4 if oracle else 0x907E00A8)
                p.call("CFGetTypeID")
                p.emit(0x809E0084)
                p.equal("created allocator uses the initialized class type")
                p.emit(0x807E00A4 if oracle else 0x807E00A8, 0x80630008)
                p.emit(0x38800221 if owner == 2 else 0x809E0080)
                p.equal("created allocator records its resolved parent or UseContext sentinel")
            if real:
                for offset in range(0, 48, 4):
                    p.emit(0x807E00A4, 0x809E00A8, 0x80630000 | offset, 0x80840000 | offset)
                    p.equal("native allocator record matches the original constructor")
                for slot in (164, 168):
                    p.emit(0x807E0000 | slot)
                    p.call("CFGetRetainCount")
                    p.expect(1, "constructor returns exactly one owned reference")
                    p.emit(0x807E0000 | slot)
                    if slot == 168:
                        original(180)
                    else:
                        p.call("CFRelease")
                p.emit(0x807E0350)
                p.expect(2, "real CFRelease releases each retained info reference")
                p.emit(0x807E034C)
                p.expect(2 if owner == 2 else 0, "UseContext storage is disposed by its own callback")
                p.emit(0x807E0354)
                p.expect(0x80000002, "release receives the retained opaque info word")
                p.emit(0x807E035C)
                p.expect(
                    0x21213434 if owner == 2 else 0x2244, "constructor/destructor callback order matches reference"
                )
            p.emit(0x807E0340)
            p.expect(2 if real else 1, "one retain callback per successful constructor")
            p.emit(0x807E0344)
            p.expect(0x80000001, "retain receives the original opaque info word")
            p.emit(0x807E0348)
            p.expect((2 if real else 1) if owner == 2 else 0, "UseContext allocates through the input context")
        if real:
            p.emit(0x38600000)
            for offset in range(832, 864, 4):
                p.emit(0x907E0000 | offset)
            p.emit(0x38600001, 0x907E0360)
            for oracle in (True, False):
                p.emit(0x38600221, 0x389E01C0)
                if oracle:
                    original(152)
                else:
                    p.call("CFAllocatorCreate")
                p.expect(0, "failed context allocation returns null")
            p.emit(0x807E0340)
            p.expect(2, "both failed allocations retain info before requesting storage")
            p.emit(0x807E0350)
            p.expect(0, "CarbonLib 1.4 does not release retained info after allocation failure")
            p.emit(0x387E0090)
            p.call("CloseConnection")
            p.expect(0, "constructor oracle connection released")
            p.emit(0x387E00B0, 0x389E00B4)
            p.call("GetGWorld")
        else:
            p.call("CFCreateFallbackToken")
            p.expect(73, "original class initializer executed")
            p.call("CarbonFallbackToken")
            p.expect(73, "constructor preserves initialized fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Create " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_creation_probe(args.destination, args.library, args.real, args.mode)
