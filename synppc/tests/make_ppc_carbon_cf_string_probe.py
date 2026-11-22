#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_string_probe.py
#
#   Summary:   Original CF character-reader probes, with an optional real-library oracle.
#
#   Classes:   None declared.
#
#   Functions: write_string_reference, make_string_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CF character-reader probes, with an optional real-library oracle."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


NAMES = ("CFStringGetCharacterAtIndex", "CFStringGetCharacters")
OFFSETS = (0x3891C, 0x38A0C)


def write_string_reference(destination):
    code = [0] * (0x5DEE0 // 4)
    data = bytearray(0x72A8)
    bodies = {
        0: [0x90626F5C, 0x4E800020],
        8: [0x38600049, 0x90620100, 0x38600000, 0x90626F5C, 0x4E800020],
        28: [0x80620100, 0x4E800020],
        OFFSETS[0]: [0x38607777, 0x4E800020],
        OFFSETS[1]: [0x4E800020],
    }
    for i, offset in enumerate((*OFFSETS, 0, 8, 28)):
        struct.pack_into(">II", data, i * 8, offset, 0)
    exports = [(name, 2, i * 8, 1) for i, name in enumerate(NAMES)]
    exports += [("CFStringProbeSetConverter", 2, 16, 1)]
    exports += [("CFStringProbeToken", 2, 32, 1)]
    for offset, words in bodies.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    write_fragment(destination, "CoreFoundationLib", pef(code, data, exports=exports, init=24, reloc=[0x4604]), 0)


def make_string_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if not real:
        write_carbon_fallback(destination)
        write_string_reference(destination)
    imports = [(library, name, 2) for name in (*NAMES, "CFStringGetLength")]
    if real:
        imports += [
            (library, name, 2)
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
        imports += [("CoreFoundationLib", name, 2) for name in ("CFStringProbeSetConverter", "CFStringProbeToken")]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    if mode == "converter-failure":
        imports += [("InterfaceLib", "ROMAWCarbonStringFailure", 2)]
    p = Probe(imports)
    p.data.extend(bytes(3072))

    def integer(register, value):
        value &= 0xFFFFFFFF
        p.emit(
            0x3C000000 | (register << 21) | (value >> 16),
            0x60000000 | (register << 21) | (register << 16) | (value & 65535),
        )

    def original(slot):
        p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def string_argument(slot, indirect):
        p.emit((0x807E0000 if indirect else 0x387E0000) | slot)

    def check_readers(slot, values, indirect=False):
        for index, value in enumerate(values):
            string_argument(slot, indirect)
            p.emit(0x38800000 | index)
            p.call(NAMES[0])
            if real:
                p.emit(0x907E00DC)
                string_argument(slot, indirect)
                p.emit(0x38800000 | index)
                original(160)
                p.emit(0x809E00DC)
                p.equal("native character equals original PPC reader")
            else:
                p.expect(value, "native character preserves unsigned UTF-16")
        for start, count in ((0, len(values)), (0, 0), (1, min(3, len(values) - 1)), (len(values) - 1, 1)):
            # Odd output pointers and trailing guards expose byte order and extent.
            integer(3, 0xA5A5A5A5)
            for offset in range(1024, 1628, 4):
                p.emit(0x907E0000 | offset)
            string_argument(slot, indirect)
            p.emit(0x38800000 | start, 0x38A00000 | count, 0x38DE0401)
            p.call(NAMES[1])
            if real:
                string_argument(slot, indirect)
                p.emit(0x38800000 | start, 0x38A00000 | count, 0x38DE0801)
                original(164)
            for i, value in enumerate(values[start : start + count]):
                p.emit(0xA07E0000 | (1025 + i * 2))
                if real:
                    p.emit(0xA09E0000 | (2049 + i * 2))
                    p.equal("native range equals original PPC reader")
                else:
                    p.expect(value, "native character range and byte conversion")
            for offset in (1024, 1025 + count * 2):
                p.emit(0x887E0000 | offset)
                p.expect(0xA5, "range copy preserves adjacent bytes")

    if mode != "valid":
        struct.pack_into(">III", p.data, 512, 0, 0, 2)
        p.data[524:526] = b"\x80\x81"
        if mode == "converter-failure":
            callback = Probe(imports)
            callback.call("ROMAWCarbonStringFailure")
            callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x4E800020)
            p.routines.append((400, callback.code))
        p.emit(0x387E0190)
        p.call("CFStringProbeSetConverter")
        p.emit(0x387E0200)
        if mode == "index-invalid":
            p.emit(0x38800000)
            p.call(NAMES[0])
        else:
            p.emit(0x38800000, 0x38A00002, 0x38DE0401)
            p.call(NAMES[1])
        p.emit(0)
    elif real:
        for offset, name in (
            (256, b"CoreFoundationLib"),
            (288, NAMES[0].encode()),
            (336, NAMES[1].encode()),
        ):
            p.data[offset : offset + len(name) + 1] = bytes([len(name)]) + name
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "real string oracle connection")
        for offset, slot in ((288, 160), (336, 164)):
            p.emit(0x807E0090, 0x389E0000 | offset, 0x38BE0000 | slot, 0x38DE00B0)
            p.call("FindSymbol")
            p.expect(0, "original string export resolution")
            p.emit(0x887E00B0)
            p.expect(2, "original string export is a descriptor")
            p.emit(0x807E0000 | slot, 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("oracle entry executes original PPC code")
        unicode = (0, 0x41, 0x7F, 0x80, 0xFF, 0x100, 0x301, 0xE9, 0xD800, 0xDC00, 0xFFFF)
        for i in range(70):
            struct.pack_into(">H", p.data, 512 + i * 2, unicode[i % len(unicode)])
        p.data[768:1024] = bytes(range(256))
        objects = []
        for i, length in enumerate((0, 1, 11, 31, 32, 33, 65)):
            p.emit(0x38600000, 0x389E0200, 0x38A00000 | length)
            p.call("CFStringCreateWithCharacters")
            p.nonzero("real UTF-16 string constructor")
            slot = 192 + i * 4
            p.emit(0x907E0000 | slot)
            objects.append((slot, length))
            if length:
                check_readers(slot, [0] * length, True)
        p.emit(0x38600000, 0x389E0300, 0x38A00100, 0x38C00000, 0x38E00000)
        p.call("CFStringCreateWithBytes")
        p.nonzero("real MacRoman byte string constructor with embedded NUL")
        p.emit(0x907E00F0)
        objects.append((240, 256))
        check_readers(240, [0] * 256, True)
        # Select each actual initialized built-in callback for a differential
        # read of the same byte object. Restore the original shared converter
        # before constructing or releasing further objects. No init is skipped.
        p.emit(0x807E00A4, 0x80830004, 0x909E00F8, 0x80A46F5C, 0x90BE00FC)
        for descriptor in (0x1FB4, 0x1FE4, 0x1FCC, 0x1F9C, 0x1F84):
            p.emit(0x809E00F8, 0x38A40000 | descriptor, 0x90A46F5C)
            check_readers(240, [0] * 256, True)
        p.emit(0x809E00F8, 0x80BE00FC, 0x90A46F5C)
        p.emit(0x38600000, 0x38800000)
        p.call("CFStringCreateMutable")
        p.nonzero("real mutable string constructor")
        p.emit(0x907E00F4)
        for length in (1, 32, 33):
            p.emit(0x807E00F4, 0x389E0200, 0x38A00000 | length)
            p.call("CFStringAppendCharacters")
        objects.append((244, 66))
        check_readers(244, [0] * 66, True)
        for slot, _ in objects:
            p.emit(0x807E0000 | slot)
            p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "close original string oracle")
    else:
        p.call("CFStringProbeToken")
        p.expect(0x49, "the original CoreFoundation fixture initializer actually ran")
        byte_values = (0, 0x7F, 0x80, 0xFF, 0x81)
        unicode = (0, 0x7F, 0x80, 0xFF, 0x100, 0xD800, 0xDC00, 0xFFFF)
        p.data[304:309] = bytes(byte_values)
        struct.pack_into(">8H", p.data, 320, *unicode)
        p.data[336:342] = bytes([5, *byte_values])
        p.data[352:358] = b"ASCII\0"
        for slot, flags, values, source in (
            (512, 0, byte_values, None),
            (576, 0x10, unicode, None),
            (640, 0x30, unicode, 320),
            (704, 4, byte_values, None),
            (736, 0x24, byte_values, 336),
            (768, 0x20, byte_values, 304),
            (800, 0x20, tuple(b"ASCII"), 352),
        ):
            struct.pack_into(">III", p.data, slot, 0, flags, len(values))
            if source is not None:
                struct.pack_into(">I", p.data, slot + 12, 0xFFFFFF if slot == 800 else len(values))
                p.emit(0x387E0000 | source, 0x907E0000 | (slot + 8))
            elif flags & 0x10:
                struct.pack_into(">" + "H" * len(values), p.data, slot + 12, *values)
            else:
                start = slot + (8 if flags & 4 else 12)
                p.data[start : start + len(values) + bool(flags & 4)] = (
                    bytes([len(values)]) if flags & 4 else b""
                ) + bytes(values)
        # Converter returns a distinct mapping and refuses FF using a nonzero
        # result whose low Boolean byte is zero. Native code must emit FFFE.
        p.routines.append(
            (400, [0x38840100, 0xB0850000, 0x2C0401FF, 0x4082000C, 0x38600100, 0x4E800020, 0x38600001, 0x4E800020])
        )
        for converter in (0, 400):
            p.emit(0x387E0000 | converter if converter else 0x38600000)
            p.call("CFStringProbeSetConverter")
            for slot, values in (
                (512, byte_values),
                (576, unicode),
                (640, unicode),
                (704, byte_values),
                (736, byte_values),
                (768, byte_values),
                (800, tuple(b"ASCII")),
            ):
                expected = [
                    (
                        value
                        if slot in (576, 640) or not converter or value < 128
                        else 0xFFFE if value == 255 else value + 256
                    )
                    for value in values
                ]
                check_readers(slot, expected)
        p.emit(0x807E032C)
        p.expect(0xFFFFFF, "character readers leave the lazy length cache untouched")
        p.call("CarbonFallbackToken")
        p.expect(0x49, "missing API keeps real fallback initialization")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, f"PPC {label} CF String {'Real ' if real else ''}Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        default="valid",
        choices=("valid", "index-invalid", "copy-invalid", "converter-failure"),
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", default="CarbonLib", choices=("CarbonLib", "CoreFoundationLib"))
    args = parser.parse_args()
    make_string_probe(args.destination, args.library, args.real, args.mode)
