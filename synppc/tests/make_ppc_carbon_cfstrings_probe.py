#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cfstrings_probe.py
#
#   Summary:   Compare native string operations with un-intercepted local CarbonLib descriptors.
#
#   Classes:   None declared.
#
#   Functions: make_profile, make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Compare native string operations with un-intercepted local CarbonLib descriptors."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "CFStringCompare",
    "CFStringCompareWithOptions",
    "__CFStringMakeConstantString",
    "CFStringCreateCopy",
    "CFStringHasPrefix",
    "CFStringHasSuffix",
    "CFStringFind",
    "CFStringGetIntValue",
)


def make_profile(destination, library, mode):
    destination.mkdir(parents=True, exist_ok=True)
    write_carbon_fallback(destination)
    foreign = mode == "foreign"
    code = [0] * ((0x5DEE0 + (4 if foreign else 0)) // 4)
    data = bytearray(0x72A8)
    offsets = (0x3A880, 0x39FA8, 0x38110, 0x37D08, 0x3B5AC, 0x3B61C, 0x3B4E0, 0x3CFE0)
    marker = [0x3C601234, 0x606356AB, 0x4E800020]
    routines = [(name, offset, marker) for name, offset in zip(NAMES, offsets)]
    for index in (2, 3):
        routines[index] = (NAMES[index], offsets[index], [0x38620900, 0x4E800020])
    routines[6] = (NAMES[6], offsets[6], [0x38000007, 0x90030000, 0x38000002, 0x90030004, 0x4E800020])
    routines += [
        ("StringProbeFirst", 0x100, [0x38620900, 0x4E800020]),
        ("StringProbeSecond", 0x110, [0x38620910, 0x4E800020]),
        ("CFStringGetLength", 0x38858, [0x38600001, 0x4E800020]),
        ("CFStringGetCharacters", 0x38A0C, [0xA003000C, 0xB0060000, 0x4E800020]),
        ("CFUniCharIsMemberOf", 0x461FC, [0x38600001, 0x4E800020]),
        ("CFAllocatorGetDefault", 0x54F58, [0x38600000, 0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFDictionaryCreateMutable", 0x2198C, [0x38620900, 0x4E800020]),
        ("CFDictionaryGetValue", 0x220E8, [0x38620900, 0x4E800020]),
        (
            "CFStringFindWithOptions",
            0x3A8F8,
            [0x2C080000, 0x41820014, 0x38000007, 0x90080000, 0x38000002, 0x90080004, 0x386000FF, 0x4E800020],
        ),
    ]
    # Original fixtures contain no Apple instructions. An oversized decomposition
    # result verifies the native guard without reading beyond its local output.
    code[0x4617C // 4 : 0x4617C // 4 + 2] = [0x3860000B, 0x4E800020]
    exports = []
    for i, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, i * 8, offset, 0)
        exports.append((name, 2, i * 8, 1))
    struct.pack_into(">4I", data, 0x900, 0, 0, 1, 0x00E90000 if mode == "oversize" else 0x00610000)
    struct.pack_into(">4I", data, 0x910, 0, 0, 1, 0x00620000)
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, exports=exports, reloc=[0x4600 | (len(routines) - 1)]), 0
    )
    p = Probe(
        [(library, name, 2) for name in NAMES]
        + [("CoreFoundationLib", name, 2) for name in ("StringProbeFirst", "StringProbeSecond")]
    )
    p.call("StringProbeFirst")
    p.emit(0x907E00C8)
    p.call("StringProbeSecond")
    p.emit(0x907E00CC)
    p.emit(0x807E00C8, 0x809E00CC, 0x38A00010 if mode == "oversize" else 0x38A00000)
    p.call(NAMES[0])
    if mode == "oversize":
        p.emit(0)
    else:
        p.expect(0x123456AB if foreign else -1, "native compare or exact foreign result")
        p.emit(0x807E00C8, 0x809E00CC, 0x38A00000, 0x38C00001, 0x38E00000)
        p.call(NAMES[1])
        p.expect(0x123456AB if foreign else -1, "range comparison ABI")
        p.emit(0x387E0300)
        p.call(NAMES[2])
        p.emit(0x809E00C8)
        p.equal("canonical cached constant")
        p.emit(0x38600000, 0x809E00C8)
        p.call(NAMES[3])
        p.emit(0x809E00C8)
        p.equal("immutable copy identity")
        for index in (4, 5):
            p.emit(0x807E00C8, 0x809E00CC)
            p.call(NAMES[index])
            p.expect(0xAB if foreign else 255, "Boolean low-byte marshalling")
        p.emit(0x387E0104, 0x809E00C8, 0x80BE00CC, 0x38C00004)
        p.call(NAMES[6])
        for offset, expected in ((256, 0), (260, 7), (264, 2), (268, 0)):
            p.emit(0x807E0000 | offset)
            p.expect(expected, "hidden CFRange output with guards")
        p.emit(0x807E00C8)
        p.call(NAMES[7])
        p.expect(0x123456AB if foreign else 0, "integer scalar marshalling")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " StrOps Probe")


def make_probe(destination, library="CarbonLib", real=False, mode="valid"):
    if not real:
        return make_profile(destination, library, mode)
    destination.mkdir(parents=True, exist_ok=True)
    p = Probe(
        [(library, name, 2) for name in NAMES]
        + [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        + [
            ("CoreFoundationLib", name, 2)
            for name in ("CFStringCreateWithCharacters", "CFRelease", "CFEqual", "CFStringCreateMutableCopy")
        ]
    )
    p.data.extend(bytes(30000 - len(p.data)))
    cursor = 4096

    def raw(value):
        nonlocal cursor
        offset = cursor
        p.data[offset : offset + len(value)] = value
        cursor += len(value) + 2
        cursor += cursor & 1
        assert cursor < len(p.data)
        return offset

    def string(value, slot):
        encoded = value.encode("utf-16-be", errors="surrogatepass")
        offset = raw(encoded)
        p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000 | (len(encoded) // 2))
        p.call("CFStringCreateWithCharacters")
        p.nonzero("original UTF16 string " + ascii(value))
        p.emit(0x907E0000 | slot)

    def release(slot):
        p.emit(0x807E0000 | slot)
        p.call("CFRelease")

    def original(index):
        p.emit(0x819E0000 | (512 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    def pair(index, arguments, label):
        p.emit(*arguments)
        p.call(NAMES[index])
        p.emit(0x907E00D8)
        p.emit(*arguments)
        original(index)
        p.emit(0x809E00D8)
        p.equal(label)

    name = b"CoreFoundationLib"
    p.data[896 : 896 + len(name) + 1] = bytes([len(name)]) + name
    p.emit(0x387E0380, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0080, 0x38FE0084, 0x39000000)
    p.call("GetSharedLibrary")
    p.expect(0, "load original string oracle")
    for i, name in enumerate(NAMES):
        offset = 1024 + i * 80
        value = name.encode()
        p.data[offset : offset + len(value) + 1] = bytes([len(value)]) + value
        p.emit(0x807E0080, 0x389E0000 | offset, 0x38BE0000 | (512 + i * 4), 0x38DE0088)
        p.call("FindSymbol")
        p.expect(0, "resolve original " + name)

    cases = [
        ("", ""),
        ("abc", "abd"),
        ("ABC", "abc"),
        ("a\0b", "a\0c"),
        ("\uffffx", ""),
        ("é", "e\u0301"),
        ("é", "e"),
        ("É", "é"),
        ("ß", "ss"),
        ("Ä", "A\u0308"),
        ("Ω", "ω"),
        ("가", "가"),
        ("\ud800", "\ud801"),
        ("x" * 70 + "a", "x" * 70 + "b"),
    ]
    for a, b in cases:
        string(a, 200)
        string(b, 204)
        for options in (0, 1, 16, 17, 32, 33, 48, 49, 64, 0x80000001):
            # CarbonLib 1.4's nonlocalized decomposition worker (0x49a80)
            # is just blr. Its unchanged character return can make the original
            # compare read beyond a ten-unit local buffer. Exercise these flags
            # on ASCII; archive tests check the native oversized-result guard.
            if options in (16, 17) and not (a.isascii() and b.isascii()):
                continue
            args = (0x807E00C8, 0x809E00CC, 0x3CA00000 | (options >> 16), 0x60A50000 | (options & 65535))
            pair(0, args, "compare " + ascii((a, b, options)))
        for location, length in ((0, len(a)), (0, 0), (min(1, len(a)), max(0, len(a) - 1))):
            for options in (0, 1, 16, 32):
                if options == 16 and not (a.isascii() and b.isascii()):
                    continue
                pair(
                    1,
                    (0x807E00C8, 0x809E00CC, 0x38A00000 | location, 0x38C00000 | length, 0x38E00000 | options),
                    "compare range " + ascii((a, b, location, length, options)),
                )
        release(200)
        release(204)

    for value in (
        "",
        "0",
        "123tail",
        " - 42x",
        "+2147483648",
        "-2147483649",
        "9" * 80,
        "\u2001-\u300042",
        "\0 123",
        "\u008512",
        "\uffff123",
        "+x",
        "123 456",
    ):
        string(value, 200)
        pair(7, (0x807E00C8,), "integer " + ascii(value))
        release(200)

    for value in ("", "abcabc", "éabcÉ", "\uffffabc", "x" * 70 + "abc"):
        string(value, 200)
        for pattern in ("", "abc", "É", "absent"):
            string(pattern, 204)
            for index in (4, 5):
                pair(index, (0x807E00C8, 0x809E00CC), NAMES[index] + ascii((value, pattern)))
            for options in (0, 1, 4, 5, 8, 12, 16):
                if options == 16 and not (value.isascii() and pattern.isascii()):
                    continue
                for output, native in ((256, True), (272, False)):
                    p.emit(0x387E0000 | (output + 4), 0x809E00C8, 0x80BE00CC, 0x38C00000 | options)
                    if native:
                        p.call(NAMES[6])
                    else:
                        original(6)
                for delta in (0, 4, 8, 12):
                    p.emit(0x807E0000 | (256 + delta), 0x809E0000 | (272 + delta))
                    p.equal("CFRange result and guards " + ascii((value, pattern, options, delta)))
            release(204)
        release(200)

    for value in ("", "ascii copy", "éΩ\uffff", "x" * 90):
        for mutable in (False, True):
            string(value, 200)
            if mutable:
                p.emit(0x38600000, 0x38800000, 0x80BE00C8)
                p.call("CFStringCreateMutableCopy")
                p.nonzero("mutable source")
                p.emit(0x907E00CC)
                release(200)
                p.emit(0x807E00CC, 0x907E00C8)
            for slot, native in ((216, True), (220, False)):
                p.emit(0x38600000, 0x809E00C8)
                if native:
                    p.call(NAMES[3])
                else:
                    original(3)
                p.nonzero("owned string copy")
                p.emit(0x907E0000 | slot)
                if not mutable:
                    p.emit(0x809E00C8)
                    p.equal("immutable default-allocator identity")
            p.emit(0x807E00D8, 0x809E00DC)
            p.call("CFEqual")
            p.expect(1, "copy value equality")
            for slot in (200, 216, 220):
                release(slot)

    for i, value in enumerate(
        (b"ROMAW native constant alpha", b"ROMAW original constant beta", b"ROMAW constant \x80 gamma")
    ):
        first, second = raw(value + b"\0"), raw(value + b"\0")
        for j in range(3):
            p.emit(0x387E0000 | (first if j == 0 else second))
            if (j + i) % 2:
                original(2)
            else:
                p.call(NAMES[2])
            p.nonzero("borrowed interned constant")
            if j == 0:
                p.emit(0x907E00D8)
            else:
                p.emit(0x809E00D8)
                p.equal("shared original/native canonical constant pointer")
    p.emit(0x387E0080)
    p.call("CloseConnection")
    p.expect(0, "close original oracle")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " StrOps Real Probe", carbon=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("case_mode", nargs="?", choices=("valid", "foreign", "oversize"))
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--mode", choices=("valid", "foreign", "oversize"), default="valid")
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.case_mode or args.mode)
