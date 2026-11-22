#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_url_resolution_probe.py
#
#   Summary:   Original URL resolution profiles and genuine path/validator comparisons.
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

"""Original URL resolution profiles and genuine path/validator comparisons."""
import argparse
import struct
from pathlib import Path
from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = ("CFURLCopyAbsoluteURL", "CFURLCopyFileSystemPath", "CFURLGetFileSystemRepresentation", "CFURLCreateWithString")
ORACLES = NAMES + ("CFURLCreateWithFileSystemPathRelativeToBase",)
OFFSETS = (0x7F6C, 0xA4BC, 0xA78C, 0x7AC0)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x38620E00, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    routines[2] = (NAMES[2], OFFSETS[2], [0x3C601234, 0x606356AB, 0x4E800020])
    routines += [
        ("URLRecordPointer", 0x100, [0x38620900, 0x4E800020]),
        ("CFURLHasDirectoryPath", 0x89D4, [0x38600000, 0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFRelease", 0x53488, [0x4E800020]),
        ("CFStringGetLength", 0x38858, [0x38600006, 0x4E800020]),
        ("CFStringGetCharacterAtIndex", 0x3891C, [0x38600061, 0x4E800020]),
        ("CFStringGetSystemEncoding", 0x35600, [0x38600000, 0x4E800020]),
        (
            "CFStringGetBytes",
            0x38AC8,
            [
                0x3C004142,
                0x60004344,
                0x90090000,
                0x38004546,
                0xB0090004,
                0x81610038,
                0x38000006,
                0x900B0000,
                0x38600006,
                0x4E800020,
            ],
        ),
        ("_CFRuntimeCreateInstance", 0x53204, [0x38620B00, 0x4E800020]),
        ("CFStringCreateCopy", 0x37D08, [0x38620A00, 0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x38620A00, 0x4E800020]),
        ("CFStringFindWithOptions", 0x3A8F8, [0x38600000, 0x4E800020]),
        ("CFStringCreateWithSubstring", 0x37AD0, [0x38620A00, 0x4E800020]),
    ]
    # A native constructor must ignore the obsolete private validator entirely.
    code[0x6BAC // 4 : 0x6BAC // 4 + 2] = [0x38600000, 0x4E800020]
    exports = []
    for index, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    imports = []
    faults = {"encoder-failure": 0x38AC8, "merge-failure": 0x9BE8, "path-failure": 0xBF64}
    if mode in faults:
        imports = [("InterfaceLib", "ROMAWURLResolutionFailure", 2)]
        code[0x280 // 4 : 0x280 // 4 + 5] = [0x81820200, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        offset = faults[mode]
        code[offset // 4] = 0x48000000 | ((0x280 - offset) & 0x03FFFFFC)
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x300, 0)
    words = [
        0x38620A00,
        0x9062090C,
        0x9062110C,
        0x3860002A,
        0x90620810,
        0x38601200,
        0x906203F8,
        0x3C600001,
        0x90620908,
        0x90621108,
        0x38620F00,
        0x90620914,
    ]
    if mode == "merge-failure":
        words += [0x38621100, 0x90620910]
    if mode == "path-failure":
        words += [0x3C60000F, 0x60632020, 0x90620908]
    words += [0x38600000, 0x4E800020]
    code[0x300 // 4 : 0x300 // 4 + len(words)] = words
    struct.pack_into(">II", data, 0xF00, 0, 6)
    write_fragment(
        destination,
        "CoreFoundationLib",
        pef(
            code,
            data,
            imports=imports,
            exports=exports,
            init=init,
            reloc=[0x4600 | len(routines)] + ([0xA000, 0x200, 0x4A00] if imports else []),
        ),
        0,
    )


def make_probe(destination, library="CarbonLib", real=False, mode="valid"):
    destination.mkdir(parents=True, exist_ok=True)
    imports = [(library, name, 2) for name in NAMES]
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
        p = Probe(imports + [("CoreFoundationLib", "URLRecordPointer", 2)])
        p.call("URLRecordPointer")
        p.emit(0x907E00C8)
        if mode.endswith("failure"):
            if mode == "encoder-failure":
                p.emit(0x807E00C8, 0x38800000, 0x38BE0140, 0x38C00020)
                p.call(NAMES[2])
            elif mode == "merge-failure":
                p.emit(0x807E00C8)
                p.call(NAMES[0])
            else:
                p.emit(0x807E00C8, 0x38800001)
                p.call(NAMES[1])
            p.finish(destination, "PPC Carbon URLResolve Probe")
            return
        p.emit(0x807E00C8)
        p.call(NAMES[0])
        p.emit(0x809E00C8)
        if mode == "foreign":
            p.emit(0x38840500)
        p.equal("native absolute identity or exact foreign marker")
        p.emit(0x807E00C8, 0x38800001)
        p.call(NAMES[1])
        p.emit(0x809E00C8, 0x38840500 if mode == "foreign" else 0x38840100)
        p.equal("native HFS string or exact foreign marker")
        p.data[320:352] = bytes([0xA5]) * 32
        p.emit(0x807E00C8, 0x38800000, 0x38BE0140, 0x38C00020)
        p.call(NAMES[2])
        p.expect(0xAB if mode == "foreign" else 1, "filesystem Boolean marshalling")
        if mode != "foreign":
            p.emit(0x807E0140)
            p.expect(0x41424344, "nine-argument encoder buffer")
            p.emit(0x807E0144)
            p.expect(0x454600A5, "encoded byte count and terminator")
        p.emit(0x38600000, 0x809E00C8, 0x38A00000)
        p.call(NAMES[3])
        p.emit(0x809E00C8, 0x38840500 if mode == "foreign" else 0x38840200)
        p.equal("native runtime allocation bypasses rejected private validator")
        p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLResolve Probe")
        return
    p = Probe(
        imports
        + [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        + [
            ("CoreFoundationLib", name, 2)
            for name in ("CFStringCreateWithCString", "CFStringCreateWithCharacters", "CFRelease", "CFEqual")
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
        p.emit(0x819E0000 | (512 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    def fresh(spec, label=""):
        kind, value, directory, parent = spec
        string(value, 208)
        for slot in (200, 204):
            p.emit(0x38600000, 0x809E00D0)
            if kind == 15:
                p.emit(0x80BE0000 | parent)
                original(3)
            else:
                p.emit(0x38A00000 | kind, 0x38C00000 | directory, 0x80FE0000 | parent)
                original(4)
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

    def compare_url():
        p.emit(0x807E00D8, 0x2C030000)
        skip = len(p.code)
        p.emit(0x41820000)
        p.emit(0x809E00DC)
        p.nonzero("original URL exists for native result")
        p.emit(0x807E00D8, 0x80630008, 0x809E00DC, 0x80840008)
        p.equal("raw absolute flags agree")
        p.emit(0x807E00D8, 0x8063000C, 0x809E00DC, 0x8084000C)
        p.call("CFEqual")
        p.nonzero("stored absolute strings agree")
        p.code[skip] |= (len(p.code) - skip) * 4
        compare(True)

    def compare_validation():
        p.emit(0x807E00D8, 0x2C030000)
        empty = len(p.code)
        p.emit(0x41820000)
        p.emit(0x807E00DC)
        p.nonzero("reference validator accepts native success")
        p.emit(0x807E00D8, 0x80630008, 0x809E00DC, 0x80840008)
        p.equal("accepted URL flags agree before parsing")
        p.emit(0x807E00D8, 0x8063000C, 0x809E00DC, 0x8084000C)
        p.call("CFEqual")
        p.nonzero("accepted URL retains exact string")
        release(216)
        release(220)
        done = len(p.code)
        p.emit(0x48000000)
        p.code[empty] |= (len(p.code) - empty) * 4
        p.emit(0x807E00DC)
        p.expect(0, "reference validator rejects native rejection")
        p.code[done] |= (len(p.code) - done) * 4

    raw = b"CoreFoundationLib"
    p.data[896 : 896 + len(raw) + 1] = bytes([len(raw)]) + raw
    p.emit(0x387E0380, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0080, 0x38FE0084, 0x39000000)
    p.call("GetSharedLibrary")
    p.expect(0, "oracle library reference load")
    for i, name in enumerate(ORACLES):
        offset = 1024 + i * 80
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
        p.emit(0x807E0080, 0x389E0000 | offset, 0x38BE0000 | (512 + i * 4), 0x38DE0088)
        p.call("FindSymbol")
        p.expect(0, "original un-intercepted URL oracle")
    for value, slot in (("http://baseUser:basePass@base.test:77/base;p?q#f", 304), ("file:///D/Source/", 308)):
        string(value, 208)
        p.emit(0x38600000, 0x809E00D0, 0x38A00000)
        original(3)
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

    for style, value, slot in (
        (0, "/D/Source/ROMAW/", 320),
        (1, "D:Source:ROMAW:", 324),
        (2, "D:\\Source\\ROMAW\\", 328),
    ):
        string(value, 208)
        p.emit(0x38600000, 0x809E00D0, 0x38A00000 | style, 0x38C00001, 0x38E00000)
        original(4)
        p.nonzero("same-style original filesystem base")
        p.emit(0x907E0000 | slot)
        release(208)
    specs += [
        (15, value, 0, 304)
        for value in (
            "#frag",
            "?query",
            ";parameter",
            "//other.test/a",
            "../x/./y/../z",
            "/root/path",
            "?q%FF",
            "#f%FF",
        )
    ]
    specs += [
        (style, value, folder, slot)
        for style, slot, value in ((0, 320, "child/path"), (1, 324, ":child:path"), (2, 328, "child\\path"))
        for folder in (0, 1)
    ]
    specs += [(15, value, 0, 320) for value in ("child", "../child")]
    for spec in specs:
        fresh(spec, "absolute URL")
        p.emit(0x807E00C8)
        p.call(NAMES[0])
        p.emit(0x907E00D8)
        p.emit(0x807E00CC)
        original(0)
        p.emit(0x907E00DC)
        compare_url()
        release(200)
        release(204)
        for style in (0, 1, 2):
            fresh(spec, "filesystem style " + str(style))
            p.emit(0x807E00C8, 0x38800000 | style)
            p.call(NAMES[1])
            p.emit(0x907E00D8)
            p.emit(0x807E00CC, 0x38800000 | style)
            original(1)
            p.emit(0x907E00DC)
            compare(True)
            release(200)
            release(204)
        for resolve in (0, 1):
            for capacity in (8, 128):
                fresh(spec, "filesystem bytes resolve=" + str(resolve) + " cap=" + str(capacity))
                p.emit(0x3C60A5A5, 0x6063A5A5)
                for start in (2560, 2816):
                    for offset in range(start - 4, start + 132, 4):
                        p.emit(0x907E0000 | offset)
                p.emit(0x807E00C8, 0x38800000 | resolve, 0x38BE0A00, 0x38C00000 | capacity)
                p.call(NAMES[2])
                p.emit(0x907E00D8)
                p.emit(0x807E00CC, 0x38800000 | resolve, 0x38BE0B00, 0x38C00000 | capacity)
                original(2)
                p.emit(0x907E00DC)
                p.emit(0x807E00D8, 0x809E00DC)
                p.equal("filesystem conversion result")
                for offset in range(0, 128, 4):
                    p.emit(0x807E0000 | (2560 + offset), 0x809E0000 | (2816 + offset))
                    p.equal("filesystem output bytes including partial writes")
                for start in (2560, 2816):
                    for offset in (start - 1, start + capacity):
                        p.emit(0x887E0000 | offset)
                        p.expect(0xA5, "filesystem output guards")
                release(200)
                release(204)
    fresh((15, "file:///D/Source/a%20b/", 0, 312), "capacity-one reference bug")
    p.emit(0x3C60A5A5, 0x6063A5A5)
    for start in (2560, 2816):
        for offset in range(start - 4, start + 132, 4):
            p.emit(0x907E0000 | offset)
    p.emit(0x807E00C8, 0x38800000, 0x38BE0A00, 0x38C00001)
    p.call(NAMES[2])
    p.expect(0, "native nonempty capacity-one rejection")
    for offset in range(2560, 2688, 4):
        p.emit(0x807E0000 | offset)
        p.expect(0xA5A5A5A5, "native rejection leaves buffer untouched")
    # The reference receives a full 128-byte backing allocation. Its declared
    # capacity is one; it reports success and places a terminator beyond that
    # declared extent. This observation never exceeds the backing allocation.
    p.emit(0x807E00CC, 0x389E0000, 0x38800000, 0x38BE0B00, 0x38C00001)
    original(2)
    p.expect(1, "reference zero-capacity sizing bug is present")
    p.emit(0x887E0B0C)
    p.expect(0, "reference terminator exceeds declared capacity")
    p.emit(0x887E0B80)
    p.expect(0xA5, "reference observation stays in backing storage")
    release(200)
    release(204)
    for final in (False, True):
        for unit in list(range(256)) + [0x100, 0x200B, 0x2028, 0xD800, 0xDC00, 0xFFFF]:
            chars = [ord("a"), unit] + ([] if final else [ord("b")])
            offset = cursor
            raw = struct.pack(">" + str(len(chars)) + "H", *chars)
            p.data[offset : offset + len(raw)] = raw
            cursor += len(raw)
            p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000 | len(chars))
            p.call("CFStringCreateWithCharacters")
            p.nonzero("explicit UTF-16 validator input")
            p.emit(0x907E00D0)
            for slot in (216, 220):
                p.emit(0x38600000, 0x809E00D0, 0x38A00000)
                if slot == 216:
                    p.call(NAMES[3])
                else:
                    original(3)
                p.emit(0x907E0000 | slot)
            compare_validation()
            release(208)
    for text in ("a#b#", "a#b#c", "%", "%0", "%00", "%aF", "%ag", "%G0", "a ", "a b", "a[", "a[b", "#", "##", "###"):
        string(text, 208)
        for slot in (216, 220):
            p.emit(0x38600000, 0x809E00D0, 0x38A00000)
            if slot == 216:
                p.call(NAMES[3])
            else:
                original(3)
            p.emit(0x907E0000 | slot)
        compare_validation()
        release(208)
    assert cursor < len(p.data)
    for slot in (304, 308, 320, 324, 328):
        release(slot)
    p.emit(0x387E0080)
    p.call("CloseConnection")
    p.expect(0, "oracle closure closes")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLRes Real Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode_arg", nargs="?", default="valid")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--mode", choices=("valid", "foreign", "encoder-failure", "merge-failure", "path-failure"))
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.mode or args.mode_arg)
