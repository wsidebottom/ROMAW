#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_private_copy_probe.py
#
#   Summary:   Original CFM private-copy isolation, visibility, imports, init and close probes.
#
#   Classes:   None declared.
#
#   Functions: dependency, module, make_private_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original CFM private-copy isolation, visibility, imports, init and close probes."""
import argparse
import struct
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def dependency(destination):
    code = [0x80620020, 0x38630001, 0x90620020, 0x4E800020, 0x80620024, 0x38630001, 0x90620024, 0x4E800020]
    data = bytearray(64)
    struct.pack_into(">IIII", data, 0, 0, 0, 16, 0)
    write_fragment(
        destination,
        "PrivateDependency",
        pef(
            code,
            data,
            exports=[
                ("Initialized", 2, 0, 1),
                ("Terminated", 2, 8, 1),
                ("InitCount", 1, 32, 1),
                ("TermCount", 1, 36, 1),
            ],
            reloc=[0x4601],
        ),
        0,
    )


def module(failure="valid"):
    code = [0x80620040, 0x4E800020, 0x80620040, 0x38630001, 0x90620040, 0x4E800020]
    init = len(code) * 4
    # Save our TOC across the imported initializer counter; preserve CFM's own connection ID.
    code += [
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x80030008,
        0x90020044,
        0x80030004,
        0x90020048,
        0x38600001,
        0x90620040,
        0x81820028,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x7FC2F378,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x38600000,
        0x4E800020,
    ]
    if failure == "init-error":
        code[-2] = 0x3860FFCE
    term = len(code) * 4
    code += [
        0x80020040,
        0x2C0000DE,
        0x40820008,
        0,
        0x380000DE,
        0x90020040,
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x8182002C,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x7FC2F378,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x38600000,
        0x4E800020,
    ]
    data = bytearray(128)
    for offset, address in ((0, 0), (8, 8), (16, init), (24, term)):
        struct.pack_into(">II", data, offset, address, 0)
    imports = [("PrivateDependency", "Initialized", 2), ("PrivateDependency", "Terminated", 2)]
    if failure == "term-error":
        code[code.index(0x8182002C)] = 0x81820030
        imports.append(("InterfaceLib", "ROMAWPrivateTermFailure", 2))
    return pef(
        code,
        data,
        imports=imports,
        exports=[
            ("GetValue", 2, 0, 1),
            ("Bump", 2, 8, 1),
            ("State", 1, 64, 1),
            ("OwnConnection", 1, 68, 1),
            ("OwnClosure", 1, 72, 1),
        ],
        main=0,
        init=16,
        term=24,
        reloc=[0x4603, 0xA000, 40, 0x4A02 if failure == "term-error" else 0x4A01],
    )


def make_private_probe(destination, kind="named", failure="valid"):
    destination.mkdir(parents=True, exist_ok=True)
    dependency(destination)
    blob = module(failure)
    module_name = "PrivateDiskModule" if kind == "disk" else "PrivateModule"
    if kind != "memory":
        # Disk-only containers are not registered as import libraries.
        write_fragment(destination, module_name, blob, int(kind == "disk"))
    imports = [
        ("InterfaceLib", name, 2)
        for name in (
            "GetSharedLibrary",
            "GetDiskFragment",
            "GetMemFragment",
            "FindSymbol",
            "CloseConnection",
            "FSMakeFSSpec",
        )
    ]
    imports += [("PrivateDependency", name, 1) for name in ("InitCount", "TermCount")]
    memory_exit = kind == "memory" and failure == "valid"
    if memory_exit:
        imports.append(("InterfaceLib", "ExitToShell", 2))
    p = Probe(imports)
    p.data.extend(bytes(8192))
    for offset, name in (
        (256, module_name),
        (288, "State"),
        (320, "GetValue"),
        (352, "Bump"),
        (384, "OwnConnection"),
        (464, "OwnClosure"),
    ):
        raw = name.encode()
        p.data[offset : offset + len(raw) + 1] = bytes([len(raw)]) + raw
    p.data[2048 : 2048 + len(blob)] = blob

    # Start from the shared dependency's current counters when a previous
    # case ran in the same application.
    p.emit(0x807E0020, 0x80630000, 0x907E05F0)
    p.emit(0x807E0024, 0x80630000, 0x907E05F4)

    def check_counter(import_offset, baseline_offset, increment, description):
        p.emit(0x807E0000 | import_offset, 0x80630000, 0x809E0000 | baseline_offset, 0x7C641850)
        p.expect(increment, description)

    if kind == "disk":
        (destination / "NotAFragment").mkdir(exist_ok=True)
        p.data[400:413] = b"\x0cNotAFragment"
        p.emit(0x38600000, 0x38800000, 0x38BE0190, 0x38DE0600)
        p.call("FSMakeFSSpec")
        p.expect(0, "directory FSSpec is a valid discovery candidate")
        p.emit(0x387E0600, 0x38800000, 0x38A00000, 0x38DE0100, 0x38E00005, 0x391E00C0, 0x393E00E0, 0x395E0700)
        p.call("GetDiskFragment")
        p.expect(0xFFFFFFCA, "directory open failure is a recoverable CFM error")
        p.emit(0x807E00C0)
        p.expect(0, "failed private preparation does not publish a connection")
        p.emit(0x38600000, 0x38800000, 0x38BE0100, 0x38DE0600)
        p.call("FSMakeFSSpec")
        p.expect(0, "real disk fragment FSSpec")

    def load(flags, slot, expected=0):
        if kind == "named":
            p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00000 | flags, 0x38DE0000 | slot, 0x38FE00E0, 0x391E0700)
            p.call("GetSharedLibrary")
        elif kind == "disk":
            p.emit(
                0x387E0600,
                0x38800000,
                0x38A00000,
                0x38DE0100,
                0x38E00000 | flags,
                0x391E0000 | slot,
                0x393E00E0,
                0x395E0700,
            )
            p.call("GetDiskFragment")
        else:
            p.emit(
                0x387E0800,
                0x38800000 | len(blob),
                0x38BE0100,
                0x38C00000 | flags,
                0x38FE0000 | slot,
                0x391E00E0,
                0x393E0700,
            )
            p.call("GetMemFragment")
        p.expect(expected, "private/shared/find load result")

    def symbol(connection, name_offset, out, expected=0):
        p.emit(0x807E0000 | connection, 0x389E0000 | name_offset, 0x38BE0000 | out, 0x38DE00F0)
        p.call("FindSymbol")
        p.expect(expected, "connection symbol lookup")

    def invoke(slot, expected):
        p.emit(0x819E0000 | slot, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x7FC2F378)
        p.expect(expected, "private data mutation")

    def named_absent():
        p.emit(0x387E0100, 0x3C807077, 0x60847063, 0x38A00002, 0x38DE00C8, 0x38FE00E0, 0x391E0700)
        p.call("GetSharedLibrary")
        p.expect(0xFFFFF50C, "private connections are invisible to named find")

    load(2, 192, 0xFFFFF50C)
    load(5, 192)
    if kind == "disk":
        p.emit(0x3860007F, 0x987E063F)  # FSSpec bytes past its Pascal name are not file identity.
    load(5, 196)
    p.emit(0x807E00C0, 0x809E00C4)
    p.nonzero("first private connection")
    p.emit(0x7C032000, 0x40820008, 0)
    named_absent()
    symbol(192, 288, 208)
    symbol(196, 288, 212)
    p.emit(0x807E00D0, 0x809E00D4, 0x7C032000, 0x40820008, 0)
    symbol(192, 384, 216)
    p.emit(0x807E00D8, 0x80630000, 0x809E00C0)
    p.emit(0x7C032000, 0x40820008, 0)
    p.emit(0x907E00F8, 0x387E00F8)
    p.call("CloseConnection")
    p.expect(0xFFFFF50F, "initializer connection cannot release a closure")
    symbol(192, 464, 216)
    p.emit(0x807E00D8, 0x80630000, 0x809E00C0)
    p.equal("initializer sees its own private closure")
    symbol(192, 352, 224)
    invoke(224, 2)
    symbol(196, 320, 224)
    invoke(224, 1)
    # Both transition vectors must share code while keeping distinct data TOCs.
    symbol(192, 320, 224)
    symbol(196, 320, 228)
    p.emit(0x807E00E0, 0x80630000, 0x809E00E4, 0x80840000)
    p.equal("private copies share PIC code")
    load(1, 200)
    symbol(200, 320, 232)
    invoke(232, 1)
    load(2, 204)
    symbol(200, 384, 216)
    p.emit(0x807E00D8, 0x80630000, 0x809E00CC)
    p.equal("find borrows the shared connection")
    p.emit(0x807E00CC, 0x907E00F8, 0x387E00F8)
    p.call("CloseConnection")
    p.expect(0xFFFFF50F, "borrowed connection cannot release a closure")
    p.emit(0x807E00F8, 0x809E00CC)
    p.equal("failed close preserves borrowed connection")
    load(1, 252)
    p.emit(0x807E00C8, 0x809E00FC, 0x7C032000, 0x40820008, 0)
    p.emit(0x387E00C8)
    p.call("CloseConnection")
    p.expect(0, "one shared closure closes without unloading another")
    invoke(232, 1)
    check_counter(0x20, 0x5F0, 3, "each private initializer runs with the same shared dependency")
    p.emit(0x807E00C0, 0x907E00EC, 0x387E00C0)
    p.call("CloseConnection")
    p.expect(0, "close private connection")
    p.emit(0x807E00C0)
    p.expect(0, "close clears its output")
    symbol(236, 320, 224, 0xFFFFF50F)
    check_counter(0x24, 0x5F4, 1, "private terminator executes at close")
    invoke(228, 1)
    invoke(232, 1)
    p.emit(0x387E00C4)
    p.call("CloseConnection")
    p.expect(0, "second private connection closes independently")
    check_counter(0x24, 0x5F4, 2, "each private terminator executes once")
    load(5, 192)
    symbol(192, 320, 224)
    invoke(224, 1)
    p.emit(0x387E00C0)
    p.call("CloseConnection")
    p.expect(0, "later private instance closes")
    p.emit(0x387E00FC)
    p.call("CloseConnection")
    p.expect(0, "last shared closure closes")
    symbol(204, 320, 224, 0xFFFFF50F)
    load(2, 200, 0xFFFFF50C)
    load(1, 200)
    symbol(200, 320, 232)
    invoke(232, 1)
    symbol(200, 384, 216)
    p.emit(0x807E00D8, 0x80630000, 0x809E00CC, 0x7C032000, 0x40820008, 0)
    p.emit(0x387E00C8)
    p.call("CloseConnection")
    p.expect(0, "reloaded shared closure closes")
    check_counter(0x24, 0x5F4, 5, "all private and shared lifetimes terminate once")
    raw = b"PeerNativeInterfaceLib"
    p.data[432 : 432 + len(raw) + 1] = bytes([len(raw)]) + raw
    for flags, expected in ((2, 0xFFFFF50C), (5, 0xFFFFFFFC), (2, 0xFFFFF50C)):
        p.emit(0x387E01B0, 0x3C807077, 0x60847063, 0x38A00000 | flags, 0x38DE00C0, 0x38FE00E0, 0x391E0700)
        p.call("GetSharedLibrary")
        p.expect(expected, "native-only private rejection does not create a visible connection")
    load(99, 192, 0xFFFFFFCE)
    if memory_exit:
        p.call("ExitToShell")
        p.emit(0)  # The Toolbox exit must not return to the application.
    # Fill startup AOT before the first memory fragment links. These branch
    # blocks follow the main routine and have no incoming control flow.
    padding = [0x48000004] * 4097 if memory_exit else ()
    p.finish(destination, "PPC Private " + kind.title() + " Probe", trailing_words=padding)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("kind", nargs="?", choices=("named", "disk", "memory"), default="named")
    parser.add_argument("failure", nargs="?", choices=("valid", "init-error", "term-error"), default="valid")
    args = parser.parse_args()
    make_private_probe(args.destination, args.kind, args.failure)
