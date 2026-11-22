#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_allocator_probe.py
#
#   Summary:   Original allocator-reference fixtures and optional real CFAllocator state checks.
#
#   Classes:   None declared.
#
#   Functions: write_allocator_reference, make_allocator_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original allocator-reference fixtures and optional real CFAllocator state checks."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def write_allocator_reference(destination, forward=False, mode="valid"):
    # An independent module layout deliberately differs from CarbonLib 1.4.
    # Its actual initializer and setter own the returned reference value.
    code = [
        0x80620028,
        0x4E800020,
        0x80620028,
        0x4E800020,
        0x2C030000,
        0x4182000C,
        0x38620060,
        0x48000008,
        0x38620040,
        0x90620028,
        0x38600000,
        0x4E800020,
        0x38620040,
        0x90620028,
        0x38600000,
        0x4E800020,
    ]
    if mode == "data-code":
        code[14:14] = [0x90620000]
    if mode == "unaligned":
        code[14:14] = [0x80620000, 0x38630001, 0x90620000]
    data = bytearray(128)
    struct.pack_into(">8I", data, 0, 0, 0, 8, 0, 16, 0, 48, 0)
    struct.pack_into(">II", data, 68, 0x200, 0x13579BDF)
    struct.pack_into(">II", data, 100, 0x200, 0x2468ACE0)
    names = ["CFAllocatorGetDefault", "CFAllocatorProbeToken", "CFAllocatorProbeSelect"]
    image = pef(
        code,
        data,
        exports=[(name, 1 if mode == "wrong-class" and i == 0 else 2, i * 8, 1) for i, name in enumerate(names)],
        init=24,
        reloc=[0x4603],
    )
    write_fragment(destination, "AllocatorBacking" if forward else "CoreFoundationLib", image, 0)
    if forward:
        image = pef(
            [0x38600000, 0x4E800020],
            bytes(8),
            imports=[("AllocatorBacking", name, 2) for name in names],
            exports=[(name, 2, i, -3) for i, name in enumerate(names)],
        )
        write_fragment(destination, "CoreFoundationLib", image, 0)


def make_allocator_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if mode not in (
        "valid",
        "missing",
        "wrong-class",
        "data-code",
        "unaligned",
        "preferred-invalid",
        "preferred-failure",
    ):
        raise ValueError(mode)
    if not real:
        write_carbon_fallback(destination)
        if mode != "missing":
            write_allocator_reference(destination, library == "CoreFoundationLib", mode)
    imports = [(library, "CFAllocatorGetDefault", 2)]
    if mode == "valid" or mode.startswith("preferred-"):
        imports += [(library, name, 2) for name in ("CFAllocatorGetContext", "CFAllocatorGetPreferredSizeForSize")]
    if mode == "preferred-failure":
        imports += [("InterfaceLib", "ROMAWCarbonAllocatorFailure", 2)]
    if real:
        imports += [
            (library, name, 2)
            for name in ("CFAllocatorCreate", "CFAllocatorSetDefault", "CFGetRetainCount", "CFRelease", "CFGetTypeID")
        ]
        imports += [
            ("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "GetGWorld")
        ]
    elif mode != "missing":
        imports += [("CoreFoundationLib", name, 2) for name in ("CFAllocatorProbeToken", "CFAllocatorProbeSelect")]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    if mode == "valid":
        p.routines.append(
            (
                704,
                [
                    0x3CC08000,
                    0x60C60001,
                    0x7C053000,
                    0x40820028,
                    0x3CC0FEDC,
                    0x60C6BA98,
                    0x7C043000,
                    0x40820018,
                    0x80C20384,
                    0x38C60001,
                    0x90C20384,
                    0x38630010,
                    0x4E800020,
                    0x3860FFFF,
                    0x4E800020,
                ],
            )
        )
        struct.pack_into(">I", p.data, 784, 0x80000001)
        p.emit(0x387E02C0, 0x907E032C)
        p.emit(0x387E0300, 0x389E0340)
        p.call("CFAllocatorGetContext")
        p.emit(0x807E0340)
        p.expect(0, "allocator context version")
        p.emit(0x807E0344)
        p.expect(0x80000001, "allocator context retains opaque info")
        p.emit(0x807E0360, 0x389E02C0)
        p.equal("allocator context retains the guest callback descriptor")
        for size, expected in ((5, 21), (0, 0), (-1, 0), (0x7FFFFFFF, 0x7FFFFFFF)):
            p.emit(0x387E0300)
            if size == 0x7FFFFFFF:
                p.emit(0x3C807FFF, 0x6084FFFF)
            else:
                p.emit(0x38800000 | (size & 0xFFFF))
            p.emit(0x3CA0FEDC, 0x60A5BA98)
            p.call("CFAllocatorGetPreferredSizeForSize")
            p.expect(expected, "preferred size callback and signed lower bound")
        p.emit(0x807E0384)
        p.expect(2, "nonpositive requests do not invoke preferred-size callback")
    if mode.startswith("preferred-"):
        if mode == "preferred-failure":
            callback = Probe(imports)
            callback.call("ROMAWCarbonAllocatorFailure")
            callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
            p.routines.append((704, callback.code))
        p.emit(0x387E02C0, 0x907E032C, 0x387E0300, 0x38800005, 0x38A00000)
        p.call("CFAllocatorGetPreferredSizeForSize")
        p.emit(0)
    elif mode != "valid":
        p.call("CFAllocatorGetDefault")
        p.emit(0)
    elif real:
        for offset, name in ((256, b"CoreFoundationLib"), (288, b"CFAllocatorGetDefault")):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "real allocator CFM connection")
        p.emit(0x807E0090, 0x389E0120, 0x38BE0098, 0x38DE009C)
        p.call("FindSymbol")
        p.expect(0, "real default allocator oracle")
        p.emit(0x887E009C)
        p.expect(2, "allocator oracle is a function descriptor")
        p.emit(0x807E0098, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("allocator oracle executes real PPC code")

        def original(slot=152):
            p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

        for name, offset, slot in (
            (b"CFAllocatorGetContext", 400, 176),
            (b"CFAllocatorGetPreferredSizeForSize", 440, 180),
        ):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x807E0090, 0x389E0000 | offset, 0x38BE0000 | slot, 0x38DE009C)
            p.call("FindSymbol")
            p.expect(0, "real allocator context/preferred oracle")
            p.emit(0x887E009C)
            p.expect(2, "context/preferred oracle is a function descriptor")
            p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("context/preferred oracle executes original PPC code")

        def expect_default(slot):
            original()
            p.emit(0x809E0000 | slot)
            p.equal("real default allocator state")
            p.call("CFAllocatorGetDefault")
            p.emit(0x809E0000 | slot)
            p.equal("native getter shares the live fallback allocator")

        original()
        p.nonzero("real system default allocator")
        p.emit(0x907E0080)
        p.call("CFGetTypeID")
        p.expect(2, "default reference is a real CFAllocator")
        expect_default(128)
        p.routines.append((640, [0x38600000, 0x4E800020]))
        struct.pack_into(">I", p.data, 324, 0x80000001)
        p.emit(0x387E02C0, 0x907E0160)
        p.emit(0x387E0280, 0x907E0154, 0x38600000, 0x389E0140)
        p.call("CFAllocatorCreate")
        p.nonzero("real custom allocator with original allocation callback")
        p.emit(0x907E0084)
        p.call("CFGetTypeID")
        p.expect(2, "custom reference is a real CFAllocator")
        p.emit(0x807E0084)
        p.call("CFAllocatorSetDefault")
        p.emit(0x807E0084)
        p.call("CFGetRetainCount")
        p.emit(0x907E00A8)
        for implicit in (False, True):
            p.emit(0x807E0084, 0x389E0200)
            original(176)
            p.emit(0x38600000 if implicit else 0x807E0084, 0x389E0230)
            p.call("CFAllocatorGetContext")
            for offset in range(0, 36, 4):
                p.emit(0x807E0200 + offset, 0x809E0230 + offset)
                p.equal("native context matches original allocator fields")
            for size in (0, -1, 7, 0x7FFFFFFF):
                for oracle in (True, False):
                    p.emit(0x38600000 if implicit and not oracle else 0x807E0084)
                    if size == 0x7FFFFFFF:
                        p.emit(0x3C807FFF, 0x6084FFFF)
                    else:
                        p.emit(0x38800000 | (size & 0xFFFF))
                    p.emit(0x3CA0FEDC, 0x60A5BA98)
                    if oracle:
                        original(180)
                        p.emit(0x907E00B8)
                    else:
                        p.call("CFAllocatorGetPreferredSizeForSize")
                        p.emit(0x809E00B8)
                        p.equal("native preferred size matches original signed callback clamp")
        for _ in range(8):
            expect_default(132)
        p.emit(0x38600000)
        p.call("CFAllocatorSetDefault")
        expect_default(132)
        p.emit(0x807E0084)
        p.call("CFAllocatorSetDefault")
        expect_default(132)
        p.emit(0x807E0084)
        p.call("CFGetRetainCount")
        p.emit(0x809E00A8)
        p.equal("native reads borrow allocator ownership")
        p.emit(0x807E0080)
        p.call("CFAllocatorSetDefault")
        expect_default(128)
        # Release only the caller's owned reference; references retained by
        # the real SetDefault implementation remain owned by that library.
        p.emit(0x807E0084)
        p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "allocator CFM reference closed")
        p.emit(0x387E0180, 0x389E0184)
        p.call("GetGWorld")
    else:
        for selector in (0, 1, 0):
            p.emit(0x38600000 | selector)
            p.call("CFAllocatorProbeSelect")
            p.call("CFAllocatorProbeToken")
            p.emit(0x907E0080)
            p.call("CFAllocatorGetDefault")
            p.emit(0x809E0080)
            p.equal("unknown layout uses its actual getter and state")
            p.emit(0x80630008)
            p.expect(0x2468ACE0 if selector else 0x13579BDF, "returned guest allocator reference")
        p.call("CarbonFallbackToken")
        p.expect(73, "allocator access keeps fallback initialization")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Allocator " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_allocator_probe(args.destination, args.library, args.real, args.mode)
