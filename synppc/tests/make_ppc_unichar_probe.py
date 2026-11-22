#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_unichar_probe.py
#
#   Summary:   Original companion-only Unicode membership probes and an exhaustive PPC oracle.
#
#   Classes:   None declared.
#
#   Functions: write_membership_reference, make_unichar_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original companion-only Unicode membership probes and an exhaustive PPC oracle."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


OFFSETS = (0x348, 0x344, 0x340, 0x33C, 0x338, 0x330, 0x334, 0x32C, 0x328, 0x324, 0x320, 0x31C)


def write_membership_reference(destination, foreign=False, failure=False):
    code = [0] * ((0x5DEE0 + (4 if foreign else 0)) // 4)
    data = bytearray(0x72A8)
    expected = {}
    initializer = [0x38600049, 0x90620200]
    for which, offset in enumerate(OFFSETS):
        base = 0x1000 + which * 256
        singles = sorted({which + 1, 1023, 0x7FFF, 0x8000, 0xFFFF})
        ranges = [(1024, 1027), (0xFF00 + which * 8, 0xFF03 + which * 8)]
        values = set(singles)
        for first, last in ranges:
            values.update(range(first, last + 1))
        expected[which + 4] = values
        summary = [0, 0]
        for value in values:
            summary[value >> 15] |= 1 << ((value >> 10) & 31)
        struct.pack_into(">9I", data, base, 1, *summary, 0, 0, len(singles), len(ranges), singles[0], singles[-1])
        struct.pack_into(">5I", data, base + 36, *singles)
        for index, (first, last) in enumerate(ranges):
            struct.pack_into(">II", data, base + 56 + index * 8, first, last)
        initializer += [0x38620000 | base, 0x90620000 | offset]
    initializer += [0x38600000, 0x4E800020]
    code[0:2] = [0x80620200, 0x4E800020]
    code[8 // 4 : 8 // 4 + len(initializer)] = initializer
    code[0x461FC // 4 : 0x461FC // 4 + 2] = [0x386001FF, 0x4E800020]
    imports, reloc = [], [0x4602]
    if failure:
        imports = [("InterfaceLib", "ROMAWUniCharFailure", 2)]
        code[0x461FC // 4 : 0x461FC // 4 + 5] = [0x81820040, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        reloc += [0xA000, 64, 0x4A00]
    for index, address in enumerate((0x461FC, 0, 8)):
        struct.pack_into(">II", data, index * 8, address, 0)
    image = pef(
        code,
        data,
        imports=imports,
        exports=[("CFUniCharIsMemberOf", 2, 0, 1), ("CFUniCharProbeToken", 2, 8, 1)],
        init=16,
        reloc=reloc,
    )
    write_fragment(destination, "CoreFoundationLib", image, 0)
    return expected


def make_unichar_probe(destination, real=False, mode="valid"):
    expected = None
    if not real:
        write_carbon_fallback(destination)
        expected = write_membership_reference(
            destination, foreign=mode.startswith("foreign"), failure=mode == "foreign-failure"
        )
    owner = "CarbonLib" if mode == "wrong-library" else "CoreFoundationLib"
    imports = [(owner, "CFUniCharIsMemberOf", 2), ("CarbonLib" if real else "InterfaceLib", "NumToString", 2)]
    if real:
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [("CoreFoundationLib", "CFUniCharProbeToken", 2), ("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    # Reserve separate nonvolatile saves outside the eight-word parameter area.
    p.emit(0x9421FFA0, 0x93610050, 0x93810054, 0x93A10058)

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def original():
        p.emit(0x819E00A0, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    if mode == "wrong-library":
        p.emit(0x38600041, 0x38800001)
        p.call("CFUniCharIsMemberOf")
        p.emit(0)
    elif mode.startswith("foreign"):
        integer(3, 0xABCD0041)
        integer(4, 0xFFFFFFFF)
        p.call("CFUniCharIsMemberOf")
        if mode == "foreign-failure":
            p.emit(0)
        else:
            p.expect(255, "foreign profile delegates and preserves an unsigned Boolean byte")
    elif real:
        for offset, name in ((512, b"CoreFoundationLib"), (544, b"CFUniCharIsMemberOf")):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
        p.emit(0x387E0200, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "real Unicode oracle connection")
        p.emit(0x807E0090, 0x389E0220, 0x38BE00A0, 0x38DE00A4)
        p.call("FindSymbol")
        p.expect(0, "original membership export")
        p.emit(0x887E00A4)
        p.expect(2, "original membership export is a descriptor")
        p.emit(0x807E00A0, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("original membership oracle executes PPC code")
        for charset in range(16):
            p.emit(0x3B600000, 0x3B800000 | charset, 0x3BE00000)
            loop = len(p.code)
            p.emit(0x7F63DB78, 0x7F84E378)
            p.call("CFUniCharIsMemberOf")
            p.emit(0x7C7D1B78, 0x7F63DB78, 0x7F84E378)
            original()
            p.emit(0x7FA4EB78)
            p.equal(f"all BMP code units match original set {charset}")
            p.emit(0x7FFF1A14, 0x3B7B0001, 0x3C800001, 0x7C1B2040)
            p.emit(0x41800000 | (((loop - len(p.code)) * 4) & 0xFFFC))
            p.emit(0x7F63DB78)
            p.expect(65536, "every BMP code unit was checked")
            p.emit(0x93FE0100 | (charset * 4), 0x7FE3FB78, 0x389E0180)
            p.call("NumToString")
        for charset in (1, 16, 0x10001, 0xFFFFFFFF):
            integer(3, 0xABCD0041)
            integer(4, charset)
            p.call("CFUniCharIsMemberOf")
            p.emit(0x7C7D1B78)
            integer(3, 0xABCD0041)
            integer(4, charset)
            original()
            p.emit(0x7FA4EB78)
            p.equal("character width and invalid 32-bit selectors match original")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "original Unicode oracle connection closes")
    else:
        p.call("CFUniCharProbeToken")
        p.expect(0x49, "companion fixture initialization executed")
        samples = {
            0,
            1,
            8,
            9,
            13,
            14,
            31,
            32,
            127,
            159,
            160,
            1023,
            1024,
            0x2000,
            0x200B,
            0x2028,
            0x2029,
            0x3000,
            0x7FFF,
            0x8000,
            0xFFFF,
        }
        for values in expected.values():
            for value in values:
                samples.update((max(0, value - 1), value, min(65535, value + 1)))
        for charset in range(16):
            for value in sorted(samples):
                integer(3, value)
                integer(4, charset)
                p.call("CFUniCharIsMemberOf")
                if charset >= 4:
                    answer = value in expected[charset]
                elif charset == 0:
                    answer = False
                elif charset == 1:
                    answer = value <= 31 or 127 <= value <= 159
                else:
                    answer = value in (32, 160, 0x3000) or 0x2000 <= value <= 0x200B
                    answer |= charset == 3 and (9 <= value <= 13 or value in (0x2028, 0x2029))
                p.expect(int(answer), "native membership follows initialized companion tables")
        p.call("CarbonFallbackToken")
        p.expect(0x49, "the main fragment still initializes and supplies missing APIs")
    p.emit(0x83610050, 0x83810054, 0x83A10058, 0x38210060)
    p.finish(destination, "PPC Core UniChar Real Probe" if real else "PPC UniChar Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode", nargs="?", default="valid", choices=("valid", "foreign", "foreign-failure", "wrong-library")
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CoreFoundationLib",), default="CoreFoundationLib")
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real-library validation requires valid mode")
    make_unichar_probe(args.destination, args.real, args.mode)
