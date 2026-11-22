#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_queries_probe.py
#
#   Summary:   Batch encoding-query ABI, cache, fallback and original-PPC comparisons.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_queries_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Batch encoding-query ABI, cache, fallback and original-PPC comparisons."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAMES = (
    "CFStringGetMaximumSizeForEncoding",
    "CFStringConvertEncodingToNSStringEncoding",
    "CFStringConvertNSStringEncodingToEncoding",
    "CFStringConvertEncodingToWindowsCodepage",
    "CFStringConvertWindowsCodepageToEncoding",
    "CFStringIsEncodingAvailable",
    "CFStringGetMostCompatibleMacStringEncoding",
)
OFFSETS = (0x35638, 0x439A0, 0x43AF8, 0x43BB8, 0x43C88, 0x43300, 0x43DA4)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    constant = [0x3C6089AB, 0x6063CDEF, 0x4E800020]
    bodies = [(name, offset, constant) for name, offset in zip(NAMES, OFFSETS)]
    get_value = [0x80A20400, 0x7C042800, 0x4182000C, 0x38600000, 0x4E800020, 0x80620404, 0x4E800020]
    create = []
    for register in (3, 4, 5, 6):
        create += [0x2C000000 | (register << 16), 0x41820008, 0]
    create += [0x80A2030C, 0x38A50001, 0x90A2030C, 0x38620400, 0x4E800020]
    set_value = [0x90820400, 0x90A20404, 0x80A20310, 0x38A50001, 0x90A20310, 0x4E800020]
    bodies += [
        ("CFStringGetSystemEncoding", 0x35524, [0x38600201, 0x4E800020]),
        ("CFDictionaryGetValue", 0x21E74, get_value),
        ("CFDictionaryCreateMutable", 0x2198C, create),
        ("CFDictionarySetValue", 0x224C8, set_value),
    ]
    for i, name in enumerate(("CFQueryToken", "CFQueryLookupCount", "CFQueryCreateCount", "CFQuerySetCount")):
        field = (0x300, 0x308, 0x30C, 0x310)[i]
        bodies.append((name, 0x50 + i * 8, [0x80620000 | field, 0x4E800020]))
    # The synthetic one-entry dictionary has no heap allocation to dispose.
    cache_fields = (0x308, 0x30C, 0x310, 0x400, 0x404, 0x9D8)
    bodies.append(
        ("CFQueryResetCache", 0x70, [0x38600000, *(0x90620000 | field for field in cache_fields), 0x4E800020])
    )
    lookup = [
        0x80A20308,
        0x38A50001,
        0x90A20308,
        0x3C80DEAD,
        0x6084BEEF,
        0x7C032000,
        0x4182000C,
        0x38600000,
        0x4E800020,
        0x38620480,
        0x4E800020,
    ]
    adapter = [0x38620500, 0x4E800020]
    base = [0x3CA00001, 0x38A5FFFF, 0x90A40000, 0x3860F50E, 0x4E800020]
    imports = []
    if mode in ("lookup-failure", "adapter-failure", "dictionary-failure"):
        failure = {
            "lookup-failure": "ROMAWCFQueryLookupFailure",
            "adapter-failure": "ROMAWCFQueryAdapterFailure",
            "dictionary-failure": "ROMAWCFQueryDictionaryFailure",
        }[mode]
        imports = [("InterfaceLib", failure, 2)]
        trampoline = [0x81820180, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        if mode == "lookup-failure":
            lookup = trampoline
        elif mode == "adapter-failure":
            adapter = trampoline
        else:
            index = next(i for i, body in enumerate(bodies) if body[0] == "CFDictionarySetValue")
            name, offset, _ = bodies[index]
            bodies[index] = (name, offset, trampoline)
    for offset, words in ((0x4DE58, lookup), (0x4DDA0, adapter), (0x43270, base)):
        code[offset // 4 : offset // 4 + len(words)] = words
    exports = []
    for index, (name, offset, words) in enumerate(bodies):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    init = len(bodies) * 8
    struct.pack_into(">II", data, init, 16, 0)
    code[4:8] = [0x38600049, 0x90620300, 0x38600000, 0x4E800020]
    for encoding, offset in (
        (0x600, 0x3D98),
        (0x201, 0x3DC8),
        (0, 0x3DF8),
        (0x500, 0x3E28),
        (0xB01, 0x3E58),
        (0x8000100, 0x3E88),
    ):
        struct.pack_into(">I", data, offset, encoding)
        struct.pack_into(">I", data, offset + 44, 0x100 if encoding == 0x8000100 else 0)
    struct.pack_into(">I", data, 0x480, 0xDEADBEEF)
    struct.pack_into(">I", data, 0x480 + 44, 7)
    for i in range(36):
        struct.pack_into(">H", data, 0x3CC0 + i * 2, 1000 + i)
    for i in range(17):
        struct.pack_into(">H", data, 0x3D08 + i * 2, 2000 + i)
    for i in range(30):
        struct.pack_into(">HH", data, 0x3D1A + i * 4, 100 + i * 10, 0x400 + i)
    struct.pack_into(">HH", data, 0x3D1A + 120, 0xFFFF, 0xFFFF)
    reloc = [0x4600 | len(bodies)] + ([0xA000, 0x180, 0x4A00] if imports else [])
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=init, reloc=reloc), 0
    )


def make_queries_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
    imports = [(library, name, 2) for name in NAMES]
    if real:
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFQueryToken",
                "CFQueryLookupCount",
                "CFQueryCreateCount",
                "CFQuerySetCount",
                "CFQueryResetCache",
            )
        ]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    p.data.extend(bytes(4096))

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def setup(index, value, length=0):
        integer(3, length if index == 0 else value)
        if index == 0:
            integer(4, value)

    def differential(index, value, length=0):
        setup(index, value, length)
        p.call(NAMES[index])
        p.emit(0x907E00E0)
        setup(index, value, length)
        p.emit(0x819E0000 | (160 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378, 0x809E00E0)
        p.equal(f"{NAMES[index]} value={value:x} length={length:x} matches original")

    if real:
        for offset, name in ((256, "CoreFoundationLib"), *[(320 + i * 64, n) for i, n in enumerate(NAMES)]):
            text = name.encode()
            p.data[offset : offset + len(text) + 1] = bytes([len(text)]) + text
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "original query connection")
        for i in range(len(NAMES)):
            p.emit(0x807E0090, 0x389E0000 | (320 + i * 64), 0x38BE0000 | (160 + i * 4), 0x38DE00D0)
            p.call("FindSymbol")
            p.expect(0, "original query export resolution")
            p.emit(0x807E0000 | (160 + i * 4), 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("query oracle executes original PPC code")
        encodings = sorted(
            set(range(0, 40))
            | set(range(0x1F8, 0x210))
            | set(range(0x3F8, 0x430))
            | set(range(0x4F8, 0x520))
            | set(range(0x5F8, 0x608))
            | {
                0x100,
                0x101,
                0x800,
                0x820,
                0x900,
                0x931,
                0x932,
                0xA00,
                0xA01,
                0xA02,
                0xA03,
                0xA05,
                0xB01,
                0xBFF,
                0xC02,
                0x8000100,
                0x10001,
                0x80000420,
                0xFFFF0201,
                0xFFFF0931,
                0x7FFFFFFF,
                0x80000000,
                0xFFFFFFFF,
            }
        )
        for value in encodings:
            for length in (0, 1, 17, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
                differential(0, value, length)
            for index in (1, 2, 3):
                differential(index, value)
        pages = sorted(
            set(range(0, 42))
            | set(range(430, 440))
            | set(range(730, 780, 5))
            | set(range(1240, 1265))
            | set(range(9998, 10010))
            | set(range(19998, 20005))
            | set(range(28585, 28605))
            | {
                850,
                851,
                852,
                855,
                860,
                861,
                862,
                863,
                864,
                865,
                866,
                869,
                874,
                932,
                936,
                949,
                950,
                1361,
                20127,
                20866,
                65000,
                65001,
                65002,
                0x80000000,
                0xFFFFFFFF,
            }
        )
        for page in pages:
            differential(4, page)
        for value in (0, 0x201, 0x500, 0x600, 0xB01, 0x8000100, 0xFFFFFFFF):
            differential(5, value)
            differential(6, value)
        for value in (0x100, 0xBFF):
            differential(5, value)
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "query oracle closes")
    else:
        p.call("CFQueryToken")
        p.expect(0x49, "original query initializer ran")
        cases = [
            (0, 0x100, 14, 7),
            (0, 0x8000100, 0xFFFFFFFA, 0x7FFFFFFF),
            (0, 0x820, 7, 7),
            (1, 0x8000100, 4, 0),
            (1, 0x1234, 0x80001234, 0),
            (2, 30, 0, 0),
            (2, 0x80001234, 0x1234, 0),
            (3, 0x405, 1005, 0),
            (3, 0x501, 2001, 0),
            (4, 150, 0x405, 0),
            (4, 1, 0x201, 0),
            (4, 65001, 0x8000100, 0),
            (5, 0x201, 1, 0),
            (6, 0x8000100, 0x100, 0),
        ]
        for index, value, expected, length in cases:
            setup(index, value, length)
            p.call(NAMES[index])
            p.expect((0xEF if index == 5 else 0x89ABCDEF) if mode == "foreign" else expected, "native query result")
        for index, value, expected in ((5, 0xDEADBEEF, 1), (6, 0xDEADBEEF, 7), (6, 0x920, 0xFFFFFFFF)):
            setup(index, value)
            p.call(NAMES[index])
            p.expect(
                (0xEF if index == 5 else 0x89ABCDEF) if mode == "foreign" else expected,
                "native external cache and base mapping",
            )
        if mode != "foreign":
            for name, count in (("CFQueryLookupCount", 2), ("CFQueryCreateCount", 1), ("CFQuerySetCount", 1)):
                p.call(name)
                p.expect(count, "shared definition cache lifecycle")
            p.call("CFQueryResetCache")
        p.call("CarbonFallbackToken")
        p.expect(0x49, "missing APIs retain initialized main fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, f"PPC {label} CF Query {'Real ' if real else ''}Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        default="valid",
        choices=("valid", "foreign", "lookup-failure", "adapter-failure", "dictionary-failure"),
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real queries require valid mode")
    make_queries_probe(args.destination, args.library, args.real, args.mode)
