#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_url_creation_probe.py
#
#   Summary:   Original URL creation/mutation ABI profiles and genuine 1.4 comparisons.
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

"""Original URL creation/mutation ABI profiles and genuine 1.4 comparisons."""
import argparse
import struct
from pathlib import Path
from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "_CFURLAlloc",
    "_CFURLInitWithString",
    "CFURLCreateWithString",
    "CFURLCreateWithBytes",
    "CFURLCreateData",
    "CFURLCreateWithFileSystemPath",
    "CFURLCreateWithFileSystemPathRelativeToBase",
    "CFURLCreateCopyDeletingLastPathComponent",
    "CFURLCreateCopyAppendingPathExtension",
    "CFURLCreateCopyDeletingPathExtension",
)
OFFSETS = (0x6AC8, 0x7084, 0x7AC0, 0x78BC, 0x79E4, 0x9E10, 0x9F90, 0xB1E0, 0xB704, 0xB928)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x38620E00, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    routines += [
        ("URLRecordPointer", 0x100, [0x38620900, 0x4E800020]),
        ("CFURLGetString", 0x8448, [0x38620A00, 0x4E800020]),
        ("_CFRuntimeCreateInstance", 0x53204, [0x38620B00, 0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFStringCreateCopy", 0x37D08, [0x38620A00, 0x4E800020]),
        ("CFStringCreateWithFormat", 0x37A90, [0x38620A00, 0x4E800020]),
        ("CFURLCreateStringByAddingPercentEscapes", 0x5CEC, [0x38620A00, 0x4E800020]),
        ("CFStringCreateWithBytes", 0x37928, [0x38620A00, 0x4E800020]),
        ("CFStringCreateExternalRepresentation", 0x3C698, [0x38620D00, 0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x38620A00, 0x4E800020]),
        ("CFStringGetLength", 0x38858, [0x38600006, 0x4E800020]),
        ("CFStringGetCharacterAtIndex", 0x3891C, [0x38600065, 0x4E800020]),
        ("CFStringFindWithOptions", 0x3A8F8, [0x38600000, 0x4E800020]),
        ("CFStringCreateMutableCopy", 0x387EC, [0x38620C00, 0x4E800020]),
        ("CFStringCreateWithSubstring", 0x37AD0, [0x38620A00, 0x4E800020]),
        ("CFStringReplace", 0x3D1C8, [0x4E800020]),
        ("CFStringInsert", 0x3D148, [0x4E800020]),
        ("CFStringDelete", 0x3D190, [0x4E800020]),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFRelease", 0x53488, [0x4E800020]),
    ]
    for offset, words in {
        0x6BAC: [0x38600001, 0x4E800020],
        0x755C: [0x38620A00, 0x4E800020],
        0x9D84: [0x38600000, 0x4E800020],
        0x98C8: [0x4E800020],
    }.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    exports = []
    for index, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    imports = []
    faults = {"allocate-failure": 0x53204, "validate-failure": 0x3891C, "reencode-failure": 0x755C}
    if mode in faults:
        imports = [("InterfaceLib", "ROMAWURLCreationFailure", 2)]
        code[0x280 // 4 : 0x280 // 4 + 5] = [0x81820200, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        offset = faults[mode]
        code[offset // 4] = 0x48000000 | ((0x280 - offset) & 0x03FFFFFC)
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x300, 0)
    struct.pack_into(">II", data, 0xF00, 0, 6)
    words = [
        0x38620A00,
        0x9062090C,
        0x38621000,
        0x906203F8,
        0x3860002A,
        0x90620810,
        0x3C60000F,
        0x60632020,
        0x90620908,
        0x38620F00,
        0x90620914,
        0x38600000,
        0x4E800020,
    ]
    code[0x300 // 4 : 0x300 // 4 + len(words)] = words
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
    imports = [("CoreFoundationLib" if name.startswith("_CFURL") else library, name, 2) for name in NAMES]
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
        p = Probe(imports + [("CoreFoundationLib", "URLRecordPointer", 2)])
        p.call("URLRecordPointer")
        p.emit(0x907E00C8)
        if mode.endswith("failure"):
            if mode == "allocate-failure":
                p.emit(0x38600000)
                p.call(NAMES[0])
            elif mode == "validate-failure":
                p.emit(0x38600000, 0x809E00C8, 0x38A00000)
                p.call(NAMES[2])
            else:
                p.emit(0x38600000, 0x809E00C8, 0x38A00000, 0x38C00001)
                p.call(NAMES[4])
            p.finish(destination, "PPC Carbon URLCreate Probe")
            return
        for index, name in enumerate(NAMES):
            if index == 0:
                p.emit(0x38600000)
            elif index == 1:
                p.emit(0x807E00C8, 0x7C641B78, 0x38A00000)
            elif index == 3:
                p.emit(0x38600000, 0x809E00C8, 0x38A00004, 0x3CC00800, 0x60C60100, 0x38E00000)
            elif index == 4:
                p.emit(0x38600000, 0x809E00C8, 0x3CA00800, 0x60A50100, 0x38C00000)
            elif index in (5, 6):
                p.emit(0x38600000, 0x809E00C8, 0x38A00000, 0x38C00001, 0x38E00000)
            elif index == 8:
                p.emit(0x38600000, 0x809E00C8, 0x7C852378)
            else:
                p.emit(0x38600000, 0x809E00C8, 0x38A00000)
            p.call(name)
            if index != 1:
                p.nonzero("native or delegated URL construction/mutation result")
        p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLCreate Probe")
        return
    p = Probe(
        imports
        + [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        + [
            ("CoreFoundationLib", name, 2)
            for name in ("CFStringCreateWithCString", "CFRelease", "CFEqual", "CFGetTypeID", "CFURLGetTypeID")
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
                original(2)
            else:
                p.emit(0x38A00000 | kind, 0x38C00000 | directory, 0x80FE0000 | parent)
                original(6)
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

    def compare_url(owned=False):
        # Compare representation before CFEqual can parse/cache either URL.
        # Semantic equality alone can conceal different path rewrites.
        p.emit(0x807E00D8, 0x2C030000)
        skip = len(p.code)
        p.emit(0x41820000)
        p.emit(0x809E00DC)
        p.nonzero("oracle URL exists for native URL")
        p.emit(0x807E00D8, 0x80630008, 0x809E00DC, 0x80840008)
        p.equal("raw URL flags agree before lazy parsing")
        p.emit(0x807E00D8, 0x8063000C, 0x809E00DC, 0x8084000C)
        p.call("CFEqual")
        p.nonzero("stored URL strings agree before canonicalization")
        p.code[skip] |= (len(p.code) - skip) * 4
        compare(owned)

    raw = b"CoreFoundationLib"
    p.data[896 : 896 + len(raw) + 1] = bytes([len(raw)]) + raw
    p.emit(0x387E0380, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0080, 0x38FE0084, 0x39000000)
    p.call("GetSharedLibrary")
    p.expect(0, "oracle library reference load")
    for i, name in enumerate(NAMES):
        offset = 1024 + i * 80
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
        p.emit(0x807E0080, 0x389E0000 | offset, 0x38BE0000 | (512 + i * 4), 0x38DE0088)
        p.call("FindSymbol")
        p.expect(0, "original un-intercepted URL oracle")
    for value, slot in (("http://baseUser:basePass@base.test:77/base;p?q#f", 304), ("file:///D/Source/", 308)):
        string(value, 208)
        p.emit(0x38600000, 0x809E00D0, 0x38A00000)
        original(2)
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

    # Constructor sources and shared bases above are built through original
    # un-intercepted entries, preventing native construction from masking mutations.
    specs += [
        (0, value, folder, 312)
        for value, folder in (
            ("file", 0),
            (".", 1),
            ("..", 1),
            ("a/.", 1),
            ("a/../", 1),
            ("ab/./", 1),
            ("/a/file.", 0),
            ("/a/.hidden", 0),
        )
    ]
    specs += [(15, value, 0, 304) for value in (".", "..", "a/.", "a/../", "/a/file.", "/a/.hidden")]
    for spec in specs:
        for index in (7, 9):
            fresh(spec, NAMES[index])
            p.emit(0x38600000, 0x809E00C8)
            p.call(NAMES[index])
            p.emit(0x907E00D8)
            p.emit(0x38600000, 0x809E00CC)
            original(index)
            p.emit(0x907E00DC)
            compare_url(True)
            release(200)
            release(204)
        for extension in ("txt", "", "a.b", "space here", "a/b", "a:b", "a\\b", "semi;query?#", "caf\u00e9"):
            fresh(spec, "append extension " + ascii(extension))
            string(extension, 212)
            p.emit(0x38600000, 0x809E00C8, 0x80BE00D4)
            p.call(NAMES[8])
            p.emit(0x907E00D8)
            p.emit(0x38600000, 0x809E00CC, 0x80BE00D4)
            original(8)
            p.emit(0x907E00DC)
            compare_url(True)
            release(212)
            release(200)
            release(204)
    for text in ("http://example.test/a%20b", "a/b", "path#fragment:colon", "a#b:c#d", "", "http://[::1]/", "a%ZZ"):
        string(text, 208)
        for index in (0,):
            p.emit(0x38600000)
            p.call(NAMES[index])
            p.nonzero("native runtime URL allocation")
            p.emit(0x907E00C8)
            p.emit(0x38600000)
            original(index)
            p.nonzero("original runtime URL allocation")
            p.emit(0x907E00CC)
        for slot in (200, 204):
            for offset in range(8, 28, 4):
                p.emit(0x807E0000 | slot, 0x80630000 | offset)
                p.expect(0, "zeroed URL body")
            p.emit(0x807E0000 | slot, 0x809E00D0, 0x80BE0130)
            if slot == 200:
                p.call(NAMES[1])
            else:
                original(1)
        # Init intentionally accepts caller-prevalidated strings, unlike Create.
        p.emit(0x807E00C8, 0x80630008, 0x809E00CC, 0x80840008)
        p.equal("initializer flags agree")
        for source, target in ((200, 216), (204, 220)):
            p.emit(0x807E0000 | source, 0x907E0000 | target)
        compare_url(True)
        for slot in (216, 220):
            p.emit(0x38600000, 0x809E00D0, 0x80BE0130)
            if slot == 216:
                p.call(NAMES[2])
            else:
                original(2)
            p.emit(0x907E0000 | slot)
        compare_url(True)
        release(208)
    for style, text in (
        (0, "/D/a b"),
        (0, "relative/"),
        (0, "/"),
        (0, "/a//"),
        (1, "D:Source:ROMAW:"),
        (1, ":relative:"),
        (2, "D:\\path\\"),
        (2, "D:'path"),
    ):
        string(text, 208)
        for folder in (0, 1):
            for index in (5, 6):
                for slot in (216, 220):
                    p.emit(0x38600000, 0x809E00D0, 0x38A00000 | style, 0x38C00000 | folder, 0x80FE0134)
                    if slot == 216:
                        p.call(NAMES[index])
                    else:
                        original(index)
                    p.nonzero("filesystem constructor produces real URL")
                    p.emit(0x907E0000 | slot)
                compare_url(True)
        release(208)
    for encoding in (0x08000100, 0, 0x0500):
        codec = {0x08000100: "utf-8", 0: "mac_roman", 0x0500: "cp1252"}[encoding]
        for text in ("http://example.test/a%20b", "http://example.test/caf\u00e9", ""):
            raw = text.encode(codec)
            offset = raw_text("bytes:" + text + str(encoding))
            p.data[offset : offset + len(raw)] = raw
            for slot in (216, 220):
                p.emit(
                    0x38600000,
                    0x389E0000 | offset,
                    0x38A00000 | len(raw),
                    0x3CC00000 | (encoding >> 16),
                    0x60C60000 | (encoding & 65535),
                    0x80FE0130,
                )
                if slot == 216:
                    p.call(NAMES[3])
                else:
                    original(3)
                if text == "http://example.test/a%20b":
                    p.nonzero("nonempty ASCII URL byte constructor succeeds in each encoding")
                p.emit(0x907E0000 | slot)
            compare_url(True)
    for spec in specs[:19]:
        for encoding in (0x08000100, 0, 0x0500):
            for whitespace in (0, 1):
                fresh(spec, "external representation")
                for source, target in ((200, 216), (204, 220)):
                    p.emit(
                        0x38600000,
                        0x809E0000 | source,
                        0x3CA00000 | (encoding >> 16),
                        0x60A50000 | (encoding & 65535),
                        0x38C00000 | whitespace,
                    )
                    if source == 200:
                        p.call(NAMES[4])
                    else:
                        original(4)
                    if spec == specs[0]:
                        p.nonzero("nonempty URL external representation succeeds")
                    p.emit(0x907E0000 | target)
                compare(True)
                release(200)
                release(204)
    for slot in (304, 308):
        release(slot)
    p.emit(0x387E0080)
    p.call("CloseConnection")
    p.expect(0, "oracle closure closes")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLCreate Real Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode_arg", nargs="?", default="valid")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument(
        "--mode", choices=("valid", "foreign", "allocate-failure", "validate-failure", "reencode-failure")
    )
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.mode or args.mode_arg)
