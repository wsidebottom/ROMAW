#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_bundle_state_probe.py
#
#   Summary:   Bundle state/load batch with original PPC dependencies and a real-library oracle.
#
#   Classes:   None declared.
#
#   Functions: write_reference, make_state_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Bundle state/load batch with original PPC dependencies and a real-library oracle."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_bundle_probe import write_symbols
from make_ppc_carbon_native_probe import write_carbon_fallback
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef

NAMES = (
    "CFBundleGetTypeID",
    "CFBundleCopyBundleURL",
    "CFBundleGetInfoDictionary",
    "CFBundleGetValueForInfoDictionaryKey",
    "CFBundleGetIdentifier",
    "CFBundleIsExecutableLoaded",
    "CFBundleLoadExecutable",
    "CFBundleGetPlugIn",
)
OFFSETS = (0xD7E8, 0xDB20, 0xDD2C, 0xDEDC, 0xDF74, 0xF184, 0xF18C, 0xF8D0)


def write_reference(destination, mode):
    code = [0] * ((0x5DEE0 + (4 if mode == "foreign" else 0)) // 4)
    data = bytearray(0x72A8)
    routines = [(name, offset, [0x3C6089AB, 0x6063CDEF, 0x4E800020]) for name, offset in zip(NAMES, OFFSETS)]
    for i in (1, 2, 3, 4, 7):
        name, offset, _ = routines[i]
        routines[i] = (name, offset, [0x38620700, 0x4E800020])
    routines += [
        ("CFRetain", 0x533E4, [0x4E800020]),
        ("CFRelease", 0x53488, [0x4E800020]),
        ("CFGetAllocator", 0x53844, [0x38600000, 0x4E800020]),
        ("CFBundleGetLocalInfoDictionary", 0xDDA8, [0x80620620, 0x4E800020]),
        ("CFDictionaryGetValue", 0x21E74, [0x80630000, 0x4E800020]),
        ("CFBundleCopyExecutableURL", 0xF0F8, [0x80620610, 0x4E800020]),
        ("CFSetRemoveValue", 0x1FAB8, [0x4E800020]),
        ("__CFStringMakeConstantString", 0x38110, [0x4E800020]),
        ("CFLog", 0x403FC, [0x4E800020]),
        ("BundleStateConfigure", 0x100, [0x90620610, 0x90820614, 0x90A20618, 0x90C2061C, 0x90E20620, 0x4E800020]),
        ("BundleStateToken", 0x120, [0x80620600, 0x4E800020]),
        ("BundleStatePointer", 0x128, [0x38620700, 0x4E800020]),
    ]
    private = {
        0x13ADC: [0x8062061C, 0x4E800020],
        0xFD2C: [0x2C060000, 0x4082000C, 0x80620614, 0x4E800020, 0x80620618, 0x4E800020],
        0xF4D0: [0x4E800020],
        0x1A98C: [0x4E800020],
        0x1A990: [0x4E800020],
    }
    imports = []
    failures = {
        "info-failure": (0x13ADC, "Info"),
        "fragment-failure": (0xFD2C, "Fragment"),
        "cleanup-failure": (0xF4D0, "Cleanup"),
        "retain-failure": (0x533E4, "Retain"),
        "executable-failure": (0xF0F8, "Executable"),
        "log-failure": (0x403FC, "Log"),
    }
    if mode in failures:
        offset, suffix = failures[mode]
        imports = [("InterfaceLib", "ROMAWBundleState" + suffix + "Failure", 2)]
        trampoline = [0x81820180, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        if offset in private:
            private[offset] = trampoline
        else:
            index = next(i for i, entry in enumerate(routines) if entry[1] == offset)
            name, _, _ = routines[index]
            routines[index] = (name, offset, trampoline)
    for offset, words in private.items():
        code[offset // 4 : offset // 4 + len(words)] = words
    exports = []
    for i, (name, offset, words) in enumerate(routines):
        code[offset // 4 : offset // 4 + len(words)] = words
        struct.pack_into(">II", data, i * 8, offset, 0)
        exports.append((name, 2, i * 8, 1))
    init = len(routines) * 8
    struct.pack_into(">II", data, init, 0x200, 0)
    code[0x200 // 4 : 0x200 // 4 + 8] = [
        0x38600049,
        0x90620600,
        0x38621000,
        0x90620408,
        0x3860002A,
        0x90620834,
        0x38600000,
        0x4E800020,
    ]
    reloc = [0x4600 | len(routines)] + ([0xA000, 0x180, 0x4A00] if imports else [])
    write_fragment(
        destination, "CoreFoundationLib", pef(code, data, imports=imports, exports=exports, init=init, reloc=reloc), 0
    )


def make_state_probe(
    destination, library="CarbonLib", real=False, mode="valid", private_copy=False, original_load=False
):
    if not real:
        write_carbon_fallback(destination)
        write_reference(destination, mode)
    imports = [(library, name, 2) for name in NAMES]
    if real:
        imports += [("CarbonLib", name, 2) for name in ("GetSharedLibrary", "FindSymbol", "CloseConnection")]
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in ("CFURLCreateWithFileSystemPath", "CFBundleCreate", "CFStringCreateWithCString", "CFRelease")
        ]
        if private_copy:
            imports += [
                (library, name, 2) for name in ("CFBundleGetFunctionPointerForName", "CFBundleGetDataPointerForName")
            ]
            imports += [("CoreFoundationLib", "CFBundleUnloadExecutable", 2)]
    else:
        imports += [
            ("CoreFoundationLib", name, 2)
            for name in ("BundleStateConfigure", "BundleStateToken", "BundleStatePointer")
        ]
        imports += [("CarbonLib", "CarbonFallbackToken", 2)]
    p = Probe(imports)
    p.data.extend(bytes(4096))

    def setup(index):
        if index:
            p.emit(0x807E00D8 if real else 0x387E0600)
        if index == 3:
            p.emit(0x809E00DC if real else 0x389E0690)

    def configure(executable=True, first=True, second=False, local=False):
        p.emit(
            0x387E06A0 if executable else 0x38600000,
            0x389E06B0 if first else 0x38800000,
            0x38BE06B0 if second else 0x38A00000,
            0x38DE06C0,
            0x38FE06C8 if local else 0x38E00000,
        )
        p.call("BundleStateConfigure")

    def reset(kind=1, loaded=0):
        p.emit(
            0x387E0680,
            0x907E0608,
            0x38600000,
            0x907E0614,
            0x907E0618,
            0x907E0628,
            0x38600000 | kind,
            0x907E0620,
            0x38600000 | loaded,
            0x987E0624,
        )

    def check_load(expected):
        setup(6)
        p.call(NAMES[6])
        p.expect(0xEF if mode == "foreign" else expected, "native load result")

    if real:
        for offset, text in ((256, "CoreFoundationLib"), *[(320 + i * 64, name) for i, name in enumerate(NAMES)]):
            raw = text.encode()
            p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE0090, 0x38FE0094, 0x39000000)
        p.call("GetSharedLibrary")
        p.expect(0, "original bundle state oracle connection")
        for i, name in enumerate(NAMES):
            p.emit(0x807E0090, 0x389E0000 | (320 + i * 64), 0x38BE0000 | (160 + i * 4), 0x38DE00D0)
            p.call("FindSymbol")
            p.expect(0, "original bundle state oracle export")
            p.emit(0x807E0000 | (160 + i * 4), 0x80630000, 0x80630000, 0x5463000A, 0x6C631800)
            p.nonzero("oracle retains original PPC code")
        bundle_dir = destination / "BundleState.bundle"
        bundle_dir.mkdir(parents=True, exist_ok=True)
        plist = """<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>org.romaw.bundle-state</string>
<key>CFBundleExecutable</key><string>BundleSymbols</string><key>CFBundlePackageType</key><string>BNDL</string>
</dict></plist>"""
        for subdir in (bundle_dir, bundle_dir / "Contents"):
            subdir.mkdir(parents=True, exist_ok=True)
            (subdir / "Info.plist").write_text(plist, encoding="utf-8")
        for subdir in (bundle_dir, bundle_dir / "Contents" / "MacOSClassic", bundle_dir / "Contents" / "MacOS"):
            subdir.mkdir(parents=True, exist_ok=True)
            if private_copy:
                write_symbols(subdir)
        path = str(bundle_dir.resolve()).replace("\\", ":").replace(":\\", ":").replace(":/", ":")
        # Windows drive colon is already present; HFS separators replace the rest.
        path = path.replace("::", ":") + ":"
        raw = path.encode() + b"\0"
        p.data[2304 : 2304 + len(raw)] = raw
        p.emit(0x38600000, 0x389E0900, 0x38A00000)
        p.call("CFStringCreateWithCString")
        p.nonzero("real HFS bundle path string")
        p.emit(0x907E00E8, 0x38830000, 0x38600000, 0x38A00001, 0x38C00001)
        p.call("CFURLCreateWithFileSystemPath")
        p.nonzero("real bundle directory URL")
        p.emit(0x907E00EC, 0x38830000, 0x38600000)
        p.call("CFBundleCreate")
        p.nonzero("genuine filesystem bundle constructed by original runtime")
        p.emit(0x907E00D8)
        raw = b"CFBundleIdentifier\0"
        p.data[2048 : 2048 + len(raw)] = raw
        p.emit(0x38600000, 0x389E0800, 0x38A00000)
        p.call("CFStringCreateWithCString")
        p.nonzero("real metadata key")
        p.emit(0x907E00DC)
        for repeat in range(3):
            for index, name in enumerate(NAMES):
                setup(index)
                if index == 6 and original_load:
                    p.emit(0x819E00B8, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
                else:
                    p.call(name)
                p.emit(0x907E00E0)
                setup(index)
                p.emit(
                    0x819E0000 | (160 + index * 4),
                    0x800C0000,
                    0x804C0004,
                    0x7C0903A6,
                    0x4E800421,
                    0x7FC2F378,
                    0x809E00E0,
                )
                p.equal(f"{name} original/native filesystem-bundle state pass {repeat}")
                if index in (0, 1, 2, 3, 4):
                    p.nonzero("genuine bundle type, URL and metadata are populated")
                if index == 6:
                    p.emit(0x807E00D8, 0x80630020)
                    p.expect(1 if private_copy else 7, "loaded CFM or missing executable kind")
                    if private_copy:
                        p.emit(0x807E00D8, 0x80630028)
                        p.nonzero("genuine private CFM connection published in bundle")
                if index == 1:
                    p.emit(0x907E00E4, 0x807E00E0)
                    p.call("CFRelease")
                    p.emit(0x807E00E4)
                    p.call("CFRelease")
        if private_copy:
            for offset, text, api in (
                (2600, "testCode", "CFBundleGetFunctionPointerForName"),
                (2700, "testData", "CFBundleGetDataPointerForName"),
            ):
                raw = text.encode() + b"\0"
                p.data[offset : offset + len(raw)] = raw
                p.emit(0x38600000, 0x389E0000 | offset, 0x38A00000)
                p.call("CFStringCreateWithCString")
                p.nonzero("real CFM symbol name")
                p.emit(0x907E00F4, 0x38830000, 0x807E00D8)
                p.call(api)
                p.nonzero("symbol belongs to the real private CFM connection")
                if api.endswith("FunctionPointerForName"):
                    p.emit(0x7C6C1B78, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
                    p.expect(42, "real private executable function runs")
                else:
                    p.emit(0x80630000)
                    p.expect(0x49, "real private executable initializer populated its data")
                p.emit(0x807E00F4)
                p.call("CFRelease")
            p.emit(0x807E00D8)
            p.call("CFBundleUnloadExecutable")
            setup(5)
            p.call(NAMES[5])
            p.expect(0, "original bundle unload closes the private connection")
        for slot in (0xDC, 0xD8, 0xEC, 0xE8):
            p.emit(0x807E0000 | slot)
            p.call("CFRelease")
        p.emit(0x387E0090)
        p.call("CloseConnection")
        p.expect(0, "original bundle oracle closes")
    else:
        p.call("BundleStateToken")
        p.expect(0x49, "original bundle state initializer ran")
        p.call("BundleStatePointer")
        p.emit(0x907E00F0)
        configure()
        reset()
        p.emit(0x387E0690, 0x907E06C0, 0x387E0698, 0x907E06C8)
        for i in (0, 1, 2, 3, 4, 5, 7):
            setup(i)
            p.call(NAMES[i])
            if mode == "foreign":
                if i in (0, 5):
                    p.expect(0xEF if i == 5 else 0x89ABCDEF, "foreign bundle scalar result")
                else:
                    p.emit(0x809E00F0)
                    p.equal("foreign bundle pointer retains valid guest identity")
            elif i == 0:
                p.expect(42, "live bundle class type")
            elif i == 1:
                p.pointer(1664, "copy retains original URL")
            elif i == 2:
                p.pointer(1728, "metadata helper cached in original bundle")
            elif i in (3, 4):
                p.pointer(1680, "metadata identifier and nonlocalized value")
            else:
                p.expect(0, "fresh bundle flags")
        configure(local=True)
        setup(3)
        p.call(NAMES[3])
        if mode == "foreign":
            p.emit(0x809E00F0)
            p.equal("foreign localized value")
        else:
            p.pointer(1688, "localized metadata takes precedence")
        configure()
        reset()
        check_load(1)
        if mode != "foreign":
            p.emit(0x807E0628)
            p.pointer(1712, "CFM result stored in original bundle")
        check_load(1)
        configure(first=False, second=True)
        reset()
        check_load(1)
        configure(first=False)
        reset()
        check_load(0)
        if mode != "foreign":
            p.emit(0x807E0620)
            p.expect(6, "failed CFM discovery marks unusable kind")
        configure(executable=False)
        reset()
        check_load(0)
        configure()
        reset(kind=0)
        check_load(0)
        p.call("CarbonFallbackToken")
        p.expect(0x49, "unimplemented main exports keep initialized fallback")
    label = "Carbon" if library == "CarbonLib" else "Core"
    p.finish(destination, f"PPC {label} BState {'Real ' if real else ''}Probe", carbon=real)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "mode",
        nargs="?",
        default="valid",
        choices=(
            "valid",
            "foreign",
            "info-failure",
            "fragment-failure",
            "cleanup-failure",
            "retain-failure",
            "executable-failure",
            "log-failure",
        ),
    )
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    parser.add_argument(
        "--private-copy",
        action="store_true",
        help="Include a real CFM executable; currently exposes the retained private-copy diagnostic",
    )
    parser.add_argument(
        "--original-load",
        action="store_true",
        help="Use the original PPC load entry for the private-copy diagnostic comparison",
    )
    args = parser.parse_args()
    if args.real and args.mode != "valid":
        parser.error("Real state probe requires valid mode")
    if (args.private_copy or args.original_load) and not args.real:
        parser.error("Private-copy comparisons require --real")
    if args.original_load and not args.private_copy:
        parser.error("Original-load comparisons require --private-copy")
    make_state_probe(args.destination, args.library, args.real, args.mode, args.private_copy, args.original_load)
