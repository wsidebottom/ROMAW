#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_bundle_lifecycle_probe.py
#
#   Summary:   Original bundle lifecycle ABI fixtures and optional genuine-library comparisons.
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

"""Original bundle lifecycle ABI fixtures and optional genuine-library comparisons."""
import argparse
import plistlib
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_bundle_probe import write_symbols
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "CFBundleCreate",
    "CFBundleGetLocalInfoDictionary",
    "CFBundleUnloadExecutable",
    "CFBundleGetVersionNumber",
    "CFBundleGetDevelopmentRegion",
    "CFBundleGetFunctionPointersForNames",
    "CFBundleGetDataPointersForNames",
)
OFFSETS = (0xDA30, 0xDDA8, 0xF2C0, 0xE3CC, 0xE4F8, 0xF6C4, 0xF84C)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x38620700, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    routines[2] = (NAMES[2], OFFSETS[2], [0x380000AA, 0x98030024, 0x4E800020])
    routines[3] = (NAMES[3], OFFSETS[3], [0x3C6089AB, 0x6063CDEF, 0x4E800020])
    for i in (5, 6):
        routines[i] = (NAMES[i], OFFSETS[i], [0x2C050000, 0x4182000C, 0x3800005A, 0x90050000, 0x4E800020])
    routines += [
        (
            "CFDictionaryGetValue",
            0x21E74,
            [0x38020A80, 0x7C030040, 0x4082000C, 0x38620800, 0x4E800020, 0x38600000, 0x4E800020],
        ),
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFArrayGetCount", 0x1BD6C, [0x38600000, 0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x38620700, 0x4E800020]),
        ("BundleLifecycleToken", 0x100, [0x80620600, 0x4E800020]),
        ("BundleLifecyclePointer", 0x108, [0x38620800, 0x4E800020]),
    ]
    private = {0x1A98C: [0x4E800020], 0x1A990: [0x4E800020], 0xF4D0: [0x4E800020]}
    imports = []
    failures = {"create-failure": 0x1A98C, "unload-failure": 0xF4D0, "metadata-failure": 0x38110}
    if mode in failures:
        imports = [("InterfaceLib", "ROMAWBundleLifecycleFailure", 2)]
        trampoline = [0x81820180, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        offset = failures[mode]
        code[0x280 // 4 : 0x280 // 4 + len(trampoline)] = trampoline
        branch = [0x48000000 | ((0x280 - offset) & 0x03FFFFFC)]
        if offset in private:
            private[offset] = branch
        else:
            i = next(i for i, entry in enumerate(routines) if entry[1] == offset)
            routines[i] = (routines[i][0], offset, branch)
    for offset, words in private.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    exports = []
    for i, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, i * 8, offset, 0)
        exports.append((name, 2, i * 8, 1))
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x200, 0)
    words = [
        0x38600049,
        0x90620600,
        0x38620A80,
        0x9062082C,
        0x38620A40,
        0x90620814,
        0x90620820,
        0x38620A00,
        0x90620818,
        0x38600001,
        0x98620820,
        0x38600000,
        0x4E800020,
    ]
    # TOC+0x814 belongs to the bundle record too: global info and local info
    # are supplied by original initializer instructions, not host fixtures.
    if mode == "metadata-failure":
        words[7:9] = [0x38600000, 0x90620818]
    if mode == "unload-failure":
        words[9:11] = [0x38600000, 0x98620820]
    code[0x200 // 4 : 0x200 // 4 + len(words)] = words
    reloc = [0x4600 | len(routines)] + ([0xA000, 0x180, 0x4A00] if imports else [])
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
            + [("CoreFoundationLib", name, 2) for name in ("BundleLifecycleToken", "BundleLifecyclePointer")]
        )
        p.call("BundleLifecycleToken")
        p.expect(0x49, "actual PEF initializer ran")
        p.call("BundleLifecyclePointer")
        p.emit(0x907E00C0)
        if mode.endswith("failure"):
            index = 0 if mode == "create-failure" else 2 if mode == "unload-failure" else 1
            p.emit(0x38830000, 0x38600000) if not index else None
            p.call(NAMES[index])
            p.emit(0)
        else:
            p.emit(0x807E00C0, 0x38830000, 0x38600000)
            p.call(NAMES[0])
            p.emit(0x809E00C0, 0x3884FF00) if mode == "foreign" else p.emit(0x809E00C0)
            p.equal("constructor owns cached native bundle or delegates foreign profile")
            p.emit(0x807E00C0)
            p.call(NAMES[1])
            p.emit(0x809E00C0, 0x3884FF00 if mode == "foreign" else 0x38840200)
            p.equal("localized dictionary read or foreign delegation")
            p.emit(0x807E00C0)
            p.call(NAMES[3])
            p.expect(0x89ABCDEF if mode == "foreign" else 0, "version fallback preserves result ABI")
            p.emit(0x807E00C0)
            p.call(NAMES[4])
            if mode == "foreign":
                p.emit(0x809E00C0, 0x3884FF00)
                p.equal("foreign development-region entry")
            else:
                p.expect(0, "no development region on nonmain synthetic bundle")
            p.emit(0x807E00C0)
            p.call(NAMES[2])
            p.emit(0x807E00C0, 0x88630024)
            p.expect(0xAA if mode == "foreign" else 0, "void unload ABI")
            for name in NAMES[5:]:
                p.emit(0x38600000, 0x907E00D0, 0x807E00C0, 0x38830000, 0x38BE00D0)
                p.call(name)
                p.emit(0x807E00D0)
                p.expect(0x5A if mode == "foreign" else 0, "bulk void/output argument ABI")
        p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " BLife Probe")
        return
    dependencies = (
        "CFStringCreateWithCString",
        "CFURLCreateWithFileSystemPath",
        "CFRelease",
        "CFEqual",
        "CFBundleGetInfoDictionary",
        "CFDictionarySetValue",
        "CFDictionaryRemoveValue",
        "CFDictionaryGetValue",
        "CFDataCreate",
        "CFArrayCreate",
        "CFBundleIsExecutableLoaded",
    )
    p = Probe(
        [(library, name, 2) for name in NAMES]
        + [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection", "CountSymbols")]
        + [("CoreFoundationLib", name, 2) for name in dependencies]
    )
    p.data.extend(bytes(32768 - len(p.data)))
    cursor = 8192

    def c_text(text):
        nonlocal cursor
        raw = text.encode("mac_roman") + b"\0"
        offset = cursor
        p.data[offset : offset + len(raw)] = raw
        cursor += len(raw)
        assert cursor < 32768
        return offset

    def string(text, out):
        offset = c_text(text)
        p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000)
        p.call("CFStringCreateWithCString")
        p.nonzero("real CFString constructed")
        p.emit(0x907E0000 | out)

    def original(index):
        p.emit(0x819E0000 | (256 + index * 4), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)

    def release(slot):
        p.emit(0x807E0000 | slot)
        p.call("CFRelease")

    def equal_objects():
        p.emit(0x7C641B78, 0x807E00E8)
        p.call("CFEqual")
        p.nonzero("native/original objects compare equal")

    def set_value(dictionary, key, value):
        p.emit(0x807E0000 | dictionary, 0x809E0000 | key, 0x80BE0000 | value)
        p.call("CFDictionarySetValue")

    raw = b"CoreFoundationLib"
    p.data[1024 : 1024 + len(raw) + 1] = bytes([len(raw)]) + raw
    p.emit(0x387E0400, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE00C0, 0x38FE00C4, 0x39000000)
    p.call("GetSharedLibrary")
    p.expect(0, "open genuine oracle fragment")
    for i, name in enumerate(NAMES):
        offset = 1088 + i * 64
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
        p.emit(0x807E00C0, 0x389E0000 | offset, 0x38BE0000 | (256 + i * 4), 0x38DE00C8)
        p.call("FindSymbol")
        p.expect(0, "resolve original un-intercepted oracle entry")
    for index, label in enumerate(("Native", "Original")):
        folder = destination / (label + ".bundle")
        info = {
            "CFBundleIdentifier": "org.romaw.lifecycle." + label.lower(),
            "CFBundleExecutable": "BundleSymbols",
            "CFBundlePackageType": "BNDL",
            "CFBundleVersion": "1.2.3",
            "CFBundleDevelopmentRegion": "en",
        }
        for sub in (folder, folder / "Contents"):
            sub.mkdir(parents=True, exist_ok=True)
            (sub / "Info.plist").write_bytes(plistlib.dumps(info))
        for sub in (folder, folder / "Contents" / "MacOSClassic", folder / "Contents" / "MacOS"):
            sub.mkdir(parents=True, exist_ok=True)
            write_symbols(sub)
        for sub in (folder / "Resources", folder / "Contents" / "Resources"):
            sub.mkdir(parents=True, exist_ok=True)
            (sub / "InfoPlist.strings").write_bytes(plistlib.dumps({"LocalizedMarker": "actual localized value"}))
        path = str(folder.resolve()).replace("\\", ":").replace(":/", ":").replace("::", ":") + ":"
        string(path, 224)
        p.emit(0x809E00E0, 0x38600000, 0x38A00001, 0x38C00001)
        p.call("CFURLCreateWithFileSystemPath")
        p.nonzero("genuine bundle directory URL")
        p.emit(0x907E00E4, 0x7C641B78, 0x38600000)
        original(0) if index else p.call(NAMES[0])
        p.nonzero("native/original uncached bundle constructor")
        p.emit(0x907E0000 | (208 + index * 4))
        if not index:
            p.emit(0x809E00E4, 0x38600000)
            p.call(NAMES[0])
            p.emit(0x809E00D0)
            p.equal("repeated create retains the actual cached bundle")
            p.call("CFRelease")
        release(228)
        release(224)
    # Exercise the constructor's file-properties fallback as well as bundles
    # recognized by their Info.plist layout. Missing paths and files are rejected.
    empty = destination / "EmptyNative"
    empty.mkdir(exist_ok=True)
    other_empty = destination / "EmptyOriginal"
    other_empty.mkdir(exist_ok=True)
    for path, is_directory, expected in (
        (destination / "MissingBundle", 1, False),
        (destination / "Native.bundle" / "Info.plist", 0, False),
        (empty, 1, True),
        (other_empty, 1, True),
    ):
        text = str(path.resolve()).replace("\\", ":").replace(":/", ":").replace("::", ":")
        if is_directory:
            text += ":"
        string(text, 224)
        p.emit(0x809E00E0, 0x38600000, 0x38A00001, 0x38C00000 | is_directory)
        p.call("CFURLCreateWithFileSystemPath")
        p.nonzero("real validation URL")
        p.emit(0x907E00E4)
        for oracle in (False, True):
            if expected and oracle != (path == other_empty):
                continue
            p.emit(0x809E00E4, 0x38600000)
            original(0) if oracle else p.call(NAMES[0])
            if expected:
                p.nonzero("ordinary directory accepted by file-properties fallback")
                p.call("CFRelease")
            else:
                p.expect(0, "file and missing directory do not construct a bundle")
        release(228)
        release(224)
    # Dictionaries are distinct real objects. Mutation in one cannot hide a
    # parser difference by caching its result before the oracle reads the input.
    for index, slot in enumerate((208, 212)):
        p.emit(0x807E0000 | slot)
        p.call("CFBundleGetInfoDictionary")
        p.nonzero("real mutable info dictionary")
        p.emit(0x907E0000 | (216 + index * 4))
    for key, out in (("CFBundleNumericVersion", 288), ("CFBundleVersion", 292), ("CFBundleDevelopmentRegion", 296)):
        string(key, out)
    versions = [
        "",
        "1",
        "1.",
        "12",
        ".",
        ".1",
        "1.2",
        "1.2.",
        "1.2.3",
        "a",
        "a1",
        "b",
        "d",
        "123",
        "1.2f1",
        "1.2.3.4",
        "1.2b12x",
        "12345678901",
    ]
    versions += [
        base + stage + str(build)
        for base in ("1.2", "12.3.4")
        for stage in "dab"
        for build in (0, 1, 9, 10, 99, 100, 254, 255, 256, 999)
    ]
    for text in versions:
        string(text, 224)
        for dictionary in (216, 220):
            set_value(dictionary, 288, 224)
        p.emit(0x807E00D4)
        original(3)
        p.emit(0x907E00E8, 0x807E00D0)
        p.call(NAMES[3])
        p.emit(0x809E00E8)
        p.equal("version grammar and cache agree with original: " + repr(text))
        release(224)
    # Fallback version key and invalid-type removal are separately observable.
    for dictionary in (216, 220):
        p.emit(0x807E0000 | dictionary, 0x809E0120)
        p.call("CFDictionaryRemoveValue")
    p.emit(0x807E00D4)
    original(3)
    p.emit(0x907E00E8, 0x807E00D0)
    p.call(NAMES[3])
    p.emit(0x809E00E8)
    p.equal("fallback version key parsing")
    p.emit(0x38600000, 0x38800000, 0x38A00000)
    p.call("CFDataCreate")
    p.nonzero("real non-string metadata object")
    p.emit(0x907E00E0)
    for dictionary in (216, 220):
        set_value(dictionary, 288, 224)
    for index, slot in enumerate((208, 212)):
        p.emit(0x807E0000 | slot)
        original(3) if index else p.call(NAMES[3])
        p.expect(0, "non-number/string version is rejected")
        p.emit(0x807E0000 | (216 + index * 4), 0x809E0120)
        p.call("CFDictionaryGetValue")
        p.expect(0, "invalid numeric version cache is removed")
    for value in ("en", "", "fr_CA"):
        string(value, 236)
        for dictionary in (216, 220):
            set_value(dictionary, 296, 236)
        p.emit(0x807E00D4)
        original(4)
        p.emit(0x907E00E8, 0x807E00D0)
        p.call(NAMES[4])
        p.emit(0x809E00E8)
        p.equal("development region filtering agrees with original")
        release(236)
    for dictionary in (216, 220):
        set_value(dictionary, 296, 224)
    p.emit(0x807E00D0)
    p.call(NAMES[4])
    p.expect(0, "non-string development region removed")
    p.emit(0x807E00D4)
    original(4)
    p.expect(0, "original rejects same non-string region")
    release(224)
    p.emit(0x807E00D4)
    original(1)
    p.nonzero("original loads actual InfoPlist.strings")
    p.emit(0x907E00E8, 0x807E00D0)
    p.call(NAMES[1])
    p.nonzero("native loads actual InfoPlist.strings")
    equal_objects()
    # Repeat cached localized lookups after the real resource/data/plist path.
    p.emit(0x807E00D0)
    p.call(NAMES[1])
    equal_objects()
    for i, name in enumerate(("testCode", "testData", "missing", "testCode")):
        string(name, 320 + i * 4)
    p.emit(0x38600000, 0x389E0140, 0x38A00004, 0x38C00000)
    p.call("CFArrayCreate")
    p.nonzero("real array of symbol-name strings")
    p.emit(0x907E00F0)
    for api in (5, 6, 5):
        for i in range(12):
            p.emit(0x3C60A5A5, 0x6063A5A5, 0x907E0000 | (384 + i * 4))
        p.emit(0x807E00D0, 0x809E00F0, 0x38BE0184)
        p.call(NAMES[api])
        p.emit(0x807E00D4, 0x809E00F0, 0x38BE019C)
        original(api)
        for index in range(4):
            p.emit(0x807E0000 | (388 + index * 4))
            valid = index == 1 if api == 6 else index in (0, 3)
            if valid:
                p.nonzero("native bulk lookup finds correct symbol class")
                if api == 5:
                    p.emit(0x7C6C1B78, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
                    p.expect(42, "native bulk function result")
                else:
                    p.emit(0x80630000)
                    p.expect(0x49, "native bulk data observes real initializer")
            else:
                p.expect(0, "native bulk wrong-class/missing symbol result")
            p.emit(0x807E0000 | (412 + index * 4))
            if valid:
                p.nonzero("oracle bulk lookup finds correct symbol class")
                if api == 5:
                    p.emit(0x7C6C1B78, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
                    p.expect(42, "original bulk function result")
                else:
                    p.emit(0x80630000)
                    p.expect(0x49, "original bulk data result")
            else:
                p.expect(0, "original bulk wrong-class/missing symbol result")
        for offset in (384, 404, 408, 428):
            p.emit(0x807E0000 | offset)
            p.expect(0xA5A5A5A5, "bulk pointer array guards")
        for index, slot in enumerate((208, 212)):
            p.emit(0x807E0000 | slot, 0x80030028, 0x901E0000 | (512 + index * 4))
            original(2) if index else p.call(NAMES[2])
            p.emit(0x807E0000 | slot)
            p.call("CFBundleIsExecutableLoaded")
            p.expect(0, "unload clears executable-loaded flag like the original")
            p.emit(0x807E0000 | slot, 0x80630028)
            p.expect(0, "unload clears real connection state")
        for index in range(2):
            p.emit(0x807E0000 | (512 + index * 4), 0x389E0208)
            p.call("CountSymbols")
            p.emit(0x907E0000 | (528 + index * 4))
        p.emit(0x807E0210, 0x809E0214)
        p.equal("original/native actual CFM mapping status after unload")
        # A reference bug may clear only the bundle flag. Release any retained
        # mappings through the documented pointer-to-ID CFM API in this fixture.
        for index in range(2):
            p.emit(0x387E0000 | (512 + index * 4))
            p.call("CloseConnection")
            p.emit(0x809E0000 | (528 + index * 4))
            p.equal("explicit CFM cleanup agrees with observed mapping state")
        # Idempotence after unload.
        p.emit(0x807E00D0)
        p.call(NAMES[2])
    release(240)
    for slot in (320, 324, 328, 332, 288, 292, 296, 208, 212):
        release(slot)
    p.emit(0x387E00C0)
    p.call("CloseConnection")
    p.expect(0, "oracle closure released")
    p.finish(destination, "PPC " + ("Carbon" if library == "CarbonLib" else "Core") + " BLife Real Probe")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode_arg", nargs="?", default="valid")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument("--real", action="store_true")
    parser.add_argument(
        "--mode", default=None, choices=("valid", "foreign", "create-failure", "unload-failure", "metadata-failure")
    )
    args = parser.parse_args()
    make_probe(args.destination, args.library, args.real, args.mode or args.mode_arg)
