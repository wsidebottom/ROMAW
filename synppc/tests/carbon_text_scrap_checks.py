#!/usr/bin/env python3
# +===================================================================
#   File:      carbon_text_scrap_checks.py
#
#   Summary:   Add native and saved-library text conversion and scrap cases to the Toolbox report.
#
#   Classes:   None.
#
#   Functions: international_names, international_operations, text_scrap_rows, text_scrap_sections
#
#   Origin:    Mac OS 9.2.1 Text Encoding Converter, CarbonLib 1.4 Scrap Manager and Universal Interfaces 3.4.2.
#
#   Copyright and Legal notices: See synppc/COPYING and carbonlib/COPYING.
# ===================================================================+

import re
import struct


def international_names(root):
    source = (root / "carbonlib/src/international_utilities.h").read_text()
    return re.findall(r"^CARBON_INTL\((\w+),", source, re.M)


def international_operations(probe):
    p, ptr = probe.p, probe.ptr
    a = probe.raw(b"\x05Alpha")
    b = probe.raw(b"\x04Beta")
    accent = probe.raw(b"\x01\x8e")
    plain = probe.raw(b"\x01E")
    _, record = probe.raw(bytes(32))
    _, first = probe.raw(bytes(256))
    _, second = probe.raw(bytes(256))
    seconds = 2082844800 + 13 * 3600 + 14 * 60 + 15
    long_date = probe.raw(struct.pack(">Q", seconds))

    def check(name, args, expected):
        probe.call(name, args)
        p.expect(expected, name + " returns the expected signed result")

    check("CompareString", [a, b, 0], -1)
    check("CompareString", [b, a, 0], 1)
    check("CompareText", [ptr(a[1] + 1), ptr(b[1] + 1), 5, 4, 0], -1)
    check("CompareText", [ptr(b[1] + 1), ptr(a[1] + 1), 4, 5, 0], 1)
    check("IdenticalString", [accent, plain, 0], 0)
    check("IdenticalString", [a, b, 0], 1)
    check("IdenticalText", [ptr(accent[1] + 1), ptr(plain[1] + 1), 1, 1, 0], 0)
    check("IdenticalText", [ptr(b[1] + 1), ptr(a[1] + 1), 4, 5, 0], 1)
    check("IdenticalText", [0, 0, -1, 0, 0], 0)
    check("StringOrder", [a, b, 0, 0, 0, 0], -1)
    check("StringOrder", [a, b, 1, 0, 0, 0], 1)
    check("TextOrder", [ptr(a[1] + 1), ptr(b[1] + 1), 5, 4, 0, 0, 0, 0], -1)
    check("TextOrder", [ptr(a[1] + 1), ptr(b[1] + 1), 5, 4, 1, 0, 0, 0], 1)
    check("ScriptOrder", [-2, 0], 0)
    check("ScriptOrder", [0, 1], -1)
    check("LanguageOrder", [0, 0], 0)
    check("LanguageOrder", [0, 1], -1)

    for regular, extended, option in (("DateString", "LongDateString", 0), ("TimeString", "LongTimeString", 1)):
        probe.call(regular, [seconds, option, ptr(first), 0])
        probe.call(extended, [long_date, option, ptr(second), 0])
        p.emit(0x887E0000 | first)
        p.nonzero(regular + " produces a localized Pascal string")
        for offset in range(0, 16, 4):
            p.emit(0x807E0000 | first + offset, 0x809E0000 | second + offset)
            p.equal(regular + " agrees with its 64-bit counterpart")

    probe.call("GetIntlResource", [0])
    p.nonzero("GetIntlResource returns the installed date locale")
    p.emit(0x907E0000 | record)
    probe.call("GetIntlResource", [0])
    p.emit(0x809E0000 | record)
    p.equal("GetIntlResource shares the installed Resource Manager handle")
    for offset in (0, 4, 8):
        p.emit(0x3860FFFF, 0x907E0000 | record + offset)
    probe.call("GetIntlResourceTable", [0, -1, ptr(record), ptr(record + 4), ptr(record + 8)])
    for offset in (0, 4, 8):
        p.emit(0x807E0000 | record + offset)
        p.expect(0, "unavailable international table clears the requested output")


def text_scrap_rows(root):
    text = (root / "carbonlib/src/text_conversion.h").read_text()
    tec = (root / "carbonlib/src/encoding_converter.h").read_text()
    scrap = (root / "carbonlib/src/carbon_scrap.h").read_text()
    return (
        [
            {"name": name, "original_library": module}
            for name, module in re.findall(r"^CARBON_TEXT\((\w+), (\w+),", text, re.M)
        ]
        + [
            {"name": name, "original_library": "TextCommon" if name == "TECGetInfo" else "TextEncodingConverter"}
            for name in re.findall(r"^CARBON_TEC\((\w+),", tec, re.M)
        ]
        + [{"name": name} for name in re.findall(r"^CARBON_SCRAP\((\w+),", scrap, re.M)]
    )


def text_scrap_sections(probe):
    p = probe.p
    _, base = probe.raw(bytes(320))
    _, mac = probe.raw(bytes.fromhex("636166b063616665"))
    _, mac_pascal = probe.raw(bytes.fromhex("04636166b00000000463616665"))
    _, utf16 = probe.raw(bytes.fromhex("006300610066221e0063006100660065"))
    _, mapping = probe.raw(struct.pack(">3I", 0x20104, 0, 0xFFFFFFFF))
    _, utf8_mapping = probe.raw(struct.pack(">3I", 0x08020104, 0, 0xFFFFFFFF))
    _, utf8_source = probe.raw(b"cafe")
    _, utf8_accent = probe.raw(b"caf\xc3\xa9")
    _, thai_mapping = probe.raw(struct.pack(">3I", 0x100, 0x508, 0xFFFFFFFF))
    _, missing_mapping = probe.raw(struct.pack(">3I", 0x100, 0x0FFF, 0xFFFFFFFF))
    _, thai_text = probe.raw(b"\xa1")
    _, thai_unicode = probe.raw(b"\x0e\x01")
    _, offsets = probe.raw(struct.pack(">2I", 0, 3))
    _, unicode_offsets = probe.raw(struct.pack(">2I", 0, 6))
    _, unrepresentable = probe.raw(b"\x26\x03")  # U+2603 SNOWMAN
    _, encodings = probe.raw(struct.pack(">I", 0x508))
    ptr, slot = probe.ptr, probe.slot

    def call(name, args=(), status=0, side=0):
        probe.call(name, args, side=side)
        p.expect(status, name + " returns the expected result")

    def read(offset):
        p.emit(0x807E0000 | (base + offset))

    def save(offset):
        p.emit(0x907E0000 | (base + offset))

    def put(offset, value):
        probe.arguments([value], 0)
        save(offset)

    def check(offset, result, label):
        read(offset)
        p.expect(result, label)

    def compare(first, second, label):
        read(first)
        p.emit(0x809E0000 | base + second)
        p.equal(label)

    def convert(to_unicode, side, by_mapping):
        source = slot(base)
        text_bytes = mac if by_mapping else mac + 4
        pascal_bytes = mac_pascal if by_mapping else mac_pascal + 8
        unicode_bytes = utf16 if by_mapping else utf16 + 8
        last_byte = 0xB0 if by_mapping else 0x65
        last_unit = 0x221E if by_mapping else 0x0065
        if to_unicode:
            out = base + 32
            call(
                "ConvertFromTextToUnicode",
                [
                    source,
                    4,
                    ptr(text_bytes),
                    0,
                    2,
                    ptr(offsets),
                    ptr(base + 4),
                    ptr(base + 12),
                    16,
                    ptr(base + 20),
                    ptr(base + 24),
                    ptr(out),
                ],
                side=side,
            )
            check(4, 2, "both input offsets have Unicode offsets")
            check(12, 0, "first Unicode offset")
            check(16, 6, "the fourth text byte starts at Unicode byte six")
            check(20, 4, "conversion consumes the complete MacRoman string")
            check(24, 8, "conversion produces four UTF-16 units")
            check(32, 0x00630061, "converted first Unicode pair")
            check(36, 0x00660000 | last_unit, "converted final Unicode pair")
            call("ConvertFromPStringToUnicode", [source, ptr(pascal_bytes), 8, ptr(base + 24), ptr(out)], side=side)
            check(24, 8, "Pascal MacRoman conversion reports byte count")
            check(36, 0x00660000 | last_unit, "Pascal conversion produces the same final Unicode pair")
            call("ResetTextToUnicodeInfo", [source], side=side)
        else:
            call("SetFallbackUnicodeToText", [source, 0, 0, 0], side=side)
            call("ConvertFromUnicodeToPString", [source, 8, ptr(unicode_bytes), ptr(base + 64)], side=side)
            check(64, 0x04636166, "Unicode conversion writes a Pascal length and prefix")
            p.emit(0x887E0000 | base + 68)
            p.expect(last_byte, "Unicode converts to the original final text byte")
            call("ResetUnicodeToTextInfo", [source], side=side)
            args = [
                source,
                8,
                ptr(unicode_bytes),
                0,
                2,
                ptr(unicode_offsets),
                ptr(base + 4),
                ptr(base + 12),
                8,
                ptr(base + 20),
                ptr(base + 24),
                ptr(base + 72),
            ]
            call("ConvertFromUnicodeToText", args, side=side)
            check(4, 2, "both Unicode offsets have text offsets")
            check(12, 0, "first byte of the first converted character")
            check(16, 3, "the fourth UTF-16 unit starts at text byte three")
            check(20, 8, "reverse conversion reads all four UTF-16 units")
            check(24, 4, "reverse conversion reports four output bytes")
            check(72, 0x63616600 | last_byte, "reverse conversion writes the exact text bytes")

            args[3] = 2
            call("ConvertFromUnicodeToText", args, side=side)
            check(72, 0x63616600 | last_byte, "continuing a UTF-16 conversion preserves text mapping")

            args[3] = 0
            args[8] = 0
            call("ConvertFromUnicodeToText", args, -8750, side=side)
            check(20, 0, "too-small output does not consume input")
            check(24, 0, "too-small output writes no bytes")

    def text_common():
        probe.call("GetApplicationTextEncoding", [])
        save(0)
        probe.call("GetApplicationTextEncoding", [], side=1)
        p.emit(0x809E0000 | base)
        p.equal("application encoding agrees with the saved CarbonLib cache")
        probe.call("GetApplicationTextEncoding", [])
        p.emit(0x809E0000 | base)
        p.equal("subsequent calls reuse the same application encoding")

        for encoding in (0, 0x100, 0x08000100, 0xA04, 7, 0x02010600):
            probe.call("ResolveDefaultTextEncoding", [encoding])
            save(0)
            probe.call("ResolveDefaultTextEncoding", [encoding], side=1)
            p.emit(0x809E0000 | base)
            p.equal(f"default encoding {encoding:#x} matches TextCommon")

        for side in (0, 1):
            call("NearestMacTextEncodings", [0, 0, 0], -50, side)
            call("NearestMacTextEncodings", [0x100, ptr(base + 4), ptr(base + 8)], -50, side)
            check(4, 0xFFFF, "Unicode has no nearest Mac encoding")
            check(8, 0xFFFF, "Unicode has no alternate Mac encoding")

        for encoding in (0, 0x201):
            for side in (0, 1):
                out = 16 + 16 * side
                probe.call("NearestMacTextEncodings", [encoding, ptr(base + out + 4), ptr(base + out + 8)], side=side)
                save(out)
            for offset in (0, 4, 8):
                compare(16 + offset, 32 + offset, f"nearest Mac encoding {encoding:#x} matches TextCommon")

        for name_part in (0, 2):
            for side in (0, 1):
                out = 16 + 32 * side
                for offset in (4, 8, 12, 16, 20):
                    put(out + offset, 0x5A5A5A5A)
                probe.call(
                    "GetTextEncodingName",
                    [0, name_part, 0, 0xFFFF, 5, *(ptr(base + out + n) for n in (4, 8, 12, 16))],
                    side=side,
                )
                save(out)
            for offset in (0, 4, 8, 12, 16, 20):
                compare(16 + offset, 48 + offset, f"encoding name part {name_part} matches TextCommon")

    def script_iso():
        for side in (0, 1):
            call("MapScriptInfoAndISOCodes", [0, 0, 0, 0, 1], side=side)
            call("MapScriptInfoAndISOCodes", [ptr(base), 0, 0, 0, 1], -50, side)
            call("MapScriptInfoAndISOCodes", [0, 0, ptr(base), 0, 0], -50, side)
            call("MapScriptInfoAndISOCodes", [0, 0, 0, ptr(base), 0], side=side)

        for to_iso, first, second in (
            (1, 0, 0),
            (1, 1, 11),
            (1, 29, 0xFF80),
            (1, 0xFF80, 0xFF80),
            (0, 0x6C61, 0x656E),
            (0, 0x7A7A, 0x2A2A),
            (0, 0x2A2A, 0x2A2A),
        ):
            for side in (0, 1):
                out = 16 + 24 * side
                put(out + 4, (first << 16) | second)
                put(out + 8, 0x5A5A5A5A)
                inputs = [ptr(base + out + 4), ptr(base + out + 6)]
                outputs = [ptr(base + out + 8), ptr(base + out + 10)]
                probe.call(
                    "MapScriptInfoAndISOCodes", (inputs + outputs if to_iso else outputs + inputs) + [to_iso], side=side
                )
                save(out)
            for offset in (0, 4, 8):
                compare(16 + offset, 40 + offset, "script/ISO mapping status and words match TextCommon")

    def script_encodings():
        _, native_name = probe.raw(bytes(256))
        _, saved_name = probe.raw(bytes(256))
        geneva = probe.raw(b"\x06Geneva")
        missing_font = probe.raw(b"\x0eNoSuchFontName")

        for encoding in (0, 0x00020007, 0x00030007, 0x10025, 1, 0x501, 0xFFFF):
            for side, name in enumerate((native_name, saved_name)):
                out = 16 + 16 * side
                put(out + 4, 0x5A5A5A5A)
                probe.arguments([0x5A5A5A5A], 0)
                p.emit(0x907E0000 | name)
                probe.call(
                    "RevertTextEncodingToScriptInfo",
                    [encoding, ptr(base + out + 4), ptr(base + out + 6), ptr(name)],
                    side=side,
                )
                save(out)
            compare(16, 32, f"revert {encoding:#x} returns the saved status")
            compare(20, 36, f"revert {encoding:#x} yields the saved script and language")
            for offset in (0, 4, 8, 12):
                p.emit(0x807E0000 | native_name + offset, 0x809E0000 | saved_name + offset)
                p.equal(f"revert {encoding:#x} yields the saved font prefix")

        for script, language, region, font in (
            (0, -128, -128, 0),
            (-1, -128, -128, 0),
            (-128, 0, 0, 0),
            (0, 0, 0, geneva),
            (0, 0, 0, missing_font),
            (1, 11, -128, 0),
            (-128, -128, -128, 0),
            (99, 0, 0, 0),
            (0, 11, 0, 0),
        ):
            for side in (0, 1):
                out = 16 + 16 * side
                put(out + 4, 0x5A5A5A5A)
                probe.call(
                    "UpgradeScriptInfoToTextEncoding", [script, language, region, font, ptr(base + out + 4)], side=side
                )
                save(out)
            compare(16, 32, "upgrade status matches TextCommon")
            compare(20, 36, "upgrade output or untouched sentinel matches TextCommon")

    def character_properties():
        latin = probe.raw(b"\x00\x41")
        combining = probe.raw(b"\x03\x01")
        arabic = probe.raw(b"\x06\x27")
        probe.call("NewPtr", [0x510])
        p.nonzero("property lookup scratch table allocated")
        save(120)

        def store_table(value, opcode, offset):
            probe.arguments([value], 0)
            p.emit(0x809E0000 | base + 120, opcode | offset)

        store_table(0x8000, 0xB0640000, 0x10)
        store_table(0x4321, 0xB0640000, 0x10 + 6 * 2)
        for width in (1, 2):
            store_table(width, 0x90640000, 0)
            if width == 1:
                store_table(0xAB, 0x98640000, 0x410 + 0x41)
            else:
                store_table(0xFEDC, 0xB0640000, 0x410 + 0x41 * 2)
            for side in (0, 1):
                call("UCUseCharPropsTablePriv", [latin, 1, slot(base + 120)], 0xAB if width == 1 else 0xFEDC, side)
                call("UCUseCharPropsTablePriv", [combining, 1, slot(base + 120)], 0x4321, side)
        probe.call("DisposePtr", [slot(base + 120)])

        for text, property_kind in ((latin, 1), (combining, 2), (arabic, 3), (latin, -1), (latin, 100)):
            for side in (0, 1):
                out = 16 + 16 * side
                put(out + 4, 0x5A5A5A5A)
                probe.call("UCGetCharProperty", [text, 1, property_kind, ptr(base + out + 4)], side=side)
                save(out)
            compare(16, 32, "character property query returns the saved status")
            compare(20, 36, "character property query writes the saved value")

        for side in (0, 1):
            put(16, 0x5A5A5A5A)
            call("UCGetCharProperty", [latin, 0, 1, ptr(base + 16)], -50, side)
            check(16, 0, "zero-length property query clears output")
            call("UCLoadCharPropsTablePriv", [1, 0], -50, side)
        for property_kind in (1, -1):
            for side in (0, 1):
                out = 16 + side * 16
                put(out + 4, 0x5A5A5A5A)
                probe.call("UCLoadCharPropsTablePriv", [property_kind, ptr(base + out + 4)], side=side)
                save(out)
                probe.call("UCUnloadCharPropsTablePriv", [property_kind, ptr(base + out + 4)], side=side)
                save(out + 8)
                check(out + 4, 0, "unloading a property clears the table handle slot")
            compare(16, 32, "property load matches TextCommon")
            compare(24, 40, "property unload matches TextCommon")

    def lock_tables():
        for descriptor, expected in ((ptr(mapping), 0), (ptr(missing_mapping), -8738)):
            for side in (0, 1):
                call("LockMappingTable", [descriptor], expected, side)
        for constructor, locker, disposer in (
            ("CreateTextToUnicodeInfo", "LockTextToUnicodeInfo", "DisposeTextToUnicodeInfo"),
            ("CreateUnicodeToTextInfo", "LockUnicodeToTextInfo", "DisposeUnicodeToTextInfo"),
        ):
            for side in (0, 1):
                call(constructor, [ptr(mapping), ptr(base)], side=side)
                call(locker, [slot(base)], side=side)
                call(disposer, [ptr(base)], side=side)
                check(0, 0, "locked converter disposal clears the handle")

    def text():
        for symbol, value in (
            ("GetTextEncodingBase", 0x0234),
            ("GetTextEncodingVariant", 0x01C7),
            ("GetTextEncodingFormat", 10),
        ):
            for side in (0, 1):
                probe.call("CreateTextEncoding", [0x10234, 0x5C7, 0x1A], side=side)
                p.expect(0x29C70234, "TextEncoding fields are packed into the documented bits")
                call(symbol, [0x29C70234], value, side)
        # Alternate which implementation creates and operates the live converter.
        for to_unicode, by_mapping, by_encoding, disposer in (
            (True, "CreateTextToUnicodeInfo", "CreateTextToUnicodeInfoByEncoding", "DisposeTextToUnicodeInfo"),
            (False, "CreateUnicodeToTextInfo", "CreateUnicodeToTextInfoByEncoding", "DisposeUnicodeToTextInfo"),
        ):
            for creator, argument in ((by_mapping, ptr(mapping)), (by_encoding, 0x508)):
                for side in (1, 0):
                    call(creator, [argument, ptr(base)], side=side)
                    read(0)
                    p.nonzero("converter constructor returns a live handle")
                    convert(to_unicode, 1 - side, creator == by_mapping)
                    call(disposer, [ptr(base)], side=side)
                    check(0, 0, "disposal clears the caller's converter handle")
        for side in (1, 0):
            call("CreateUnicodeToTextRunInfoByEncoding", [1, ptr(encodings), ptr(base)])
            call("SetFallbackUnicodeToTextRun", [slot(base), 0, 0, 0])
            call("ResetUnicodeToTextRunInfo", [slot(base)])
            call("DisposeUnicodeToTextRunInfo", [ptr(base)], side=side)
            check(0, 0, "run disposal clears the caller's handle")
        call("ConvertFromTextToUnicode", [0, 0, 0, 0x80000000, 0, 0, 0, 0, 0, ptr(base + 20), ptr(base + 24), 0], -50)
        check(20, 0, "failed conversion clears the bytes consumed")
        check(24, 0, "failed conversion clears the bytes written")

        _, fallback = probe.raw(bytes(8))
        _, replacement = probe.raw(b"!")
        p.routines.append(
            (fallback, [0x90850000, 0x88090000, 0x98060000, 0x38000001, 0x90080000, 0x38600000, 0x4E800020])
        )
        probe.call("NewRoutineDescriptor", [ptr(fallback), 0x003FFFF0, 1])
        p.nonzero("allocate Unicode fallback descriptor")
        save(212)
        for options in (0, 1, 5):
            for side in (1, 0):
                out = 144 + 32 * side
                call("CreateUnicodeToTextInfo", [ptr(mapping), ptr(base)], side=side)
                if options == 5:
                    call("SetFallbackUnicodeToText", [slot(base), slot(base + 212), 5, ptr(replacement)], side=side)
                put(out + 12, 0x5A5A5A5A)
                probe.call(
                    "ConvertFromUnicodeToText",
                    [
                        slot(base),
                        2,
                        ptr(unrepresentable),
                        options & 1,
                        0,
                        0,
                        0,
                        0,
                        4,
                        ptr(base + out + 4),
                        ptr(base + out + 8),
                        ptr(base + out + 12),
                    ],
                    side=side,
                )
                save(out)
                if options == 5:
                    check(out + 4, 2, "custom fallback consumes one UTF-16 unit")
                    check(out + 8, 1, "custom fallback produces one text byte")
                    check(out + 12, 0x215A5A5A, "custom fallback writes its refcon byte")
                call("DisposeUnicodeToTextInfo", [ptr(base)], side=side)
            for n in range(0, 16, 4):
                compare(144 + n, 176 + n, "unmappable Unicode matches the saved converter")
        probe.call("DisposeRoutineDescriptor", [slot(base + 212)])

        for utf8, size in ((utf8_source, 4), (utf8_accent, 5)):
            for options in (0, 2):
                for side in (1, 0):
                    out = 144 + 32 * side
                    call("CreateUnicodeToTextInfo", [ptr(utf8_mapping), ptr(base)], side=side)
                    for n in range(0, 28, 4):
                        put(out + n, 0x5A5A5A5A)
                    probe.call(
                        "ConvertFromUnicodeToText",
                        [
                            slot(base),
                            size,
                            ptr(utf8),
                            options,
                            2,
                            ptr(offsets),
                            ptr(base + out + 4),
                            ptr(base + out + 8),
                            4,
                            ptr(base + out + 16),
                            ptr(base + out + 20),
                            ptr(base + out + 24),
                        ],
                        side=side,
                    )
                    save(out)
                    call("DisposeUnicodeToTextInfo", [ptr(base)], side=side)
                for n in range(0, 28, 4):
                    compare(144 + n, 176 + n, "UTF-8 adapter matches the saved result")

    def converter():
        _, path = probe.raw(struct.pack(">2I", 0x08000104, 0x104))
        _, source = probe.raw(b"caf\xc3\xa9")
        _, internet = probe.raw(b"\x05UTF-8")
        for count, listing, words in (
            ("TECCountAvailableTextEncodings", "TECGetAvailableTextEncodings", 4),
            ("TECCountDirectTextEncodingConversions", "TECGetDirectTextEncodingConversions", 12),
            ("TECCountAvailableSniffers", "TECGetAvailableSniffers", 4),
        ):
            for side in (1, 0):
                out = side * 64
                probe.call(count, [ptr(base + out)], side=side)
                save(out + 4)
                for n in range(12, 12 + words * 4, 4):
                    put(out + n, 0x5A5A5A5A)
                probe.call(listing, [ptr(base + out + 12), 4, ptr(base + out + 8)], side=side)
                save(out + 60)
            for n in (0, 4, 8, 60, *range(12, 12 + words * 4, 4)):
                compare(n, n + 64, listing + " matches the saved catalogue")
            if count != "TECCountAvailableSniffers":
                read(0)
                p.nonzero(count + " reports installed plugin entries")

        for side in (1, 0):
            call("TECGetTextEncodingFromInternetName", [ptr(base + 4), ptr(internet)], side=side)
            check(4, 0x08000104, "UTF-8 Internet alias maps to its encoding")
            call("TECGetTextEncodingInternetName", [0x08000104, ptr(base + 32)], side=side)
            check(32, 0x05555446, "UTF-8 canonical Internet name begins with the Pascal prefix")
            call("TECConvertText", [0, 0, 0, ptr(base + 4), 0, 0, ptr(base + 8)], -8748, side=side)
            check(4, 0, "invalid converter clears the input count")
            check(8, 0, "invalid converter clears the output count")

        # Supply a converter header with a borrowed list handle so the two
        # implementations can exercise the accessor without a plugin factory.
        put(128, ptr(base + 160))
        put(160, 0x656E6376)
        put(180, 2)
        put(184, ptr(base + 132))
        for side in (1, 0):
            call("TECGetEncodingList", [ptr(base + 128), ptr(base + 4), ptr(base + 8)], side=side)
            check(4, 2, "converter returns two encodings from its header")
            probe.arguments([ptr(base + 132), slot(base + 8)], side)
            p.equal("converter returns its original list handle")

        context = slot(base)
        stream = [context, ptr(source), 5, ptr(base + 4), ptr(base + 40), 32, ptr(base + 8)]
        for creator, args in (
            ("TECCreateConverter", [ptr(base), 0x08000104, 0x104]),
            ("TECCreateConverterFromPath", [ptr(base), ptr(path), 2]),
        ):
            for side in (1, 0):
                call(creator, args, side=side)
                call("TECConvertText", stream, side=1 - side)
                check(4, 5, "UTF-8 conversion consumes all five bytes")
                check(8, 8, "conversion writes four UTF-16 code units")
                check(40, 0x00630061, "first UTF-16 pair")
                check(44, 0x006600E9, "second UTF-16 pair")
                call("TECFlushText", [context, ptr(base + 40), 32, ptr(base + 8)], side=side)
                check(8, 0, "UTF-8 conversion left no buffered data")
                call("TECClearConverterContextInfo", [context], side=side)
                call("TECGetEncodingList", [context, ptr(base + 4), ptr(base + 8)], -50, side=side)
                check(8, 0, "serial converter has no encoding list")
                call("TECDisposeConverter", [context], side=1 - side)

    def multi_converter_and_info():
        _, destinations = probe.raw(struct.pack(">I", 0x508))
        for side in (1, 0):
            put(0, 0x5A5A5A5A)
            call("TECCreateOneToManyConverter", [ptr(base), 0x100, 0, ptr(destinations)], -50, side=side)
            check(0, 0x5A5A5A5A, "zero destinations leave the output untouched")
            call("TECCreateOneToManyConverter", [ptr(base), 0x100, 1, ptr(destinations)], side=side)
            context = slot(base)
            call("TECGetEncodingList", [context, ptr(base + 4), ptr(base + 8)], side=side)
            check(4, 1, "a multi-converter retains one destination encoding")
            read(8)
            p.emit(0x80630000, 0x80630000)
            p.expect(0x508, "the converter preserves the requested output encoding")
            stream = [
                context,
                ptr(utf16 + 8),
                8,
                ptr(base + 12),
                ptr(base + 32),
                16,
                ptr(base + 16),
                ptr(base + 64),
                2,
                ptr(base + 20),
            ]
            call("TECConvertTextToMultipleEncodings", stream, side=side)
            check(12, 8, "multi-conversion consumes four UTF-16 units")
            check(16, 4, "multi-conversion writes four destination bytes")
            check(32, 0x63616665, "multi-conversion writes ASCII text")
            check(20, 1, "multi-conversion emits one run")
            call(
                "TECFlushMultipleEncodings",
                [context, ptr(base + 32), 16, ptr(base + 16), ptr(base + 64), 2, ptr(base + 20)],
                side=side,
            )
            check(16, 0, "multi-converter flush leaves no pending ASCII")
            check(20, 0, "empty flush writes no runs")
            call("TECDisposeConverter", [context], side=1 - side)

        for side in (1, 0):
            call("TECGetInfo", [0], side=side)
            call("TECGetInfo", [ptr(base + 224)], side=side)
            read(224)
            p.nonzero("TECGetInfo allocates a caller-owned handle")
            p.emit(0x80630000)
            save(228)
            probe.call("BlockMoveData", [slot(base + 228), ptr(base + 96 * side), 84])
            probe.call("DisposeHandle", [slot(base + 224)])
        for n in range(0, 84, 4):
            compare(n, n + 96, "TECGetInfo preserves the saved version record")
        check(0, 0x00020160, "TECGetInfo reports its layout and converter version")
        check(8, 0xFF, "TECGetInfo publishes the installed capability bits")

    def catalogue_and_sniffers():
        for count, listing, keys in (
            ("TECCountSubTextEncodings", "TECGetSubTextEncodings", (0x100, 0x0FFF)),
            ("TECCountMailTextEncodings", "TECGetMailTextEncodings", (0, -1)),
            ("TECCountWebTextEncodings", "TECGetWebTextEncodings", (0, -1)),
            ("TECCountDestinationTextEncodings", "TECGetDestinationTextEncodings", (0x08000104, 0x0FFF)),
        ):
            for encoding in keys:
                for side in (1, 0):
                    out = 64 * side
                    probe.call(count, [encoding, ptr(base + out + 4)], side=side)
                    save(out)
                    for n in range(12, 32, 4):
                        put(out + n, 0x5A5A5A5A)
                    probe.call(listing, [encoding, ptr(base + out + 12), 4, ptr(base + out + 8)], side=side)
                    save(out + 32)
                for n in range(0, 36, 4):
                    compare(n, n + 64, listing + " count, status and bounded entries match the saved catalogue")
                check(28, 0x5A5A5A5A, listing + " preserves the output guard")
                if encoding == 0x08000104 and count == "TECCountDestinationTextEncodings":
                    check(0, 0, "installed UTF-8 plugins advertise direct destinations")
                    read(4)
                    p.nonzero("the UTF-8 destination catalogue is populated")

        _, candidates = probe.raw(struct.pack(">2I", 0x08000104, 0x0FFF))
        _, sample = probe.raw(b"cafe")
        _, clear_method = probe.raw(bytes(8))
        _, sample_method = probe.raw(bytes(8))
        _, dispose_method = probe.raw(bytes(8))
        p.routines.extend(
            (
                (clear_method, [0x38000007, 0x90040034, 0x38600000, 0x4E800020]),
                (
                    sample_method,
                    [
                        0x80A40010,
                        0x88A50000,
                        0x8004000C,
                        0x7C002A14,
                        0x90040018,
                        0x80040008,
                        0x9004001C,
                        0x38600000,
                        0x4E800020,
                    ],
                ),
                (dispose_method, [0x38600000, 0x4E800020]),
            )
        )
        put(104, ptr(base + 240))
        put(276, ptr(clear_method))
        put(280, ptr(sample_method))
        put(284, ptr(dispose_method))
        for side in (1, 0):
            out = 144 + side * 48
            put(96, 0)
            call("TECCreateSniffer", [ptr(base + 96), ptr(candidates), 2], side=1 - side)
            read(96)
            p.nonzero("sniffer creation returns a context")
            context = slot(base + 96)
            # Install a test plugin in the second slot. The installed profile
            # does not ship a sniffer for either of these encodings.
            probe.arguments([context, ptr(base + 104)], side)
            p.emit(0x80630000, 0x90830050)
            call("TECClearSnifferContextInfo", [context], side=side)
            read(96)
            p.emit(0x80630000, 0x80630084)
            p.expect(7, "sniffer reset calls the installed plugin")
            sniff = [
                context,
                ptr(sample),
                4,
                ptr(base + out + 4),
                2,
                ptr(base + out + 12),
                100,
                ptr(base + out + 20),
                100,
            ]
            probe.call("TECSniffTextEncoding", sniff, side=side)
            save(out)
            check(out + 12, 100, "ranked sniff output carries the plugin error score")
            check(out + 16, -1, "the unsupported candidate follows the plugin result")
            check(out + 20, 199, "the callback receives the sample and stack argument")
            sniff[4] = 1
            call("TECSniffTextEncoding", sniff, -50, side=side)
            call("TECDisposeSniffer", [context], side=side)
            call("TECCreateSniffer", [ptr(base + 96), ptr(candidates), 0], -50, side=side)
            call("TECDisposeSniffer", [0], side=side)
            call("TECClearSnifferContextInfo", [0], -50, side=side)
        for n in range(0, 28, 4):
            compare(144 + n, 192 + n, "sniffer scores and ranked encodings agree with the saved fragment")

    def run_constructors():
        pair = probe.far_raw(struct.pack(">6I", 0x100, 0x508, 0xFFFFFFFF, 0x20104, 0, 0xFFFFFFFF))
        bad_pair = probe.far_raw(struct.pack(">6I", 0x100, 0x508, 0xFFFFFFFF, 0x100, 0x0FFF, 0xFFFFFFFF))
        scripts = probe.far_raw(struct.pack(">h", 0))

        for creator, count, source in (
            ("CreateUnicodeToTextRunInfo", 2, pair),
            ("CreateUnicodeToTextRunInfoByEncoding", 1, ptr(encodings)),
        ):
            for side in (1, 0):
                start = side * 128
                call(creator, [count, source, ptr(base)], side=side)
                read(0)
                p.nonzero("the run constructor publishes an owned handle")
                p.emit(0x80630000, 0x80630110)
                p.expect(count, "the run contains every requested mapping")
                call("ResetUnicodeToTextRunInfo", [slot(base)], side=1 - side)
                args = [
                    slot(base),
                    8,
                    ptr(utf16 + 8),
                    0,
                    0,
                    0,
                    0,
                    0,
                    16,
                    ptr(base + start + 20),
                    ptr(base + start + 24),
                    ptr(base + start + 28),
                    2,
                    ptr(base + start + 44),
                    ptr(base + start + 48),
                ]
                call("ConvertFromUnicodeToTextRun", args, side=side)
                check(start + 20, 8, "run conversion consumes all Unicode input")
                check(start + 24, 4, "run conversion produces four bytes")
                check(start + 28, 0x63616665, "run conversion preserves the ASCII text")
                check(start + 44, 1, "conversion returns one encoding run")
                call("DisposeUnicodeToTextRunInfo", [ptr(base)], side=1 - side)
                check(0, 0, "run disposal clears the owned handle")
            compare(52, 180, "the native run chooses the same encoding as the saved constructor")

        call("CreateUnicodeToTextRunInfo", [2, bad_pair, ptr(base)], -8738)
        check(0, 0, "the run constructor rolls back a partially loaded mapping list")
        for side in (1, 0):
            # The legacy script preference path depends on the installed tables.
            probe.call("CreateUnicodeToTextRunInfoByScriptCode", [1, scripts, ptr(base)], side=side)
            save(80 + 128 * side)
            read(0)
            p.emit(0x2C030000, 0)
            branch = len(p.code) - 1
            call("DisposeUnicodeToTextRunInfo", [ptr(base)], side=side)
            p.code[branch] = 0x41820000 | (len(p.code) - branch) * 4
        compare(80, 208, "script conversion selects the same mapping status as the saved constructor")

        mac_roman = ptr(mapping)
        for api, source, flags, capacity, offset_table, expected in (
            ("ConvertFromUnicodeToTextRun", mac_roman, 0, 2, ptr(unicode_offsets), None),
            ("ConvertFromUnicodeToTextRun", mac_roman, 0x80, 16, ptr(offsets), None),
            ("ConvertFromUnicodeToTextRun", mac_roman, 0x100, 16, ptr(offsets), -50),
            ("ConvertFromUnicodeToTextRun", mac_roman, 0, 16, ptr(unicode_offsets), 0),
            ("ConvertFromUnicodeToScriptCodeRun", mac_roman, 0, 16, ptr(unicode_offsets), 0),
            ("ConvertFromUnicodeToScriptCodeRun", ptr(thai_mapping), 0, 16, ptr(unicode_offsets), -8738),
        ):
            for side in (1, 0):
                start = side * 128
                call("CreateUnicodeToTextRunInfo", [1, source, ptr(base)], side=1)
                for offset in range(20, 80, 4):
                    put(start + offset, 0xA55AA55A)
                probe.call(
                    api,
                    [
                        slot(base),
                        8,
                        ptr(utf16 + 8),
                        flags,
                        2,
                        offset_table,
                        ptr(base + start + 56),
                        ptr(base + start + 60),
                        capacity,
                        ptr(base + start + 20),
                        ptr(base + start + 24),
                        ptr(base + start + 28),
                        2,
                        ptr(base + start + 44),
                        ptr(base + start + 48),
                    ],
                    side=side,
                )
                save(start + 80)
                if expected is not None:
                    check(start + 80, expected, "run status follows the requested options and encoding")
                if expected == 0:
                    check(start + 28, 0x63616665, "converted run holds the expected text")
                    check(start + 44, 1, "converted text occupies one run")
                    if api == "ConvertFromUnicodeToScriptCodeRun":
                        check(start + 52, 0x0000A55A, "script runs use six packed bytes")
                call("DisposeUnicodeToTextRunInfo", [ptr(base)], side=1)
            for offset in range(20, 84, 4):
                compare(offset, offset + 128, api + " matches saved output, offsets, status, and untouched bytes")

    def mapping_queries():
        for mask, pattern in ((0, 0), (1 | 8, ptr(mapping)), (1 | 4 | 8, ptr(utf8_mapping))):
            for side in (1, 0):
                start = side * 128
                for offset in range(0, 24, 4):
                    put(start + offset, 0xA55A5AA5)
                call("CountUnicodeMappings", [mask, pattern, ptr(base + start + 24)], side=side)
                probe.call(
                    "QueryUnicodeMappings",
                    [mask, pattern, 2, ptr(base + start + 28), ptr(base + start)],
                    side=side,
                )
                save(start + 32)
            for offset in range(0, 36, 4):
                compare(offset, 128 + offset, "mapping query bytes, totals and status match the saved converter")
        call("QueryUnicodeMappings", [0, 0, 0, ptr(base + 28), 0], -8751)
        read(28)
        p.nonzero("zero-capacity Unicode query reports the required entry count")
        call("CountUnicodeMappings", [1, 0, ptr(base + 24)], -50)
        check(24, 0, "an absent filter mapping clears the count")
        length_inputs = probe.far_raw(struct.pack(">2I", 0x508, 0))
        for side in (1, 0):
            start = side * 128
            put(start + 40, 0xA55A5AA5)
            put(start + 44, 0xA55A5AA5)
            probe.call(
                "UnicodeValidConversionLength",
                [8, ptr(utf16), 2, length_inputs, ptr(base + start + 40)],
                side=side,
            )
            save(start + 48)
            call("UnicodeValidConversionLength", [0, 0, 0, 0, 0], side=side)
        compare(40, 168, "valid Unicode length agrees for Thai")
        compare(44, 172, "valid Unicode length agrees for Roman")
        compare(48, 176, "the first unsupported mapping status agrees with the saved converter")

    def change_and_truncate():
        for side in (1, 0):
            call("CreateTextToUnicodeInfo", [ptr(mapping), ptr(base)], side=1 - side)
            to_unicode = slot(base)
            call("ChangeUnicodeToTextInfo", [to_unicode, ptr(mapping)], -50, side=side)
            call("ChangeTextToUnicodeInfo", [to_unicode, ptr(thai_mapping)], side=side)
            call("ChangeTextToUnicodeInfo", [to_unicode, ptr(missing_mapping)], -8738, side=side)
            call(
                "ConvertFromTextToUnicode",
                [to_unicode, 1, ptr(thai_text), 0, 0, 0, 0, 0, 4, ptr(base + 20), ptr(base + 24), ptr(base + 72)],
                side=side,
            )
            check(20, 1, "changing a text converter preserves its handle and consumes one input byte")
            p.emit(0xA07E0000 | (base + 72))
            p.expect(0x00A1, "the replacement table survives a rejected update")
            for limit, expected in ((0, 0), (3, 3), (9, 4)):
                call("TruncateForTextToUnicode", [to_unicode, 4, ptr(mac + 4), limit, ptr(base + 4)], side=side)
                check(4, expected, "truncation respects the input extent and single-byte character boundaries")
            call("DisposeTextToUnicodeInfo", [ptr(base)], side=1 - side)

            call("CreateUnicodeToTextInfo", [ptr(mapping), ptr(base)], side=1 - side)
            to_text = slot(base)
            call("ChangeTextToUnicodeInfo", [to_text, ptr(mapping)], -50, side=side)
            call("ChangeUnicodeToTextInfo", [to_text, ptr(thai_mapping)], side=side)
            call("ChangeUnicodeToTextInfo", [to_text, ptr(missing_mapping)], -8738, side=side)
            for limit, expected in ((0, 0), (1, 0), (3, 2), (9, 8)):
                call("TruncateForUnicodeToText", [to_text, 8, ptr(utf16 + 8), 0, limit, ptr(base + 4)], side=side)
                check(4, expected, "Unicode truncation does not split a UTF-16 code unit")
            call("TruncateForUnicodeToText", [to_text, 8, ptr(utf16 + 8), 0x80, 3, ptr(base + 4)], -50, side=side)
            check(4, 8, "invalid Unicode truncation options leave the output alone")
            call(
                "ConvertFromUnicodeToText",
                [to_text, 2, ptr(utf16 + 8), 2, 0, 0, 0, 0, 4, ptr(base + 20), ptr(base + 24), ptr(base + 72)],
                side=side,
            )
            check(24, 1, "the replacement reverse converter produces one byte")
            p.emit(0x887E0000 | (base + 72))
            p.expect(0x63, "the replacement mapping writes ASCII")
            read(0)
            p.emit(0x80630000, 0x8063011C)
            p.expect(0x508, "the reverse context retains its new encoding after truncation and conversion")
            call("DisposeUnicodeToTextInfo", [ptr(base)], side=1 - side)

        # A converter record with a minimal stateful table. The lead byte
        # enters the trail state without marking a complete character.
        table = bytearray(0x120)
        table[0x10 + 0x81] = 1
        table[0x10 + 0xFF] = 3
        struct.pack_into(">HBB", table, 0x114, 2, 4, 0)
        struct.pack_into(">HBB", table, 0x11C, 0, 2, 0)
        _, transitions = probe.raw(table)
        _, multibyte = probe.raw(b"a\x81\x40b\xff")
        put(128, ptr(base + 160))
        put(132, ptr(transitions))
        put(136, ptr(base + 140))
        put(140, 5)
        put(160, 0x7A3)
        put(192, ptr(base + 132))
        put(196, ptr(base + 136))
        for side in (1, 0):
            for limit, expected in ((1, 1), (2, 1), (3, 3), (4, 4)):
                call("TruncateForTextToUnicode", [ptr(base + 128), 5, ptr(multibyte), limit, ptr(base + 4)], side=side)
                check(4, expected, "stateful input truncation does not split a lead/trail pair")
            call("TruncateForTextToUnicode", [ptr(base + 128), 5, ptr(multibyte), 5, ptr(base + 4)], -8739, side=side)
            check(4, 5, "a bad stateful transition preserves the initial bound")

        for limit in range(1, 6):
            for side in (1, 0):
                call("CreateUnicodeToTextInfo", [ptr(utf8_mapping), ptr(base)], side=side)
                probe.call(
                    "TruncateForUnicodeToText",
                    [slot(base), 5, ptr(utf8_accent), 0, limit, ptr(base + 144 + side * 32)],
                    side=side,
                )
                save(148 + side * 32)
                call("DisposeUnicodeToTextInfo", [ptr(base)], side=side)
            compare(144, 176, "UTF-8 truncation point agrees with the saved converter")
            compare(148, 180, "UTF-8 truncation status agrees with the saved converter")

    def scrap():
        flavor = int.from_bytes(b"ECTS", "big")
        promised = int.from_bytes(b"PRMS", "big")
        _, routine = probe.raw(bytes(8))
        p.routines.append(
            (
                routine,
                [
                    0x81850000,  # Load the saved PutScrapFlavor descriptor.
                    0x80C50004,
                    0x38C60001,
                    0x90C50004,
                    0x38E50008,  # Supply four bytes of sender-only data.
                    0x38A00001,
                    0x38C00004,
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800420,
                ],
            )
        )
        probe.load_oracle_vector(probe.names.index("PutScrapFlavor"), 3)
        save(104)
        put(112, 0x50415353)
        probe.call("NewScrapPromiseKeeperUPP", [ptr(routine)])
        p.nonzero("create the scrap promise keeper")
        save(100)
        for side in (0, 1):
            call("ClearCurrentScrap", side=side)
            call("GetCurrentScrap", [ptr(base)], side=1 - side)
            call("GetScrapFlavorCount", [slot(base), ptr(base + 4)], side=side)
            check(4, 0, "cleared scrap has no flavors")
            call("SetScrapPromiseKeeper", [slot(base), 0, 0], side=side)
            call("PutScrapFlavor", [slot(base), flavor, 1, 4, ptr(mac)], side=side)
            call("GetScrapFlavorCount", [slot(base), ptr(base + 4)], side=1 - side)
            check(4, 1, "saved and native share one sender-only flavor")
            call("GetScrapFlavorFlags", [slot(base), flavor, ptr(base + 8)], side=1 - side)
            check(8, 1, "sender-only ownership is preserved")
            call("GetScrapFlavorSize", [slot(base), flavor, ptr(base + 8)], side=side)
            check(8, 4, "stored flavor size")
            call("GetScrapFlavorInfoList", [slot(base), ptr(base + 4), ptr(base + 12)], side=side)
            check(4, 1, "flavor list count")
            check(12, flavor, "flavor list type")
            check(16, 1, "flavor list flags")
            put(8, 2)
            put(20, 0x5A5A5A5A)
            call("GetScrapFlavorData", [slot(base), flavor, ptr(base + 8), ptr(base + 20)], side=side)
            check(8, 4, "bounded read reports the complete flavor size")
            check(20, 0x63615A5A, "bounded read preserves the output tail")
            put(108, 0)
            call("SetScrapPromiseKeeper", [slot(base), slot(base + 100), ptr(base + 104)], side=side)
            call("PutScrapFlavor", [slot(base), promised, 1, -1, 0], side=1 - side)
            call("GetScrapFlavorSize", [slot(base), promised, ptr(base + 8)], side=side)
            check(108, 1, "first query invokes the promise keeper")
            check(8, 4, "the fulfilled flavor reports its delivered size")
            call("GetScrapFlavorData", [slot(base), promised, ptr(base + 8), ptr(base + 20)], side=side)
            check(108, 1, "reading a fulfilled flavor leaves the keeper idle")
            check(20, 0x50415353, "a saved promise keeper can publish data for native readers")
            call("CallInScrapPromises", side=side)
            call("ClearCurrentScrap", side=1 - side)
            call("GetScrapFlavorSize", [slot(base), flavor, ptr(base + 8)], -4990, side=side)

        probe.call("DisposeRoutineDescriptor", [slot(base + 100)])

    probe.section("Text encoding defaults, equivalents, and names", text_common)
    probe.section("Bidirectional script and ISO codes", script_iso)
    probe.section("Script and text encoding conversion", script_encodings)
    probe.section("Unicode character properties and table ownership", character_properties)
    probe.section("Text encodings and Unicode converter records", text)
    probe.section("Unicode converter run constructors and saved conversions", run_constructors)
    probe.section("Unicode mapping queries and valid conversion lengths", mapping_queries)
    probe.section("Unicode converter mapping changes and truncation", change_and_truncate)
    probe.section("Text Encoding Converter catalogue and streams", converter)
    probe.section("Text Encoding Converter multiple outputs and version", multi_converter_and_info)
    probe.section("Text Encoding Converter labelled catalogues and sniffers", catalogue_and_sniffers)
    probe.section("Unicode converter and mapping locks", lock_tables)
    probe.section("Carbon scrap flavor lifecycle", scrap)
