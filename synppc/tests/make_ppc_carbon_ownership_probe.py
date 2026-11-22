#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_ownership_probe.py
#
#   Summary:   Original ownership fixtures and differential tests on real CF objects.
#
#   Classes:   None declared.
#
#   Functions: write_ownership_reference, make_ownership_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original ownership fixtures and differential tests on real CF objects."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAMES = ("CFRetain", "CFRelease", "CFGetRetainCount", "CFGetAllocator")
OFFSETS = (0x533E4, 0x53488, 0x535F0, 0x53844)


def write_ownership_reference(destination, arrays=False):
    # Original sparse code with the recognized section shape. Public bodies
    # deliberately disagree with the native implementations. The PEF's own
    # initializer publishes its class, default allocator and single-key map.
    code = [0] * (0x5DEE0 // 4)
    bodies = {
        0: [0x38600049, 0x4E800020],
        8: [0x90820308, 0x80620304, 0x4E800020],
        24: [0x90820308, 0x90A20304, 0x4E800020],
        40: [0x38600000, 0x90620304, 0x90820308, 0x4E800020],
        64: [0x80A20310, 0x38A50001, 0x90A20310, 0x90620314, 0x90820318, 0x4E800020],
        96: [0x90620410, 0x4E800020],
        104: [0x7C62182E, 0x4E800020],
        112: [
            0x38600000,
            0x90620310,
            0x90620308,
            0x90620304,
            0x3C600001,
            0x60630200,
            0x90620104,
            0x38600000,
            0x4E800020,
        ],
        160: [
            0x38620100,
            0x90623164,
            0x38620300,
            0x90620A64,
            0x38620400,
            0x906265E0,
            0x38620040,
            0x90620128,
            0x38600000,
            0x4E800020,
        ],
        208: [0x90620128, 0x4E800020],
        OFFSETS[0]: [0x38600000, 0x4E800020],
        OFFSETS[1]: [0x4E800020],
        OFFSETS[2]: [0x386003E7, 0x4E800020],
        OFFSETS[3]: [0x38600000, 0x4E800020],
    }
    data = bytearray(0x72A8)
    for i, offset in enumerate((*OFFSETS, 8, 24, 40)):
        struct.pack_into(">II", data, i * 8, offset, 0)
    for i, offset in enumerate((64, 160, 96, 104, 112, 0, 208)):
        struct.pack_into(">II", data, 64 + i * 8, offset, 0)
    struct.pack_into(">I", data, 260, 0x10200)
    struct.pack_into(">I", data, 272, 0xFEDCBA98)
    exports = [
        (name, 2, i * 8, 1)
        for i, name in enumerate((*NAMES, "CFDictionaryGetValue", "CFDictionarySetValue", "CFDictionaryRemoveValue"))
    ]
    exports += [
        (name, 2, slot, 1)
        for name, slot in (
            ("CFOwnershipSetFinalize", 80),
            ("CFOwnershipStat", 88),
            ("CFOwnershipReset", 96),
            ("CFOwnershipToken", 104),
            ("CFOwnershipSetFree", 112),
        )
    ]
    reloc = [0x4606, 0xA000, 64, 0x4606]
    if arrays:
        bodies.update(
            {
                224: bodies[160][:-2]
                + [
                    0x38620078,
                    0x90620120,
                    0x90620220,
                    0x38620040,
                    0x90620228,
                    0x38620098,
                    0x90623238,
                    0x386200A0,
                    0x9062323C,
                    0x386200A8,
                    0x90623240,
                    0x386200B0,
                    0x90623244,
                    0x38600007,
                    0x906208C0,
                    0x38600100,
                    0x90620A68,
                    0x38600000,
                    0x90620A6C,
                    0x38620200,
                    0x90620330,
                    0x38600000,
                    0x4E800020,
                ],
                400: [0x90620324, 0x90820328, 0x80A2032C, 0x38A50001, 0x90A2032C, 0x80620320, 0x4E800020],
                440: [0x90620320, 0x4E800020],
                448: [0x7C832378, 0x4E800020],
                456: [0x4E800020],
                460: [0x38600000, 0x4E800020],
                472: [0x7C632278, 0x7C630034, 0x5463D97E, 0x4E800020],
                496: [0x90620408, 0x4E800020],
                504: [0x90626F28, 0x4E800020],
                0x17344: [0x80623164, 0x4E800020],
                0x1B5C4: [0x38600009, 0x4E800020],
                0x1B8B4: [0x38600000, 0x4E800020],
            }
        )
        struct.pack_into(">I", data, 72, 224)
        struct.pack_into(">I", data, 516, 0x200)
        struct.pack_into(">I", data, 528, 0xDEADBEEF)
        extra = (
            (None, 400),
            ("CFArrayCreate", 0x1B8B4),
            ("CFArrayGetTypeID", 0x1B5C4),
            ("CFOwnershipSetResult", 440),
            (None, 448),
            (None, 456),
            (None, 460),
            (None, 472),
            ("CFOwnershipSetInit", 496),
            ("CFAllocatorGetDefault", 0x17344),
            ("CFOwnershipSetDefault", 504),
        )
        for i, (name, offset) in enumerate(extra):
            slot = 120 + i * 8
            struct.pack_into(">II", data, slot, offset, 0)
            if name:
                exports.append((name, 2, slot, 1))
        exports.append(("CFOwnershipTypeCallbacks", 1, 0x3234, 1))
        reloc += [0xA000, 120, 0x460A]
    for offset, words in bodies.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    image = pef(code, data, exports=exports, init=72, reloc=reloc)
    write_fragment(destination, "CoreFoundationLib", image, 0)


def make_ownership_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if mode not in ("valid", "finalize-invalid", "finalize-failure", "free-invalid", "free-failure"):
        raise ValueError(mode)
    if not real:
        write_carbon_fallback(destination)
        write_ownership_reference(destination, arrays=mode == "valid")
    imports = [(library, name, 2) for name in NAMES]
    if real:
        imports += [
            (library, name, 2)
            for name in ("CFDataCreate", "CFAllocatorCreate", "CFAllocatorAllocate", "CFAllocatorDeallocate")
        ]
        imports += [
            ("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "GetGWorld")
        ]
    else:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFOwnershipSetFinalize",
                "CFOwnershipStat",
                "CFOwnershipReset",
                "CFOwnershipToken",
                "CFOwnershipSetFree",
            )
        ]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    if mode.endswith("failure"):
        imports += [("InterfaceLib", "ROMAWCarbonOwnershipFailure", 2)]
    p = Probe(imports)

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 0xFFFF),
        )

    def load(register=3, oracle=False):
        p.emit(
            (0x80000000 | (register << 21) | (30 << 16) | (180 if oracle else 176))
            if real
            else (0x38000000 | (register << 21) | (30 << 16) | 388)
        )

    def original(operation):
        p.emit(0x819E0098 + operation * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def checkpoint(retains):
        load()
        p.call("CFGetRetainCount")
        p.expect(retains + 1, "native count matches the requested number of retained references")
        if real:
            for oracle in (False, True):
                load(oracle=oracle)
                original(2)
                p.expect(retains + 1, "original getter validates native and original overflow state")
            load(3)
            load(4, True)
            p.emit(0x80630004, 0x80840004)
            p.equal("native/original header count and flags agree")

    def walk(operation, amount):
        integer(31, amount)
        start = len(p.code)
        load()
        p.call(NAMES[operation])
        if real:
            load(oracle=True)
            original(operation)
        p.emit(0x3BFFFFFF, 0x2C1F0000)
        p.emit(0x40820000 | (((start - len(p.code)) * 4) & 0xFFFF))

    if not real:
        struct.pack_into(">I", p.data, 392, 0x780)
    if mode != "valid":
        if mode.endswith("failure"):
            callback = Probe(imports)
            callback.call("ROMAWCarbonOwnershipFailure")
            callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020)
            p.routines.append((672, callback.code))
        p.emit(0x387E02A0)
        p.call("CFOwnershipSetFinalize" if mode.startswith("finalize-") else "CFOwnershipSetFree")
        load()
        p.call("CFRelease")
        p.emit(0)
    else:
        if real:
            for offset, name in (
                (256, b"CoreFoundationLib"),
                *[(288 + i * 32, name.encode()) for i, name in enumerate(NAMES)],
            ):
                p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
            p.call("GetSharedLibrary")
            p.expect(0, "ownership oracle CFM connection")
            for i in range(4):
                p.emit(0x807E0090, 0x389E0000 | (288 + i * 32), 0x38BE0098 + i * 4, 0x38DE00A8)
                p.call("FindSymbol")
                p.expect(0, "original ownership export")
                p.emit(0x887E00A8)
                p.expect(2, "ownership oracle is a descriptor")
                p.emit(0x807E0098 + i * 4, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
                p.nonzero("ownership oracle executes original PPC code")
            p.data[448:464] = bytes(range(16))
            for slot in (176, 180):
                p.emit(0x38600000, 0x389E01C0, 0x38A00010)
                p.call("CFDataCreate")
                p.nonzero("actual data object for ownership testing")
                p.emit(0x907E0000 | slot)
        load()
        p.call("CFGetAllocator")
        p.nonzero("borrowed system allocator")
        p.emit(0x907E0080)
        if real:
            load()
            original(3)
            p.emit(0x809E0080)
            p.equal("native/original allocator identity")
        checkpoint(0)
        previous = 0
        for retains in (1, 32766, 32767, 32768, 65534, 65535, 65536, 65540):
            walk(0, retains - previous)
            checkpoint(retains)
            previous = retains
        for retains in (65536, 65535, 65534, 32768, 32767, 32766, 1, 0):
            walk(1, previous - retains)
            checkpoint(retains)
            previous = retains
        if real:
            load()
            p.call("CFRelease")
            load(oracle=True)
            original(1)
            # A custom allocator tests prefixed generic objects, parent retain
            # ownership and reentrant disposal through native storage callbacks.
            for name, slot, counter in (("CFAllocatorAllocate", 640, 832), ("CFAllocatorDeallocate", 656, 836)):
                callback = Probe(imports)
                callback.emit(0x80DE0000 | counter, 0x38C60001, 0x90DE0000 | counter)
                if slot == 640:
                    callback.emit(0x7C852378, 0x7C641B78)
                else:
                    callback.emit(0x7C641B78)
                callback.emit(0x807E0080)
                callback.call(name)
                callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020)
                p.routines.append((slot, callback.code))
            for field, slot in ((532, 640), (540, 656)):
                p.emit(0x387E0000 | slot, 0x907E0000 | field)
            struct.pack_into(">I", p.data, 516, 0x80000001)
            p.emit(0x38600000, 0x389E0200)
            p.call("CFAllocatorCreate")
            p.nonzero("custom parent for prefixed data")
            p.emit(0x907E00B8)
            for slot in (176, 180):
                p.emit(0x807E00B8, 0x389E01C0, 0x38A00010)
                p.call("CFDataCreate")
                p.nonzero("data allocated by custom parent")
                p.emit(0x907E0000 | slot)
                p.call("CFGetAllocator")
                p.emit(0x809E00B8)
                p.equal("generic custom allocation returns the actual parent")
            p.emit(0x807E00B8)
            p.call("CFGetRetainCount")
            p.expect(3, "generic object creation owns its parent references")
            for oracle, expected in ((False, 2), (True, 1)):
                load(oracle=oracle)
                if oracle:
                    original(1)
                else:
                    p.call("CFRelease")
                p.emit(0x807E00B8)
                p.call("CFGetRetainCount")
                p.expect(expected, "generic destruction releases its parent reference")
            p.emit(0x807E0340, 0x809E0344)
            p.equal("custom storage allocations and deallocations balance")
            p.emit(0x807E00B8)
            p.call("CFRelease")
            p.emit(0x387E0090)
            p.call("CloseConnection")
            p.expect(0, "ownership oracle connection released")
            p.emit(0x387E00C0, 0x389E00C4)
            p.call("GetGWorld")
        else:
            p.emit(0x38600304)
            p.call("CFOwnershipStat")
            p.expect(0, "overflow dictionary entry was removed")
            p.emit(0x38600308)
            p.call("CFOwnershipStat")
            load(4)
            p.equal("overflow dictionary callbacks receive the raw guest object key")
            p.routines.append((672, [0x90620344, 0x80820340, 0x38840001, 0x90820340, 0x4E800020]))
            p.emit(0x387E02A0)
            p.call("CFOwnershipSetFinalize")
            p.emit(0x807E0080, 0x907E0180)
            for system in (False, True):
                p.call("CFOwnershipReset")
                p.emit(0x38600780 if system else 0x38600700, 0x907E0188)
                load()
                p.call("CFGetAllocator")
                p.emit(0x809E0080)
                p.equal("system and prefixed allocator lookup")
                load()
                p.call("CFRelease")
                p.emit(0x38600310)
                p.call("CFOwnershipStat")
                p.expect(1, "one storage deallocation per destroyed object")
                p.emit(0x38600314)
                p.call("CFOwnershipStat")
                p.emit(0x389E0184 if system else 0x389E0180)
                p.equal("destruction frees the object or its allocation prefix")
            p.emit(0x807E0340)
            p.expect(2, "both class finalizers ran")
            p.call("CFOwnershipToken")
            p.expect(73, "ownership reference initializer executed")
            p.call("CarbonFallbackToken")
            p.expect(73, "ownership preserves initialized fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Own " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_ownership_probe(args.destination, args.library, args.real, args.mode)
