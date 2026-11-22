#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_pascal_probe.py
#
#   Summary:   Shared synthetic CF string reference and differential tests for string conversion.
#
#   Classes:   None declared.
#
#   Functions: pointer_reference_bodies, write_reference, make_pascal_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CF Pascal-string ABI fixtures and original-PPC differential tests."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAME = "CFStringGetPascalString"
POINTER_NAMES = ("CFStringGetCStringPtr", "CFStringGetPascalStringPtr")


def pointer_reference_bodies():
    return [
        (POINTER_NAMES[0], 0x38FF4, [0x38601234, 0x4E800020]),
        (POINTER_NAMES[1], 0x38D70, [0x38601234, 0x4E800020]),
        ("CFBorrowToken", 0x28, [0x80620300, 0x4E800020]),
        ("CFBorrowSetEncoding", 0x20, [0x90623A70, 0x4E800020]),
    ]


def write_reference(destination, mode, c_string=False):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    # Ninth PPC argument is the used-byte output at caller SP+56. All three
    # non-encoding flags must be zero. Return min(character count, capacity).
    encoder = [
        0x2C040000,
        0x41820008,
        0,
        0x2C060000,
        0x41820008,
        0,
        0x2C080000,
        0x41820008,
        0,
        0x81610038,
        0x39800000,
        0x7C0C2800,
        0x4080001C,
        0x7C0C5000,
        0x40800014,
        0x380C0041,
        0x7C0961AE,
        0x398C0001,
        0x4BFFFFE4,
        0x918B0000,
        0x7D836378,
        0x4E800020,
    ]
    entry = ("CFStringGetCString", 0x395A0) if c_string else (NAME, 0x39274)
    entries = [(NAME, 0x39274), ("CFStringGetCString", 0x395A0)] if mode in ("valid", "foreign") else [entry]
    bodies = [(name, offset, [0x386001FF, 0x4E800020]) for name, offset in entries]
    bodies += [
        ("__CFStringComputeEightBitStringEncoding", 0x357C4, [0x38600000, 0x90623A70, 0x4E800020]),
        # The fixture's engine emits ASCII rather than real encoded bytes.
        # Keep this dependency foreign as native engine branches migrate.
        ("__CFStringEncodeByteStream", 0x419BC, encoder),
        ("CFPascalToken", 0, [0x80620300, 0x4E800020]),
    ]
    if mode in ("valid", "foreign"):
        bodies += pointer_reference_bodies()
    imports = []
    if mode in ("encoding-failure", "encoder-failure"):
        imports = [("InterfaceLib", "ROMAWCFPascalFailure", 2)]
        index = 1 if mode == "encoding-failure" else 2
        name, offset, _ = bodies[index]
        bodies[index] = (name, offset, [0x81820080, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420])
    native_failure = mode in ("utf8-failure", "nonlossy-failure")
    if native_failure:
        failure = "ROMAWCFUTF8Failure" if mode == "utf8-failure" else "ROMAWCFNonLossyFailure"
        imports = [("InterfaceLib", failure, 2)]
        name, _, words = bodies[2]
        bodies[2] = (name, 0x419B8, words)
    exports = []
    for index, (name, offset, words) in enumerate(bodies):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    initializer_vector = len(bodies) * 8
    struct.pack_into(">II", data, initializer_vector, 8, 0)
    code[2:7] = [0x38600049, 0x90620300, 0x38600000, 0x4E800020, 0x60000000]
    struct.pack_into(">I", data, 0x3A70, 0xFFFFFFFF if mode == "encoding-failure" else 0)
    reloc = [0x4600 | len(bodies)]
    if native_failure:
        struct.pack_into(">II", data, 40, 0x60, 0)
        code[0x60 // 4 : 0x60 // 4 + 5] = [0x81820080, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        initializer = [0x38600049, 0x90620300, 0x38620028]
        if mode == "utf8-failure":
            initializer += [0x9062593C]
            struct.pack_into(">I", data, 0x3E88, 0x8000100)
        else:
            initializer += [0x90626F5C, 0x38620200, 0x906205A8]
            data[0x21B:0x22B] = b"0123456789abcdef"
        initializer += [0x38600000, 0x4E800020]
        code[2 : 2 + len(initializer)] = initializer
        reloc = [0x4605]
    if imports:
        reloc += [0xA000, 0x80, 0x4A00]
    write_fragment(
        destination,
        "CoreFoundationLib",
        pef(code, data, imports=imports, exports=exports, init=initializer_vector, reloc=reloc),
        0,
    )


def make_pascal_probe(destination, library="CarbonLib", real=False, mode="valid", c_string=False):
    name = "CFStringGetCString" if c_string else NAME
    kind = ("CF CStr" if real else "CF CString") if c_string else "CF Pascal"
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode, c_string)
    imports = [(library, name, 2)]
    if real:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFStringCreateWithCharacters",
                "CFStringCreateWithBytes",
                "CFStringCreateMutable",
                "CFStringAppendCharacters",
                "CFRelease",
            )
        ]
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
    else:
        imports += [("CoreFoundationLib", "CFPascalToken", 2), ("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    p.data += bytes(0x6000 - len(p.data))

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def fill():
        integer(3, 0xA5A5A5A5)
        p.emit(0x38DE0FFC, 0x38FE17FC, 0x38A00000)
        loop = len(p.code)
        p.emit(0x7C66292E, 0x7C67292E, 0x38A50004, 0x2C050408)
        p.emit(0x41800000 | (((loop - len(p.code)) * 4) & 0xFFFC))

    def setup(capacity, encoding, original=False):
        p.emit(0x807E00C8, 0x389E1800 if original else 0x389E1000)
        integer(5, capacity)
        integer(6, encoding)

    if real:
        for offset, text in ((256, b"CoreFoundationLib"), (288, name.encode())):
            p.data[offset : offset + len(text) + 1] = bytes([len(text)]) + text
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "original conversion connection")
        p.emit(0x807E0090, 0x389E0120, 0x38BE00A0, 0x38DE00A4)
        p.call("FindSymbol")
        p.expect(0, "original Pascal conversion export")
        p.emit(0x807E00A0, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("conversion oracle executes original PPC code")
        units = (0, 0x41, 0x7F, 0xE9, 0x100, 0x301, 0xD800, 0x41, 0xDC00, 0xD800, 0xDC00, 0xFFFF)
        for i in range(256):
            struct.pack_into(">H", p.data, 0x2000 + i * 2, units[i % len(units)])
            struct.pack_into(">H", p.data, 0x2200 + i * 2, 0x100)
        p.data[0x2400:0x2500] = bytes(range(256))
        struct.pack_into(">2H", p.data, 0x2600, 0xD800, 0xDC00)
        struct.pack_into(">5H", p.data, 0x2610, 0x41, 0xD83D, 0xDE00, 0x42, 0xDC00)
        struct.pack_into(">H", p.data, 0x2620, 0xDC00)
        struct.pack_into(">H", p.data, 0x2630, 0xD800)
        p.data[0x2C00:0x2F00] = (b"ABC" + bytes((0x80, 0x41, 0x42, 0x43)) * 192)[:768]
        objects = [(False, n, 0x2400, False) for n in (0, 1, 3, 255, 256)]
        objects += [(True, n, 0x2000, False) for n in (0, 1, 3, 9, 127, 128, 255, 256)]
        objects += [(True, 255, 0x2200, False), (True, 9, 0x2000, True), (True, 255, 0x2200, True)]
        objects += [(True, n, source, False) for n, source in ((2, 0x2600), (5, 0x2610), (1, 0x2620), (1, 0x2630))]
        objects += [(False, n, 0x2C00, False) for n in (514, 515, 516, 768)]
        for unicode, length, source, mutable in objects:
            if mutable:
                p.emit(0x38600000, 0x38800000)
                p.call("CFStringCreateMutable")
                p.nonzero("real mutable conversion source constructor")
                p.emit(0x907E00C8, 0x389E0000 | source, 0x38A00000 | length)
                p.call("CFStringAppendCharacters")
                p.emit(0x807E00C8)
            elif unicode:
                p.emit(0x38600000, 0x389E0000 | source, 0x38A00000 | length)
                p.call("CFStringCreateWithCharacters")
            else:
                p.emit(0x38600000, 0x389E0000 | source, 0x38A00000 | length, 0x38C00000, 0x38E00000)
                p.call("CFStringCreateWithBytes")
            p.nonzero("real conversion source constructor")
            p.emit(0x907E00C8)
            for encoding in (0, 0x600, 0x100, 0x8000100, 0xBFF):
                for capacity in (-1, 0, 1, 2, 3, 4, 16, 255, 256, 257, 512, 1024):
                    fill()
                    setup(capacity, encoding)
                    p.call(name)
                    p.emit(0x907E00CC)
                    setup(capacity, encoding, True)
                    p.emit(0x819E00A0, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378, 0x809E00CC)
                    label = (
                        f"source={source:x} unicode={unicode} mutable={mutable} count={length} "
                        f"encoding={encoding:x} capacity={capacity}"
                    )
                    p.equal(label + " Boolean matches")
                    if not c_string and source == 0x2200 and encoding == 0x8000100 and capacity >= 512:
                        p.expect(1, "the original accepts all 255 units in a 510-byte UTF-8 payload")
                        p.emit(0x887E1000)
                        p.expect(254, "the 1.4 Pascal prefix stores the encoded length's low byte")
                    p.emit(0x38DE0FFC, 0x38FE17FC, 0x38A00000)
                    loop = len(p.code)
                    p.emit(0x7C66282E, 0x7C87282E)
                    p.equal(label + " payload/prefix/guards match")
                    p.emit(0x38A50004, 0x2C050408)
                    p.emit(0x41800000 | (((loop - len(p.code)) * 4) & 0xFFFC))
            p.emit(0x807E00C8)
            p.call("CFRelease")
        # Record private-engine quirks independently of the public wrapper.
        # This descriptor always comes from the original PEF, even after the
        # private engine eventually acquires a complete native registration.
        private = b"__CFStringEncodeByteStream"
        p.data[336 : 337 + len(private)] = bytes([len(private)]) + private
        p.emit(0x807E0090, 0x389E0150, 0x38BE00B0, 0x38DE00A4)
        p.call("FindSymbol")
        p.expect(0, "original private encoding oracle")
        p.emit(0x807E00B0, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
        p.nonzero("private oracle executes original PPC code")
        struct.pack_into(">6H", p.data, 0x2800, 0x41, 0x100, 0x42, 0xFFFF, 0x43, 0x44)
        p.emit(0x38600000, 0x389E2800, 0x38A00006)
        p.call("CFStringCreateWithCharacters")
        p.nonzero("private oracle source")
        p.emit(0x907E00C8, 0x9421FFC0)
        for location, length, encoding, external, measure, count, used, word in (
            (1, 2, 0x100, 0, False, 2, 4, 0x00410100),
            (1, 2, 0x100, 1, False, 2, 6, 0xFEFF0041),
            (1, 2, 0x100, 1, True, 2, 6, None),
            (1, 2, 0xBFF, 0, False, 1, 6, 0x5C753031),
            (0, 6, 0xBFF, 0, False, 3, 8, 0x415C7530),
        ):
            fill()
            p.emit(
                0x807E00C8,
                0x38800000 | location,
                0x38A00000 | length,
                0x38C00000 | external,
                0x38E00000 | encoding,
                0x39000000,
                0x39200000 if measure else 0x393E1000,
                0x39400010,
                0x381E00D0,
                0x90010038,
                0x819E00B0,
                0x800C0000,
                0x804C0004,
                0x7C0903A6,
                0x4E800421,
                0x7FC2F378,
            )
            p.expect(count, "original private range/sentinel count")
            p.emit(0x807E00D0)
            p.expect(used, "original private BOM/measurement byte count")
            if word is not None:
                p.emit(0x807E1000)
                p.expect(word, "original private UTF-16/NonLossyASCII output")
        p.emit(0x38210040, 0x807E00C8)
        p.call("CFRelease")
        # Pin the original codec's surrogate rollback and zero-capacity
        # measurement rules independently of either public wrapper.
        p.emit(0x38600000, 0x389E2600, 0x38A00002)
        p.call("CFStringCreateWithCharacters")
        p.nonzero("original UTF-8 surrogate source")
        p.emit(0x907E00C8, 0x9421FFC0)
        for capacity, measure, external, count, used in (
            (0, False, 1, 2, 4),
            (1, False, 0, 1, 0),
            (3, False, 1, 1, 0),
            (4, False, 1, 2, 4),
            (1, True, 1, 2, 4),
        ):
            fill()
            p.emit(
                0x807E00C8,
                0x38800000,
                0x38A00002,
                0x38C00000 | external,
                0x3CE00800,
                0x60E70100,
                0x39000000,
                0x39200000 if measure else 0x393E1000,
                0x39400000 | capacity,
                0x381E00D0,
                0x90010038,
                0x819E00B0,
                0x800C0000,
                0x804C0004,
                0x7C0903A6,
                0x4E800421,
                0x7FC2F378,
            )
            p.expect(count, "original UTF-8 surrogate count")
            p.emit(0x807E00D0)
            p.expect(used, "original UTF-8 byte count")
            p.emit(0x807E1000)
            p.expect(0xF0908080 if capacity == 4 and not measure else 0xA5A5A5A5, "original UTF-8 guarded payload")
        p.emit(0x38210040, 0x807E00C8)
        p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "original conversion connection closes")
    else:
        p.call("CFPascalToken")
        p.expect(0x49, "original companion initializer ran")
        if mode in ("utf8-failure", "nonlossy-failure"):
            unicode = mode == "utf8-failure"
            struct.pack_into(">III", p.data, 512, 0, 0x10 if unicode else 0, 3)
            p.data[524:530] = b"\x01\x00\x01\x01\x01\x02" if unicode else b"\x80AB\0\0\0"
            p.emit(0x387E0200, 0x907E00C8)
            setup(16, 0x8000100 if unicode else 0xBFF)
            p.call(name)
            p.emit(0)
            label = "Carbon" if library == "CarbonLib" else "Core"
            p.finish(destination, f"PPC {label} {kind} Probe")
            return
        for slot, flags, length in (
            (512, 0, 3),
            (544, 4, 3),
            (576, 0x20, 3),
            (608, 0x10, 3),
            (640, 0, 256),
            (672, 0, 0),
        ):
            struct.pack_into(">III", p.data, slot, 0, flags, length)
            if flags == 0x20:
                p.data[720:724] = b"ABC\0"
                p.emit(0x387E02D0, 0x907E0248)
                struct.pack_into(">I", p.data, slot + 12, 0xFFFFFF)
            else:
                offset = slot + (8 if flags & 4 else 12)
                p.data[offset : offset + 3 + bool(flags & 4)] = (bytes([3]) if flags & 4 else b"") + b"ABC"
            p.emit(0x387E0000 | slot, 0x907E00C8)
            for capacity in (0, 1, 3, 4):
                fill()
                setup(capacity, 0x100 if flags & 0x10 else 0)
                p.call(name)
                if mode == "foreign":
                    p.expect(255, "foreign layout delegates its Boolean result")
                    continue
                expected = length <= 255 and (length < capacity)
                p.expect(int(expected), "native Pascal result follows source length and capacity")
                p.emit(0x887E1000)
                prefix = (
                    (65 if c_string and length else length)
                    if expected
                    else (0 if flags & 0x10 and capacity > 0 else 0xA5)
                )
                p.expect(prefix, "prefix preservation and partial-conversion failure")
                if expected and length:
                    p.emit(0x807E1000)
                    p.expect(0x41424300 if c_string else 0x03414243, "native copy and ninth-argument encoder output")
        if mode != "foreign":
            p.emit(0x807E024C)
            p.expect(3, "conversion updates the shared lazy length cache")
        p.call("CarbonFallbackToken")
        p.expect(0x49, "missing exports retain original main initialization")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, f"PPC {label} {kind} {'Real ' if real else ''}Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        default="valid",
        choices=("valid", "foreign", "encoding-failure", "encoder-failure", "utf8-failure", "nonlossy-failure"),
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--cstring", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real conversion validation requires valid mode")
    make_pascal_probe(args.destination, args.library, args.real, args.mode, args.cstring)
