#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_url_probe.py
#
#   Summary:   Original URL ABI/profile fixtures and genuine CarbonLib 1.4 URL comparisons.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original URL ABI/profile fixtures and genuine CarbonLib 1.4 URL comparisons."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "CFURLGetTypeID",
    "CFURLCanBeDecomposed",
    "CFURLGetString",
    "CFURLGetBaseURL",
    "CFURLHasDirectoryPath",
    "CFURLCopyScheme",
    "CFURLCopyNetLocation",
    "CFURLCopyPath",
    "CFURLCopyStrictPath",
    "CFURLCopyResourceSpecifier",
    "CFURLCopyLastPathComponent",
    "CFURLCreateCopyAppendingPathComponent",
)
OFFSETS = (0x61D8, 0x83E0, 0x8448, 0x84A8, 0x89D4, 0x85C0, 0x863C, 0x8848, 0x88B4, 0x8A6C, 0xABA8, 0xAE24)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x38620B00, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    routines[0] = (NAMES[0], OFFSETS[0], [0x38601234, 0x4E800020])
    for index in (1, 4):
        routines[index] = (NAMES[index], OFFSETS[index], [0x386000EF, 0x4E800020])
    routines[8] = (NAMES[8], OFFSETS[8], [0x2C040000, 0x4182000C, 0x3800005A, 0x98040000, 0x38620B00, 0x4E800020])
    routines += [
        ("CFStringGetLength", 0x38858, [0x38600006, 0x4E800020]),
        ("CFStringGetCharacterAtIndex", 0x3891C, [0x38600065, 0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFStringCreateWithSubstring", 0x37AD0, [0x38620A00, 0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x38620A00, 0x4E800020]),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFStringCreateMutableCopy", 0x387EC, [0x38620C00, 0x4E800020]),
        ("CFURLCreateStringByAddingPercentEscapes", 0x5CEC, [0x38620D00, 0x4E800020]),
        ("CFStringInsert", 0x3D148, [0x4E800020]),
        ("URLFallbackToken", 0x100, [0x80620600, 0x4E800020]),
        ("URLRecordPointer", 0x108, [0x38620900, 0x4E800020]),
    ]
    private = {0x61E0: [0x80030008, 0x60002000, 0x90030008, 0x4E800020], 0x98C8: [0x4E800020]}
    imports = []
    failures = {"parse-failure": 0x61E0, "convert-failure": 0x98C8, "insert-failure": 0x3D148}
    if mode in failures:
        imports = [("InterfaceLib", "ROMAWURLFailure", 2)]
        trampoline = [0x81820200, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        code[0x280 // 4 : 0x280 // 4 + len(trampoline)] = trampoline
        offset = failures[mode]
        branch = [0x48000000 | ((0x280 - offset) & 0x03FFFFFC)]
        if offset in private:
            private[offset] = branch
        else:
            i = next(i for i, item in enumerate(routines) if item[1] == offset)
            routines[i] = (routines[i][0], offset, branch)
    for offset, words in private.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    exports = []
    for index, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x300, 0)
    flags = 0xF2000 | (32 if mode == "insert-failure" else 0)
    if mode == "parse-failure":
        flags = 0xF0000
    if mode == "convert-failure":
        flags = 0
    words = [
        0x38600049,
        0x90620600,
        0x3860002A,
        0x90620810,
        0x3C600000 | (flags >> 16),
        0x60630000 | (flags & 65535),
        0x90620908,
        0x38620A00,
        0x9062090C,
        0x38620E00,
        0x90620914,
        0x38621000,
        0x906203F8,
        0x38600000,
        0x4E800020,
    ]
    code[0x300 // 4 : 0x300 // 4 + len(words)] = words
    struct.pack_into(">II", data, 0xE00, 0, 6)
    reloc = [0x4600 | len(routines)] + ([0xA000, 0x200, 0x4A00] if imports else [])
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=init, reloc=reloc), 0
    )


def make_probe(destination, library="CarbonLib", real=False, mode="valid"):
    destination.mkdir(parents=True, exist_ok=True)
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
        p = Probe(
            [(library, name, 2) for name in NAMES]
            + [("CoreFoundationLib", name, 2) for name in ("URLFallbackToken", "URLRecordPointer")]
        )
        p.call("URLFallbackToken")
        p.expect(0x49, "genuine synthetic initializer executed")
        p.call("URLRecordPointer")
        p.emit(0x907E00C0)
        if mode.endswith("failure"):
            index = 1 if mode == "parse-failure" else 2 if mode == "convert-failure" else 11
            if index == 11:
                p.emit(0x807E00C0, 0x7C641B78, 0x38A30000, 0x38600000, 0x38C00000)
            p.call(NAMES[index])
            p.emit(0)
        else:
            for index, name in enumerate(NAMES):
                if index:
                    p.emit(0x807E00C0)
                if index == 8:
                    p.emit(0x388000A5, 0x989E00D0, 0x389E00D0)
                if index == 11:
                    p.emit(0x7C641B78, 0x38A30000, 0x38600000, 0x38C00000)
                p.call(name)
                if index == 0:
                    p.expect(0x1234 if mode == "foreign" else 42, "real type registration or foreign delegate")
                elif index in (1, 4):
                    p.expect(0xEF if mode == "foreign" else 0, "Boolean word/byte ABI")
                elif mode == "foreign":
                    p.emit(0x809E00C0, 0x38840200)
                    p.equal("foreign URL pointer result")
                elif index in (2, 9, 10):
                    p.emit(0x809E00C0, 0x38840100)
                    p.equal("borrowed/string component result")
                else:
                    p.expect(0, "absent base/path/component result")
                if index == 8:
                    p.emit(0x887E00D0)
                    p.expect(0x5A if mode == "foreign" else 0, "optional absolute-path output")
        p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URL Probe")
        return
    p = Probe(
        [(library, name, 2) for name in NAMES]
        + [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        + [
            ("CoreFoundationLib", name, 2)
            for name in (
                "CFStringCreateWithCString",
                "CFURLCreateWithString",
                "CFURLCreateWithFileSystemPathRelativeToBase",
                "CFRelease",
                "CFEqual",
                "CFGetTypeID",
            )
        ]
    )
    p.data.extend(bytes(16384 - len(p.data)))
    cursor = 4096
    texts = {}

    def raw_text(text):
        nonlocal cursor
        if text not in texts:
            b = text.encode("utf-8") + b"\0"
            texts[text] = cursor
            p.data[cursor : cursor + len(b)] = b
            cursor += len(b)
            assert cursor < len(p.data)
        return texts[text]

    def string(text, out):
        offset = raw_text(text)
        p.emit(0x38600000, 0x389E0000 | offset, 0x3CA00800, 0x60A50100)
        p.call("CFStringCreateWithCString")
        p.nonzero("actual UTF-8 CFString")
        p.emit(0x907E0000 | out)

    def release(slot):
        p.emit(0x807E0000 | slot)
        p.call("CFRelease")

    def original(index):
        p.emit(0x819E0000 | (256 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    def fresh(spec, label=""):
        kind, value, directory, parent = spec
        string(value, 208)
        for slot in (200, 204):
            p.emit(0x38600000, 0x809E00D0)
            if kind == 15:
                p.emit(0x80BE0000 | parent)
                p.call("CFURLCreateWithString")
            else:
                p.emit(0x38A00000 | kind, 0x38C00000 | directory, 0x80FE0000 | parent)
                p.call("CFURLCreateWithFileSystemPathRelativeToBase")
            p.nonzero("separate original URL object: " + label + " " + ascii(spec))
            p.emit(0x907E0000 | slot)
        p.emit(0x807E00C8, 0x809E00CC, 0x7C032000, 0x40820008, 0)
        release(208)

    def compare(owned=False):
        # Results are in the two slots. Equal NULLs bypass CFEqual; distinct
        # objects must be non-NULL and genuinely equal according to the runtime.
        p.emit(0x807E00D8, 0x809E00DC, 0x7C032000)
        equal_branch = len(p.code)
        p.emit(0x41820000)
        p.nonzero("native value is populated when oracle is populated")
        p.emit(0x7C832378)
        p.nonzero("oracle value is populated when native is populated")
        p.emit(0x807E00D8, 0x809E00DC)
        p.call("CFEqual")
        p.nonzero("original/native real objects compare equal")
        p.code[equal_branch] |= (len(p.code) - equal_branch) * 4
        if owned:
            for slot in (216, 220):
                p.emit(0x807E0000 | slot, 0x2C030000)
                skip = len(p.code)
                p.emit(0x41820000)
                p.call("CFRelease")
                p.code[skip] |= (len(p.code) - skip) * 4

    raw = b"CoreFoundationLib"
    p.data[896 : 896 + len(raw) + 1] = bytes([len(raw)]) + raw
    p.emit(0x387E0380, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0080, 0x38FE0084, 0x39000000)
    p.call("GetSharedLibrary")
    p.expect(0, "oracle library reference load")
    for i, name in enumerate(NAMES):
        offset = 1024 + i * 80
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
        p.emit(0x807E0080, 0x389E0000 | offset, 0x38BE0000 | (256 + i * 4), 0x38DE0088)
        p.call("FindSymbol")
        p.expect(0, "original un-intercepted URL oracle")
    for value, slot in (("http://base.test/base/", 304), ("file:///D/Source/", 308)):
        string(value, 208)
        p.emit(0x38600000, 0x809E00D0, 0x38A00000)
        p.call("CFURLCreateWithString")
        p.nonzero("real shared absolute base")
        p.emit(0x907E0000 | slot)
        release(208)
    specs = [
        (15, "http://user:pass@example.test:8080/a/b;param?query#frag", 0, 312),
        (15, "http://example.test/", 0, 312),
        (15, "file:///D/Source/a%20b/", 0, 312),
        (15, "mailto:name@example.test", 0, 312),
        (15, "urn:example:opaque", 0, 312),
        (15, "a/b", 0, 304),
        (15, "../a?x#y", 0, 304),
        (15, "file:///D/a%FF", 0, 312),
        (15, "file:///D/a%2Fb", 0, 312),
        (0, "/D/Source/ROMAW/a b", 0, 312),
        (0, "/D/Source/ROMAW/", 1, 312),
        (0, "relative/path", 0, 308),
        (0, "/", 1, 312),
        (1, "D:Source:ROMAW:README.md", 0, 312),
        (1, ":relative:path", 0, 308),
        (1, "D:", 1, 312),
        (2, "D:\\Source\\ROMAW\\README.md", 0, 312),
        (2, "D:\\Source\\ROMAW\\", 1, 312),
        (2, "\\\\host\\share\\folder", 1, 312),
    ]
    # These inputs are rejected by the original constructor; they do not
    # produce objects on which the URL accessor/append APIs can be called.
    for rejected, parent in (("", 304), ("http://[::1]:42/a", 312), ("http://example.test/a%ZZ", 312)):
        string(rejected, 208)
        for _ in range(2):
            p.emit(0x38600000, 0x809E00D0, 0x80BE0000 | parent)
            p.call("CFURLCreateWithString")
            p.expect(0, "original constructor rejection: " + ascii(rejected))
        release(208)
    p.call(NAMES[0])
    p.nonzero("registered native URL type ID")
    p.emit(0x907E013C)
    original(0)
    p.emit(0x809E013C)
    p.equal("native/original registered type ID")
    for spec in specs:
        for index in range(1, 11):
            fresh(spec, NAMES[index])
            p.emit(0x807E00C8)
            if index == 8:
                p.emit(0x3C60A5A5, 0x6063A5A5, 0x907E0280, 0x907E0290, 0x807E00C8, 0x389E0281)
            p.call(NAMES[index])
            p.emit(0x907E00D8, 0x807E00CC)
            if index == 8:
                p.emit(0x389E0291)
            original(index)
            p.emit(0x907E00DC)
            if index in (1, 4):
                p.emit(0x807E00D8, 0x809E00DC)
                p.equal("native/original Boolean URL state")
            else:
                compare(index >= 5)
            if index == 8:
                p.emit(0x887E0281, 0x889E0291)
                p.equal("absolute-path Boolean agrees with original")
                for offset in (640, 642, 643, 656, 658, 659):
                    p.emit(0x887E0000 | offset)
                    p.expect(0xA5, "strict-path output byte guard")
            release(200)
            release(204)
    components = (
        "child",
        "a b",
        "slash/part",
        "colon:part",
        "back\\part",
        "semi;query?#",
        "%20",
        "",
        "caf\u00e9",
        "snow\u2603",
    )
    for spec in specs:
        for value in components:
            for directory in (0, 1):
                fresh(spec, "append " + ascii(value) + " directory=" + str(directory))
                string(value, 212)
                p.emit(0x38600000, 0x809E00C8, 0x80BE00D4, 0x38C00000 | directory)
                p.call(NAMES[11])
                p.emit(0x907E00D8)
                p.emit(0x38600000, 0x809E00CC, 0x80BE00D4, 0x38C00000 | directory)
                original(11)
                p.emit(0x907E00DC)
                if value == "child" and spec[0] != 15:
                    p.emit(0x807E00D8)
                    p.nonzero("real filename append succeeds")
                    p.emit(0x807E00DC)
                    p.nonzero("original filename append succeeds")
                compare(True)
                release(212)
                release(200)
                release(204)
    for slot in (304, 308):
        release(slot)
    p.emit(0x387E0080)
    p.call("CloseConnection")
    p.expect(0, "oracle closure closes")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URL Real Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode_arg", nargs="?", default="valid")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--mode", choices=("valid", "foreign", "parse-failure", "convert-failure", "insert-failure"))
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.mode or args.mode_arg)
