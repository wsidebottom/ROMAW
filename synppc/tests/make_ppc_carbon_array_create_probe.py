#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_array_create_probe.py
#
#   Summary:   Original immutable-array construction probes and real PPC differential tests.
#
#   Classes:   None declared.
#
#   Functions: make_array_create_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original immutable-array construction probes and real PPC differential tests."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_carbon_ownership_probe import write_ownership_reference


def make_array_create_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if mode not in (
        "valid",
        "retain-invalid",
        "retain-failure",
        "allocate-invalid",
        "allocate-failure",
        "init-invalid",
        "init-failure",
    ):
        raise ValueError(mode)
    if not real:
        write_carbon_fallback(destination)
        write_ownership_reference(destination, arrays=True)
    names = [
        "CFArrayCreate",
        "CFArrayGetTypeID",
        "CFArrayGetCount",
        "CFArrayGetValueAtIndex",
        "CFGetTypeID",
        "CFGetAllocator",
        "CFGetRetainCount",
        "CFRelease",
        "CFAllocatorGetDefault",
    ]
    imports = [(library, name, 2) for name in names]
    # The synthetic profile uses opaque test words and its own callback table.
    # Real type callbacks require valid CF objects.
    type_callbacks = "kCFTypeArrayCallBacks" if real else "CFOwnershipTypeCallbacks"
    imports += [("CoreFoundationLib", type_callbacks, 1)]
    if real:
        imports += [
            (library, name, 2)
            for name in (
                "CFDataCreate",
                "CFAllocatorCreate",
                "CFAllocatorAllocate",
                "CFAllocatorDeallocate",
                "CFAllocatorSetDefault",
            )
        ]
        imports += [
            ("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "GetGWorld")
        ]
    else:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFOwnershipSetResult",
                "CFOwnershipStat",
                "CFOwnershipSetInit",
                "CFOwnershipSetDefault",
                "CFOwnershipToken",
            )
        ]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    if mode.endswith("failure"):
        imports += [("InterfaceLib", "ROMAWCarbonArrayCreateFailure", 2)]
    p = Probe(imports)
    p.data.extend(bytes(1024))
    opaque = (0, 1, 0xDEADBEEF, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x12345678, 0xAAAAAAAA)
    struct.pack_into(">8I", p.data, 384, *opaque)
    struct.pack_into(">I", p.data, 560, 0x12345678)
    struct.pack_into(">I", p.data, 608, 0x12345678)

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 0xFFFF),
        )

    def original(slot):
        p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def epilogue(words):
        words += [0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020]

    def reset(offset, count=2):
        p.emit(0x38600000)
        for word in range(count):
            p.emit(0x907E0000 | (offset + word * 4))

    if not real:
        p.emit(0x387E0400)
        p.call("CFOwnershipSetResult")
    for field, descriptor in ((564, 672), (568, 688)):
        p.emit(0x387E0000 | descriptor, 0x907E0000 | field)
    if mode != "valid":
        descriptor = 720 if mode.startswith("init-") else 640 if mode.startswith("allocate-") else 672
        if mode.endswith("failure"):
            callback = Probe(imports)
            callback.call("ROMAWCarbonArrayCreateFailure")
            epilogue(callback.code)
            p.routines.append((descriptor, callback.code))
        if mode.startswith("allocate-"):
            struct.pack_into(">I", p.data, 452, 0x200)
            struct.pack_into(">I", p.data, 464, 0x80000001)
            p.emit(0x387E0280, 0x907E01E0, 0x387E01C0)
        else:
            if mode.startswith("init-"):
                p.emit(0x387E02D0)
                p.call("CFOwnershipSetInit")
            p.emit(0x38600000)
        p.emit(0x389E0180, 0x38A00003, 0x38DE0230)
        p.call("CFArrayCreate")
        p.emit(0)
    else:
        # Raw callback argument hashes distinguish the original allocator word
        # from the resolved allocation parent, and preserve tagged values.
        for descriptor, counter in ((672, 832), (688, 840)):
            words = [
                0x80A20000 | counter,
                0x38A50001,
                0x90A20000 | counter,
                0x80A20000 | (counter + 4),
                0x54A5083E,
                0x7CA51A78,
                0x7CA52278,
                0x90A20000 | (counter + 4),
            ]
            if descriptor == 672:
                words += [0x7C832378, 0x3CA05555, 0x60A55555, 0x7C632A78]
            words += [0x4E800020]
            p.routines.append((descriptor, words))
        type_index = next(i for i, entry in enumerate(imports) if entry[1] == type_callbacks)
        p.emit(0x807E0008 + type_index * 4)
        for offset in range(0, 20, 4):
            p.emit(0x80830000 | offset, 0x909E0000 | (584 + offset))
        integer(3, 0x12345678)
        p.emit(0x907E0248)

        if real:
            for offset, name in (
                (256, b"CoreFoundationLib"),
                (288, b"CFArrayCreate"),
                (320, b"CFArrayGetTypeID"),
                (352, b"CFRelease"),
                (1024, b"kCFTypeArrayCallBacks"),
            ):
                p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
            p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
            p.call("GetSharedLibrary")
            p.expect(0, "array construction oracle connection")
            for offset, slot in ((288, 152), (320, 156), (352, 160)):
                p.emit(0x807E0090, 0x389E0000 | offset, 0x38BE0000 | slot, 0x38DE00A4)
                p.call("FindSymbol")
                p.expect(0, "array construction/type/release oracle")
                p.emit(0x887E00A4)
                p.expect(2, "array oracle is a descriptor")
                p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
                p.nonzero("array oracle executes original PPC code")
            p.emit(0x807E0090, 0x389E0400, 0x38BE00A8, 0x38DE00A4)
            p.call("FindSymbol")
            p.expect(0, "saved type callbacks resolve through the oracle connection")
            p.emit(0x887E00A4)
            p.expect(1, "saved type callbacks are a data export")
        p.call("CFArrayGetTypeID")
        p.emit(0x907E0088)
        if real:
            original(156)
            p.emit(0x809E0088)
            p.equal("array type getter shares the real class ID")
        else:
            p.expect(7, "array class ID comes from the real fixture initializer")
        p.call("CFAllocatorGetDefault")
        p.emit(0x907E0080)
        if real:
            p.data[416:432] = bytes(range(16))
            for i in range(8):
                p.emit(0x38600000, 0x389E01A0, 0x38A00010)
                p.call("CFDataCreate")
                p.nonzero("actual CF values for type callbacks")
                p.emit(0x907E0000 | (448 + i * 4))
            for descriptor, name, counter in ((640, "CFAllocatorAllocate", 848), (656, "CFAllocatorDeallocate", 852)):
                callback = Probe(imports)
                callback.emit(0x80DE0000 | counter, 0x38C60001, 0x90DE0000 | counter)
                if descriptor == 640:
                    callback.emit(0x80DE0360, 0x2C060000)
                    failure = len(callback.code)
                    callback.emit(0x40820000)
                    callback.emit(0x7C852378, 0x7C641B78)
                else:
                    callback.emit(0x7C641B78)
                callback.emit(0x807E0080)
                callback.call(name)
                epilogue(callback.code)
                if descriptor == 640:
                    callback.code[failure] |= (len(callback.code) - failure) * 4
                    callback.emit(0x38600000)
                    epilogue(callback.code)
                p.routines.append((descriptor, callback.code))
            for field, descriptor in ((532, 640), (540, 656)):
                p.emit(0x387E0000 | descriptor, 0x907E0000 | field)
            struct.pack_into(">I", p.data, 516, 0xFEDCBA98)
            p.emit(0x38600000, 0x389E0200)
            p.call("CFAllocatorCreate")
            p.nonzero("actual custom allocator")
        else:
            p.emit(0x38600330)
            p.call("CFOwnershipStat")
        p.emit(0x907E0084)

        def arguments(owner, form, count, *, oracle=False):
            p.emit(0x38600000 if owner in (0, 3) else 0x807E0080 if owner == 1 else 0x807E0084)
            p.emit(0x389E01C0 if real and form in (2, 3) else 0x389E0180, 0x38A00000 | count)
            if form == 0:
                p.emit(0x38C00000)
            elif form == 2:
                # Each constructor recognizes its own canonical type callbacks.
                # The saved code treats the imported native descriptors as custom.
                p.emit(0x80DE00A8 if oracle else 0x80DE0008 + type_index * 4)
            else:
                p.emit(0x38DE0000 | (608 if form == 1 else 584 if form == 3 else 560))

        for owner in range(4):
            if owner == 3:
                p.emit(0x807E0084)
                p.call("CFAllocatorSetDefault" if real else "CFOwnershipSetDefault")
            for form in range(6):
                p.emit(0x38600000 if form == 5 else 0x387E02A0, 0x907E0234)
                for count in (0, 1, 3, 8):
                    if real:
                        p.emit(0x807E0084)
                        p.call("CFGetRetainCount")
                        p.emit(0x907E0378)
                    for oracle in (True, False) if real else (False,):
                        reset(832, 4)
                        arguments(owner, form, count, oracle=oracle)
                        if oracle:
                            original(152)
                        else:
                            p.call("CFArrayCreate")
                        p.nonzero("array construction succeeds")
                        p.emit(0x907E00B4 if oracle else 0x907E00B0)
                        if oracle:
                            for offset in (0, 4):
                                p.emit(0x807E0340 + offset, 0x907E0370 + offset)
                    if real:
                        for offset in (0, 4):
                            p.emit(0x807E0340 + offset, 0x809E0370 + offset)
                            p.equal("native/original retain callback counts and argument hashes match")
                        length = (32 if form >= 4 else 12) + count * 4
                        for offset in range(0, length, 4):
                            p.emit(0x807E00B0, 0x809E00B4, 0x80630000 | offset, 0x80840000 | offset)
                            p.equal("native immutable-array record matches original bytes")
                    p.emit(0x807E00B0)
                    p.call("CFGetTypeID")
                    p.emit(0x809E0088)
                    p.equal("array header uses its registered type")
                    p.emit(0x807E00B0)
                    p.call("CFArrayGetCount")
                    p.expect(count, "constructed count")
                    p.emit(0x807E00B0)
                    p.call("CFGetAllocator")
                    p.emit(0x809E0084 if owner >= 2 else 0x809E0080)
                    p.equal("constructed array records its resolved parent")
                    for i in range(count):
                        p.emit(0x807E00B0, 0x38800000 | i)
                        p.call("CFArrayGetValueAtIndex")
                        if real and form in (2, 3):
                            p.emit(0x809E0000 | (448 + i * 4))
                            p.equal("type callback retains the actual CF value")
                        else:
                            p.expect(opaque[i] ^ (0x55555555 if form == 4 else 0), "retained opaque value")
                    if real:
                        if owner >= 2:
                            p.emit(0x807E0084)
                            p.call("CFGetRetainCount")
                            p.emit(0x809E0378, 0x38840002)
                            p.equal("both constructed arrays own their custom-parent references")
                        if form in (2, 3):
                            for i in range(8):
                                p.emit(0x807E0000 | (448 + i * 4))
                                p.call("CFGetRetainCount")
                                p.expect(3 if i < count else 1, "both arrays own each retained CF value")
                        for oracle in (True, False):
                            reset(840)
                            p.emit(0x807E00B0 if oracle else 0x807E00B4)
                            if oracle:
                                original(160)
                                for offset in (0, 4):
                                    p.emit(0x807E0348 + offset, 0x907E0380 + offset)
                            else:
                                p.call("CFRelease")
                                for offset in (0, 4):
                                    p.emit(0x807E0348 + offset, 0x809E0380 + offset)
                                    p.equal("reciprocal array release callback records agree")
                        p.emit(0x807E0084)
                        p.call("CFGetRetainCount")
                        p.emit(0x809E0378)
                        p.equal("array lifecycle preserves caller/default parent ownership")
                        if form in (2, 3):
                            for i in range(8):
                                p.emit(0x807E0000 | (448 + i * 4))
                                p.call("CFGetRetainCount")
                                p.expect(1, "array lifecycle balances CF value ownership")
                    else:
                        p.emit(0x807E00B0)
                        p.call("CFRelease")
            if owner == 3:
                p.emit(0x807E0080 if real else 0x38600000)
                p.call("CFAllocatorSetDefault" if real else "CFOwnershipSetDefault")
        if real:
            # Original 1.4's outer constructor dereferences the null result of
            # failed instance allocation. Check the native API's bounded null
            # return without reproducing that low-memory write in the oracle.
            p.emit(0x38600001, 0x907E0360)
            reset(832, 2)
            arguments(2, 4, 3)
            p.call("CFArrayCreate")
            p.expect(0, "allocation failure returns null before retaining elements")
            p.emit(0x807E0340)
            p.expect(0, "failed allocation invokes no value callbacks")
            for i in range(8):
                p.emit(0x807E0000 | (448 + i * 4))
                p.call("CFRelease")
            p.emit(0x807E0084)
            p.call("CFRelease")
            p.emit(0x387E0090)
            p.call("CloseConnection")
            p.expect(0, "array oracle connection released")
            p.emit(0x387E00D0, 0x389E00D4)
            p.call("GetGWorld")
        else:
            p.call("CFOwnershipToken")
            p.expect(73, "original fixture initializer executed")
            p.call("CarbonFallbackToken")
            p.expect(73, "array creation preserves initialized PEF fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Array New " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_array_create_probe(args.destination, args.library, args.real, args.mode)
