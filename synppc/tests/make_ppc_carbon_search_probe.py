#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_search_probe.py
#
#   Summary:   Original CFArray search ABI probes and optional real-PEF differential checks.
#
#   Classes:   None declared.
#
#   Functions: make_search_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CFArray search ABI probes and optional real-PEF differential checks."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe, write_cf_sentinel
from make_ppc_carbon_native_probe import write_carbon_fallback

COMPARATOR = [
    0x80C202D0,
    0x38C60001,
    0x90C202D0,
    0x80C202D4,
    0x1CC60021,
    0x7CC61A78,
    0x1CC60021,
    0x7CC62278,
    0x90C202D4,
    0x3CC58000,
    0x54C6003C,
    0x2C060002,
    0x41820008,
    0,
    0x70A60001,
    0x4182000C,
    0x7C041840,
    0x48000008,
    0x7C032040,
    0x38600000,
    0x4D820020,
    0x3860FFF9,
    0x4D800020,
    0x38600009,
    0x4E800020,
]


def make_search_probe(destination, library="CarbonLib", real=False):
    if not real:
        write_carbon_fallback(destination)
        write_cf_sentinel(destination)
    imports = [(library, "CFArrayBSearchValues", 2)]
    if real:
        imports += [
            (library, name, 2) for name in ("CFArrayCreate", "CFArrayCreateMutable", "CFArrayAppendValue", "CFRelease")
        ]
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [("CarbonLib", "CarbonFallbackToken", 2), ("CoreFoundationLib", "CFFallbackToken", 2)]
    p = Probe(imports)
    if real:
        p.data.extend(bytes(4096 - len(p.data)))
    p.routines.append((640, COMPARATOR))

    def word(register, value):
        p.emit(0x3C000000 | register << 21 | value >> 16, 0x60000000 | register << 21 | register << 16 | value & 65535)

    def clear_trace():
        p.emit(0x38600000, 0x907E02D0, 0x907E02D4)

    def search(slot, start, length, value, direction=0, storage=False, contents=None):
        def arguments():
            p.emit((0x807E0000 if real else 0x387E0000) | slot, 0x38800000 | start, 0x38A00000 | length)
            word(6, value)
            p.emit(0x38FE0280, 0x3D008000, 0x61080002 | direction)

        if real:
            clear_trace()
            arguments()
            p.emit(0x819E0098, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x907E00A4)
            for source, target in ((720, 168), (724, 172)):
                p.emit(0x807E0000 | source, 0x907E0000 | target)
        clear_trace()
        arguments()
        p.call("CFArrayBSearchValues")
        if real:
            p.emit(0x809E00A4)
            p.equal("native search matches the real PEF export")
            for source, target in ((720, 168), (724, 172)):
                p.emit(0x807E0000 | source, 0x809E0000 | target)
                p.equal("comparator call count/order matches real CarbonLib")
        else:
            expected = start
            while expected < start + length and contents[expected] < value:
                expected += 1
            if storage and start and length and contents[start] <= value <= contents[start + length - 1]:
                expected = max(expected, start + length - (1 << (length.bit_length() - 1)) + 1)
            p.expect(expected, "search insertion result and CFStorage range quirk")

    values = [0, 1, 1, 0x80000000, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF]
    queries = sorted(set(values + [2, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFD]))
    if real:
        for offset, name in ((512, b"CoreFoundationLib"), (544, b"CFArrayBSearchValues")):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
        p.emit(0x387E0200, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "real CoreFoundation CFM connection")
        p.emit(0x807E0090, 0x389E0220, 0x38BE0098, 0x38DE009C)
        p.call("FindSymbol")
        p.expect(0, "real PEF search symbol")
        p.emit(0x887E009C)
        p.expect(2, "oracle is a function descriptor")
        p.emit(0x807E0098, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("oracle begins with original PPC code, not a native service stub")
        for direction in (0, 1):
            ordered = list(reversed(values)) if direction else values
            struct.pack_into(">8I", p.data, 256, *values)
            # Copy in the selected order at runtime so both directions share
            # one source payload without rewriting earlier code's constants.
            for i, value in enumerate(ordered):
                word(3, value)
                p.emit(0x907E0100 + i * 4)
            p.emit(0x38600000, 0x389E0100, 0x38A00008, 0x38C00000)
            p.call("CFArrayCreate")
            p.nonzero("immutable search array")
            p.emit(0x907E0080)
            for slot, capacity in ((132, 8), (136, 0), (140, 0)):
                p.emit(0x38600000, 0x38800000 | capacity, 0x38A00000)
                p.call("CFArrayCreateMutable")
                p.nonzero("mutable search array")
                p.emit(0x907E0000 | slot)
                if slot != 140:
                    for value in ordered:
                        p.emit(0x807E0000 | slot)
                        word(4, value)
                        p.call("CFArrayAppendValue")
                else:
                    p.emit(0x3BE00000)
                    loop = len(p.code)
                    p.emit(0x807E008C, 0x57E4F87E)
                    if direction:
                        p.emit(0x20842710)
                    p.call("CFArrayAppendValue")
                    p.emit(0x3BFF0001, 0x2C1F4E20)
                    branch = len(p.code)
                    p.emit(0x41800000 | (((loop - branch) * 4) & 65535))
            for slot, kind in ((128, 0), (132, 1), (136, 2), (140, 3)):
                p.emit(0x807E0000 | slot, 0x80630004, 0x70630003)
                p.expect(kind, "real search storage form")
                if kind < 3:
                    for start in range(9):
                        for length in range(9 - start):
                            for value in queries:
                                search(slot, start, length, value, direction)
                else:
                    for start, length in ((0, 1), (1, 1), (1, 7), (3, 8), (17, 15), (16380, 12), (19998, 2)):
                        first = 10000 - start // 2 if direction else start // 2
                        last = 10000 - (start + length - 1) // 2 if direction else (start + length - 1) // 2
                        keys = {
                            0,
                            0xFFFFFFFF,
                            first,
                            last,
                            (first + last) // 2,
                            max(0, first - 1),
                            first + 1,
                            max(0, last - 1),
                            last + 1,
                        }
                        for value in sorted(keys):
                            search(slot, start, length, value, direction)
                    for start in (0, 1, 20000):
                        search(slot, start, 0, 0, direction)
            # Exercise the contiguous helper's larger unrolled bit-search
            # paths, independently of CFStorage's separate search routine.
            p.emit(0x3BE00000)
            loop = len(p.code)
            p.emit(0x57E3F87E)
            if direction:
                p.emit(0x20630100)
            p.emit(0x57E4103A, 0x38840400, 0x7C7E212E, 0x3BFF0001, 0x2C1F0200)
            branch = len(p.code)
            p.emit(0x41800000 | (((loop - branch) * 4) & 65535))
            p.emit(0x38600000, 0x389E0400, 0x38A00200, 0x38C00000)
            p.call("CFArrayCreate")
            p.nonzero("large contiguous search array")
            p.emit(0x907E00B0)
            for start, length in (
                (0, 16),
                (0, 17),
                (1, 31),
                (3, 32),
                (17, 33),
                (20, 63),
                (64, 64),
                (7, 65),
                (0, 255),
                (1, 256),
                (13, 257),
                (1, 511),
                (0, 512),
            ):
                first = 256 - start // 2 if direction else start // 2
                last = 256 - (start + length - 1) // 2 if direction else (start + length - 1) // 2
                keys = {
                    0,
                    0xFFFFFFFF,
                    first,
                    last,
                    (first + last) // 2,
                    max(0, first - 1),
                    first + 1,
                    max(0, last - 1),
                    last + 1,
                }
                for value in sorted(keys):
                    search(176, start, length, value, direction)
            for slot in (128, 132, 136, 140, 176):
                p.emit(0x807E0000 | slot)
                p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "oracle CFM reference closed")
    else:
        struct.pack_into(">8I", p.data, 256, *values)
        p.data[320] = 3
        struct.pack_into(">8I", p.data, 336, *values)
        struct.pack_into(">I", p.data, 408, 4)
        struct.pack_into(">I", p.data, 424, 32)
        p.data[428] = 1
        p.emit(0x387E0100, 0x907E01B4, 0x38600008, 0x907E0088)
        for kind in range(4):
            flags = 13 if kind == 1 else kind
            p.emit(0x38600000 | flags, 0x907E0084)
            if kind < 2:
                header = 36 if kind else 12
                for i in range(8):
                    p.emit(0x807E0100 + i * 4, 0x907E0000 | (128 + header + i * 4))
            else:
                p.emit(0x387E0000 | (320 if kind == 2 else 400), 0x907E008C)
            for start in range(9):
                for length in range(9 - start):
                    for value in queries:
                        search(128, start, length, value, storage=kind == 3, contents=values)
        for name in ("CarbonFallbackToken", "CFFallbackToken"):
            p.call(name)
            p.expect(73, "search retains initialized PEF fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, "PPC " + label + " Search " + ("Real Probe" if real else "Probe"), carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_search_probe(args.destination, args.library, args.real)
