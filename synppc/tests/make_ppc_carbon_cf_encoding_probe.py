#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_encoding_probe.py
#
#   Summary:   CF byte extraction and fastest-encoding ABI and original-PPC comparisons.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_encoding_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""CF byte extraction and fastest-encoding ABI and original-PPC comparisons."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAMES = ("CFStringGetBytes", "CFStringGetFastestEncoding")


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    encoder = [0x80030004, 0x2C000010, 0x41820008, 0]
    for register, expected in ((4, 1), (5, 2), (6, 1), (7, 0x100), (8, 0x7F), (10, 8)):
        encoder += [0x2C000000 | (register << 16) | expected, 0x41820008, 0]
    encoder += [
        0x81610038,
        0x2C0B0000,
        0x4182000C,
        0x38000002,
        0x900B0000,
        0x38000059,
        0x98090000,
        0x3800005A,
        0x98090001,
        0x38600002,
        0x4E800020,
    ]
    constant = [0x3C6089AB, 0x6063CDEF, 0x4E800020]
    bodies = [
        (NAMES[0], 0x38AC8, constant),
        (NAMES[1], 0x3CFA8, constant),
        ("__CFStringComputeEightBitStringEncoding", 0x357C4, [0x38600000, 0x90623A70, 0x4E800020]),
        # The synthetic engine checks the full ABI rather than encoding text.
        ("__CFStringEncodeByteStream", 0x419BC, encoder),
        ("CFEncodeToken", 0x50, [0x80620300, 0x4E800020]),
    ]
    imports = []
    if mode in ("encoding-failure", "engine-failure"):
        failure = "ROMAWCFEncodingFailure" if mode == "encoding-failure" else "ROMAWCFByteEngineFailure"
        imports = [("InterfaceLib", failure, 2)]
        index = 2 if mode == "encoding-failure" else 3
        name, offset, _ = bodies[index]
        bodies[index] = (name, offset, [0x81820080, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420])
    exports = []
    for index, (name, offset, words) in enumerate(bodies):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    code[4:8] = [0x38600049, 0x90620300, 0x38600000, 0x4E800020]
    struct.pack_into(">II", data, 40, 16, 0)
    struct.pack_into(">I", data, 0x3A70, 0xFFFFFFFF)
    reloc = [0x4605] + ([0xA000, 0x80, 0x4A00] if imports else [])
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=40, reloc=reloc), 0
    )


def make_encoding_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
    imports = [(library, name, 2) for name in NAMES]
    if real:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFStringCreateWithBytes",
                "CFStringCreateWithCharacters",
                "CFStringCreateMutable",
                "CFStringAppendCharacters",
                "CFRelease",
            )
        ]
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [("CoreFoundationLib", "CFEncodeToken", 2), ("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    p.data.extend(bytes(0x4000 - len(p.data)))

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def fill():
        integer(3, 0xA5A5A5A5)
        p.emit(0x907E00D4, 0x907E00D8, 0x38DE03FC, 0x38FE0FFC, 0x38A00000)
        loop = len(p.code)
        p.emit(0x7C66292E, 0x7C67292E, 0x38A50004, 0x2C050408)
        p.emit(0x41800000 | (((loop - len(p.code)) * 4) & 0xFFFC))

    def setup(start, length, encoding, capacity, variant, original=False):
        measure, publish, external, loss = variant
        p.emit(0x807E00C0)
        for register, value in ((4, start), (5, length), (6, encoding), (7, loss), (8, external), (10, capacity)):
            integer(register, value)
        p.emit(0x39200000 if measure else (0x393E1001 if original else 0x393E0401))
        p.emit((0x381E00D8 if original else 0x381E00D4) if publish else 0x38000000, 0x90010038)

    def original(slot):
        p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    if real:
        for offset, name in ((256, "CoreFoundationLib"), (288, NAMES[0]), (336, NAMES[1])):
            text = name.encode()
            p.data[offset : offset + len(text) + 1] = bytes([len(text)]) + text
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "real encoding oracle connection")
        for offset, slot in ((288, 160), (336, 164)):
            p.emit(0x807E0090, 0x389E0000 | offset, 0x38BE0000 | slot, 0x38DE00A8)
            p.call("FindSymbol")
            p.expect(0, "original encoding export")
            p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("encoding oracle executes original PPC code")
        p.data[0x3000:0x3005] = b"A\0\x80\xffZ"
        p.data[0x3100:0x3200] = bytes(range(256))
        p.data[0x3500:0x3800] = (b"ABC" + bytes((0x80, 0x41, 0x42, 0x43)) * 192)[:768]
        struct.pack_into(">6H", p.data, 0x3200, 0x41, 0x100, 0xD800, 0xDC00, 0xDC00, 0xFFFF)
        for i in range(255):
            struct.pack_into(">H", p.data, 0x3300 + i * 2, 0x100)
        objects = [
            (False, 0, 0x3000, False),
            (False, 5, 0x3000, False),
            (False, 256, 0x3100, False),
            (False, 768, 0x3500, False),
            (True, 6, 0x3200, False),
            (True, 255, 0x3300, False),
            (True, 6, 0x3200, True),
        ]
        for unicode, length, source, mutable in objects:
            if mutable:
                p.emit(0x38600000, 0x38800000)
                p.call("CFStringCreateMutable")
                p.nonzero("encoding mutable constructor")
                p.emit(0x907E00C0, 0x389E0000 | source, 0x38A00000 | length)
                p.call("CFStringAppendCharacters")
                p.emit(0x807E00C0)
            else:
                p.emit(0x38600000, 0x389E0000 | source, 0x38A00000 | length, 0x38C00000, 0x38E00000)
                p.call("CFStringCreateWithCharacters" if unicode else "CFStringCreateWithBytes")
            p.nonzero("encoding source constructor")
            p.emit(0x907E00C0, 0x807E00C0)
            p.call(NAMES[1])
            p.emit(0x907E00CC, 0x807E00C0)
            original(164)
            p.emit(0x809E00CC)
            p.equal("fastest encoding matches original")
            # Ninth arguments must not overwrite the outer saved r30 at SP+56.
            p.emit(0x9421FFB0)
            ranges = ((0, 0),) if not length else ((0, length), (1, length - 1), (length - 1, 1))
            for start, count in ranges:
                for encoding in (0, 0x600, 0x100, 0x8000100, 0xBFF):
                    for capacity in (0, 1, 4, 17, 1024):
                        for variant in (
                            (False, True, 0, 0),
                            (True, True, 1, 0x7F),
                            (False, False, 1, 0x7F),
                            (True, False, 0, 0),
                        ):
                            fill()
                            setup(start, count, encoding, capacity, variant)
                            p.call(NAMES[0])
                            p.emit(0x907E00CC)
                            setup(start, count, encoding, capacity, variant, True)
                            original(160)
                            p.emit(0x809E00CC)
                            label = f"get bytes source={source:x} count={length} range={start}/{count} encoding={encoding:x} cap={capacity} variant={variant}"
                            p.equal(label + " count matches")
                            p.emit(0x807E00D4, 0x809E00D8)
                            p.equal(label + " used bytes match")
                            p.emit(0x38DE03FC, 0x38FE0FFC, 0x38A00000)
                            loop = len(p.code)
                            p.emit(0x7C66282E, 0x7C87282E)
                            p.equal(label + " output and guards match")
                            p.emit(0x38A50004, 0x2C050408)
                            p.emit(0x41800000 | (((loop - len(p.code)) * 4) & 0xFFFC))
            p.emit(0x38210050, 0x807E00C0)
            p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "encoding oracle connection closes")
    else:
        p.call("CFEncodeToken")
        p.expect(0x49, "original encoding initializer ran")
        struct.pack_into(">III", p.data, 512, 0, 0, 5)
        p.data[524:529] = b"A\0\x80\xffZ"
        struct.pack_into(">III", p.data, 544, 0, 0x10, 5)
        p.emit(0x387E0220)
        p.call(NAMES[1])
        p.expect(0x89ABCDEF if mode == "foreign" else 0x100, "wide fastest encoding")
        p.emit(0x387E0200, 0x907E00C0, 0x9421FFB0)
        for measure in (False, True):
            fill()
            setup(1, 3, 0, 2, (measure, True, 1, 0x7F))
            p.call(NAMES[0])
            p.expect(0x89ABCDEF if mode == "foreign" else (3 if measure else 2), "byte range and ninth stack argument")
            if mode != "foreign":
                p.emit(0x807E00D4)
                p.expect(3 if measure else 2, "used-byte output uses guest byte order")
                p.emit(0x807E0400)
                p.expect(0xA5A5A5A5 if measure else 0xA50080A5, "raw binary payload and output guards")
        p.emit(0x38210050, 0x387E0200)
        p.call(NAMES[1])
        p.expect(0x89ABCDEF if mode == "foreign" else 0, "shared initialized byte encoding")
        p.emit(0x387E0220, 0x907E00C0, 0x9421FFB0)
        for publish in (True, False):
            fill()
            setup(1, 2, 0x100, 8, (False, publish, 1, 0x7F))
            p.call(NAMES[0])
            p.expect(0x89ABCDEF if mode == "foreign" else 2, "private engine argument order")
            if mode != "foreign":
                p.emit(0x807E00D4)
                p.expect(2 if publish else 0xA5A5A5A5, "optional ninth engine argument")
                p.emit(0x807E0400)
                p.expect(0xA5595AA5, "fallback engine's original output")
        p.emit(0x38210050)
        p.call("CarbonFallbackToken")
        p.expect(0x49, "missing APIs preserve main fallback initialization")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, f"PPC {label} CF Encode {'Real ' if real else ''}Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode", nargs="?", default="valid", choices=("valid", "foreign", "encoding-failure", "engine-failure")
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", default="CarbonLib", choices=("CarbonLib", "CoreFoundationLib"))
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real encoding validation requires valid mode")
    make_encoding_probe(args.destination, args.library, args.real, args.mode)
