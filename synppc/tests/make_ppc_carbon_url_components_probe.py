#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_url_components_probe.py
#
#   Summary:   Original URL component/FSRef ABI fixtures and genuine 1.4 comparisons.
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

"""Original URL component/FSRef ABI fixtures and genuine 1.4 comparisons."""
import argparse
import struct
from pathlib import Path
from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "CFURLCopyHostName",
    "CFURLCopyUserName",
    "CFURLCopyPassword",
    "CFURLCopyParameterString",
    "CFURLCopyQueryString",
    "CFURLCopyFragment",
    "CFURLGetPortNumber",
    "__CFURLReservedPtr",
    "__CFURLSetReservedPtr",
    "CFURLCopyPathExtension",
    "CFURLCreateFromFileSystemRepresentation",
    "CFURLCreateFromFileSystemRepresentationRelativeToBase",
    "CFURLCreateFromFSRef",
    "CFURLGetFSRef",
)
OFFSETS = (
    0x8BE0,
    0x8DE8,
    0x8EBC,
    0x901C,
    0x9124,
    0x91DC,
    0x8CC0,
    0x9258,
    0x9260,
    0xAD14,
    0xA880,
    0xA928,
    0xC58C,
    0xC614,
)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x38620B00, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    routines[13] = (NAMES[13], OFFSETS[13], [0x3C601234, 0x606356AB, 0x4E800020])
    routines += [
        ("URLRecordPointer", 0x100, [0x38620900, 0x4E800020]),
        ("CFURLCopyLastPathComponent", 0xABA8, [0x38620A00, 0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x38620A00, 0x4E800020]),
        ("CFStringGetLength", 0x38858, [0x38600006, 0x4E800020]),
        ("CFStringGetCharacterAtIndex", 0x3891C, [0x38600065, 0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFStringCreateWithSubstring", 0x37AD0, [0x38620A00, 0x4E800020]),
        ("CFStringFindWithOptions", 0x3A8F8, [0x38600000, 0x4E800020]),
        ("CFRelease", 0x53488, [0x4E800020]),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFStringGetSystemEncoding", 0x35600, [0x38600000, 0x4E800020]),
        ("CFStringCreateWithBytes", 0x37928, [0x38620A00, 0x4E800020]),
        ("CFURLCreateWithFileSystemPath", 0x9E10, [0x38620A00, 0x4E800020]),
        ("CFURLCreateWithFileSystemPathRelativeToBase", 0x9F90, [0x38620A00, 0x4E800020]),
    ]
    exports = []
    for index, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, index * 8, offset, 0)
        exports.append((name, 2, index * 8, 1))
    code[0x4AF8 // 4 : 0x4AF8 // 4 + 2] = [0x38600000, 0x4E800020]
    imports = []
    if mode in ("capability-failure", "bytes-failure"):
        imports = [("InterfaceLib", "ROMAWURLComponentFailure", 2)]
        code[0x280 // 4 : 0x280 // 4 + 5] = [0x81820200, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        offset = 0x4AF8 if mode == "capability-failure" else 0x37928
        code[offset // 4] = 0x48000000 | ((0x280 - offset) & 0x03FFFFFC)
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x300, 0)
    words = [0x38620A00, 0x9062090C, 0x9062080C, 0x38621000, 0x906203F8, 0x38600000, 0x4E800020]
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
    imports = [("CoreFoundationLib" if name.startswith("__CFURL") else library, name, 2) for name in NAMES]
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
        p = Probe(imports + [("CoreFoundationLib", "URLRecordPointer", 2)])
        p.call("URLRecordPointer")
        p.emit(0x907E00C8)
        if mode in ("capability-failure", "bytes-failure"):
            p.emit(0x38600000, 0x389E00C8, 0x38A00004, 0x38C00000)
            p.call(NAMES[12 if mode == "capability-failure" else 10])
            p.finish(destination, "PPC Carbon URLComp Probe")
            return
        for index, name in enumerate(NAMES):
            if index == 8:
                p.emit(0x807E00C8, 0x389E00C8)
                p.call(name)
                p.emit(0x807E00C8)
                p.call(NAMES[7])
                p.emit(0x389E00C8)
                if mode == "foreign":
                    p.emit(0x807E00C8)
                    p.call("CFURLCopyHostName")
                    p.emit(0x907E00D8, 0x807E00C8)
                    p.call(NAMES[7])
                    p.emit(0x809E00D8)
                p.equal("reserved pointer marshalling and native storage")
                continue
            if index in (10, 11):
                p.emit(0x38600000, 0x389E00C8, 0x38A00004, 0x38C00001, 0x38E00000)
            elif index == 12:
                p.emit(0x38600000, 0x389E00C8)
            else:
                p.emit(0x807E00C8, 0x38800000)
            p.call(name)
            if mode == "foreign":
                if index == 13:
                    p.expect(0xAB, "Boolean fallback preserves low byte")
                else:
                    p.nonzero("public fallback value")
            elif index == 6:
                p.expect(0xFFFFFFFF, "file URL has no port")
            elif index in (0, 10, 11):
                p.nonzero("native file hostname or constructor result")
            elif index == 7:
                p.expect(0, "new URL has no reserved pointer")
            else:
                p.expect(0, "native absent component or unavailable filesystem capability")
        if mode == "valid":
            # Both import namespaces use the same synthetic URL in a reporting app.
            p.emit(0x807E00C8, 0x38800000)
            p.call("__CFURLSetReservedPtr")
        p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLComp Probe")
        return
    p = Probe(
        imports
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
        p.emit(0x819E0000 | (512 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

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
        p.emit(0x807E0080, 0x389E0000 | offset, 0x38BE0000 | (512 + i * 4), 0x38DE0088)
        p.call("FindSymbol")
        p.expect(0, "original un-intercepted URL oracle")
    for value, slot in (("http://baseUser:basePass@base.test:77/base;p?q#f", 304), ("file:///D/Source/", 308)):
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

    specs += [
        (15, "http://us%65r:pa%73s@ho%73t.test:2147483648/a.b/last.;p%2F?q%2F#f%2F", 0, 312),
        (15, "#only", 0, 304),
        (15, "?only", 0, 304),
        (15, ";only", 0, 304),
        (15, "//other.test/path", 0, 304),
    ]
    for port in (
        "0",
        "65536",
        "2147483647",
        "2147483648",
        "99999999999999999999999",
        "-2147483648",
        "-2147483649",
        "+23",
        "23x",
    ):
        specs.append((15, "http://example.test:" + port + "/a.tar.gz", 0, 312))
    for spec in specs:
        for index in (0, 1, 2, 3, 4, 5, 6, 9):
            for leave in (None, "/", "") if index in (3, 4, 5) else (None,):
                fresh(spec, NAMES[index] + " leave=" + repr(leave))
                if leave is not None:
                    string(leave, 212)
                else:
                    p.emit(0x38600000, 0x907E00D4)
                p.emit(0x807E00C8, 0x809E00D4)
                p.call(NAMES[index])
                p.emit(0x907E00D8)
                p.emit(0x807E00CC, 0x809E00D4)
                original(index)
                p.emit(0x907E00DC)
                if index == 6:
                    p.emit(0x807E00D8, 0x809E00DC)
                    p.equal("native/original saturated decimal port")
                else:
                    compare(True)
                if leave is not None:
                    release(212)
                release(200)
                release(204)
    fresh((15, "http://example.test/a", 0, 312), "reserved pointers")
    for value in (880, 0):
        for slot, index in ((200, 8), (204, 8)):
            p.emit(0x807E0000 | slot, 0x389E0000 | value if value else 0x38800000)
            if slot == 200:
                p.call(NAMES[index])
            else:
                original(index)
        for slot in (200, 204):
            p.emit(0x807E0000 | slot)
            if slot == 200:
                p.call(NAMES[7])
            else:
                original(7)
            p.emit(0x389E0000 | value if value else 0x38800000)
            p.equal("reserved pointer identity without ownership")
    release(200)
    release(204)
    for text in ("D:Source:ROMAW:README.md", "D:Source:ROMAW:", ":relative:path", "D:", "D:caf\u00e9"):
        raw = text.encode("mac_roman")
        offset = raw_text("raw:" + text)
        p.data[offset : offset + len(raw)] = raw
        for index in (10, 11):
            for folder in (0, 1):
                for slot in (216, 220):
                    p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000 | len(raw), 0x38C00000 | folder, 0x80FE0134)
                    if slot == 216:
                        p.call(NAMES[index])
                    else:
                        original(index)
                    p.nonzero("actual HFS representation constructor")
                    p.emit(0x907E0000 | slot)
                compare(True)
    # ROMAW currently reports no modern FSRef filesystem capability. These
    # genuine checks establish the gate and untouched output only; archive tests
    # separately exercise supported lookup/conversion and cleanup control flow.
    for offset in range(320, 480, 4):
        p.emit(0x3C60A5A5, 0x6063A5A5, 0x907E0000 | offset)
    fresh((15, "file:///D/Source/ROMAW/README.md", 0, 312), "FSRef gate")
    p.emit(0x807E00C8, 0x389E0140)
    p.call(NAMES[13])
    p.expect(0, "native FSRef capability gate")
    p.emit(0x807E00CC, 0x389E0190)
    original(13)
    p.expect(0, "original FSRef capability gate")
    p.emit(0x38600000, 0x389E0140)
    p.call(NAMES[12])
    p.expect(0, "native FSRef constructor capability gate")
    p.emit(0x38600000, 0x389E0190)
    original(12)
    p.expect(0, "original FSRef constructor capability gate")
    for offset in range(320, 480, 4):
        p.emit(0x807E0000 | offset)
        p.expect(0xA5A5A5A5, "unsupported FSRef leaves records untouched")
    release(200)
    release(204)
    for slot in (304, 308):
        release(slot)
    p.emit(0x387E0080)
    p.call("CloseConnection")
    p.expect(0, "oracle closure closes")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " URLComp Real Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("mode_arg", nargs="?", default="valid")
    parser.add_argument("--mode", choices=("valid", "foreign", "capability-failure", "bytes-failure"))
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.mode or args.mode_arg)
