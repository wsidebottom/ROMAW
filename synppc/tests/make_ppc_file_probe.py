#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_file_probe.py
#
#   Summary:   Original PPC checks for guest-endian synchronous file I/O.
#
#   Classes:   None declared.
#
#   Functions: make_file_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC checks for guest-endian synchronous file I/O."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_file_probe(destination):
    names = [
        "GetCurrentProcess",
        "GetProcessInformation",
        "HSetVol",
        "FSOpen",
        "FSClose",
        "FSRead",
        "FSWrite",
        "GetEOF",
        "GetFPos",
        "SetFPos",
        "FlushVol",
        "NewAliasMinimalFromFullPath",
        "DisposeHandle",
        "PBDTGetCommentSync",
        "PBDTGetCommentAsync",
        "SameProcess",
        "Get1Resource",
        "RemoveResource",
        "ResError",
        "GetNextProcess",
        "PBDTGetAPPLSync",
        "PBDTGetAPPLAsync",
        "NewAlias",
        "ResolveAlias",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def unequal():
        failures.append(len(code))
        emit(0x40820000)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        unequal()

    def reference():
        emit(0xA87E0110)

    def amount(value):
        emit(0x38600000 | (value & 0xFFFF), 0x907E0114)

    def seek(mode, offset):
        reference()
        emit(0x38800000 | mode, 0x38A00000 | (offset & 0xFFFF))
        call("SetFPos")
        check(0)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    emit(0x387E0070)
    call("GetCurrentProcess")
    check(0)
    # Enumerate the live process from kNoProcess, then reach the list end.
    # The serial occupies eight bytes; preserve the following longword.
    emit(0x38600000, 0x907E01C0, 0x907E01C4, 0x38601234, 0x907E01C8, 0x387E01C0)
    call("GetNextProcess")
    check(0)
    for offset in (0, 4):
        emit(0x807E01C0 + offset, 0x809E0070 + offset, 0x7C032000)
        unequal()
    emit(0x807E01C8)
    check(0x1234)
    emit(0x387E01C0)
    call("GetNextProcess")
    check(-600)
    emit(0x807E01C0)
    check(0)
    emit(0x807E01C4)
    check(0)
    emit(0x807E01C8)
    check(0x1234)
    emit(0x38600000)
    call("GetNextProcess")
    check(-50)
    emit(0x38601234, 0x907E01C4, 0x387E01C0)
    call("GetNextProcess")
    check(-50)
    emit(0x807E01C4)
    check(0x1234)
    # RemoveResource detaches the resource without disposing the live Handle.
    emit(0x3C605052, 0x60634F42, 0x3880007B)
    call("Get1Resource")
    emit(0x907E01B8, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    call("RemoveResource")
    call("ResError")
    check(0)
    emit(0x3C605052, 0x60634F42, 0x3880007B)
    call("Get1Resource")
    check(0)
    call("ResError")
    check(0)  # The existing Get1Resource implementation clears a missing-resource error.
    emit(0x807E01B8, 0x80630000, 0x88630000)
    check(42)
    emit(0x807E01B8)
    call("DisposeHandle")
    emit(0x38600000)
    call("RemoveResource")
    call("ResError")
    check(-196)
    # Compare a real serial with itself and with kCurrentProcess. Boolean
    # output is one byte; the adjacent sentinel must survive each call.
    emit(0x38600000, 0x907E0078, 0x38600002, 0x907E007C)
    for right in (0x70, 0x78):
        emit(0x3C60A55A, 0x907E01B0, 0x387E0070, 0x389E0000 | right, 0x38BE01B0)
        call("SameProcess")
        check(0)
        emit(0x887E01B0)
        check(1)
        emit(0x887E01B1)
        check(0x5A)
    emit(0x38601234, 0x907E007C)
    emit(0x387E0070, 0x389E0078, 0x38BE01B0)
    call("SameProcess")
    check(-50)
    for missing in range(3):
        emit(0x387E0070, 0x389E0070, 0x38BE01B0)
        emit((0x38600000, 0x38800000, 0x38A00000)[missing])
        call("SameProcess")
        check(-50)
    emit(0x3860003C, 0x907E0080, 0x387E00C0, 0x907E00B8)
    emit(0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check(0)
    # Resplendence/REALbasic supplies a larger record containing its FSSpec.
    # Accept the known prefix and preserve the caller's trailing storage.
    emit(0x38600082, 0x907E0080, 0x3C60DEAD, 0x6063BEEF, 0x907E00BC)
    emit(0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check(0)
    emit(0x807E0080)
    check(130)
    emit(0x807E00BC, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
    unequal()
    emit(0x3860003B, 0x907E0080, 0x387E0070, 0x389E0080)
    call("GetProcessInformation")
    check(-50)  # truncated record rejected without changing the tail
    emit(0x807E00BC, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
    unequal()
    emit(0x38600000, 0x389E0080)
    call("GetProcessInformation")
    check(-50)
    emit(0x387E0070, 0x38800000)
    call("GetProcessInformation")
    check(-50)
    # Full aliases accept an optional source FSSpec. Round-trip the running
    # application's FSSpec through both the absolute and relative forms.
    # Standard ppc-launch.<GUID> directories exceed the classic 31-character
    # name limit: absolute resolution must recover through the stored parent
    # catalog ID when the path tag contains a truncated directory name.
    for source in (0, 0xC0):
        emit(0x3C60DEAD, 0x6063BEEF, 0x907E02A4)
        emit(0x387E0000 | source if source else 0x38600000, 0x389E00C0, 0x38BE02A0)
        call("NewAlias")
        check(0)
        emit(0x807E02A0, 0x2C030000)
        failures.append(len(code))
        emit(0x41820000)
        emit(0x807E02A4, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
        unequal()  # A guest Handle output occupies exactly four bytes.
        emit(0x3C605A5A, 0x907E02A8)
        emit(0x387E0000 | source if source else 0x38600000, 0x809E02A0, 0x38BE0240, 0x38DE02A8)
        call("ResolveAlias")
        check(0)
        emit(0x887E02A9)
        check(0x5A)  # The changed flag is one byte.
        for expected, actual, opcode in ((0xC0, 0x240, 0xA0000000), (0xC2, 0x242, 0x80000000)):
            emit(opcode | 0x007E0000 | expected, opcode | 0x009E0000 | actual, 0x7C032000)
            unequal()
        # Compare the Pascal name, excluding unused trailing FSSpec storage.
        emit(0x88FE00C6, 0x38E70001, 0x39000000, 0x387E00C6, 0x389E0246)
        name_loop = len(code)
        emit(0x7D2340AE, 0x7D4440AE, 0x7C095000)
        unequal()
        emit(0x39080001, 0x7C083800)
        branch = len(code)
        emit(0x41800000 | (((name_loop - branch) * 4) & 65535))
        emit(0x807E02A0)
        call("DisposeHandle")
    emit(0x38600000, 0x38800000, 0x38BE02A0)
    call("NewAlias")
    check(-50)
    emit(0x807E02A0)
    check(0)
    emit(0x38600000, 0x389E00C0, 0x38A00000)
    call("NewAlias")
    check(-50)
    # Full-path alias creation must return a guest-endian live Handle.
    emit(0x38600009, 0x389E0180, 0x38BE01A0, 0x38DE01A0, 0x38FE01A8)
    call("NewAliasMinimalFromFullPath")
    check(0)
    emit(0x807E01A8, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    call("DisposeHandle")
    emit(0x38600000, 0x389E0180, 0x38BE01A0, 0x38DE01A0, 0x38FE01A8)
    call("NewAliasMinimalFromFullPath")
    check(-50)
    emit(0x807E01A8)
    check(0)  # failed creation clears the output handle
    for operation in ("PBDTGetCommentSync", "PBDTGetCommentAsync", "PBDTGetAPPLSync", "PBDTGetAPPLAsync"):
        emit(0x387E0200)
        call(operation)
        check(-51)  # invalid desktop reference; no database is modified
        emit(0xA87E0210)
        check(-51)  # ioResult has Macintosh byte order in both modes
    for operation in ("PBDTGetAPPLSync", "PBDTGetAPPLAsync"):
        emit(0x38600000)
        call(operation)
        check(-50)
    emit(0x38600000, 0xA89E00C0, 0x80BE00C2)
    call("HSetVol")
    check(0)
    emit(0x387E0150, 0x38800000, 0x38BE0110)
    call("FSOpen")
    check(0)
    amount(8)
    emit(0x3C60DEAD, 0x6063BEEF, 0x907E0128)
    reference()
    emit(0x389E0114, 0x38BE0120)
    call("FSRead")
    check(0)
    emit(0x807E0114)
    check(8)
    emit(0x807E0120, 0x3C804152, 0x60844449, 0x7C032000)
    unequal()
    emit(0x807E0124, 0x3C807465, 0x60847374, 0x7C032000)
    unequal()
    emit(0x807E0128, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
    unequal()
    for name, value in [("GetFPos", 8), ("GetEOF", 16)]:
        reference()
        emit(0x389E0118)
        call(name)
        check(0)
        emit(0x807E0118)
        check(value)
        reference()
        emit(0x38800000)
        call(name)
        check(-50)
    seek(2, -3)
    amount(8)
    reference()
    emit(0x389E0114, 0x38BE0120)
    call("FSRead")
    check(-39)
    emit(0x807E0114)
    check(3)
    emit(0x807E0120, 0x5463C23E, 0x3C800065, 0x60846E64, 0x7C032000)
    unequal()
    seek(1, 4)
    amount(4)
    reference()
    emit(0x389E0114, 0x38BE0170)
    call("FSWrite")
    check(0)
    emit(0x807E0114)
    check(4)
    reference()
    emit(0x389E0118)
    call("GetFPos")
    check(0)
    emit(0x807E0118)
    check(8)
    seek(1, 4)
    amount(4)
    reference()
    emit(0x389E0114, 0x38BE0120)
    call("FSRead")
    check(0)
    emit(0x807E0120, 0x3C805752, 0x60844954, 0x7C032000)
    unequal()
    for name in ["FSRead", "FSWrite"]:
        amount(-1)
        reference()
        emit(0x389E0114, 0x38BE0120)
        call(name)
        check(-50)
        emit(0x807E0114)
        check(0)
        reference()
        emit(0x38800000, 0x38BE0120)
        call(name)
        check(-50)
        amount(0)
        reference()
        emit(0x389E0114, 0x38A00000)
        call(name)
        check(0)
    reference()
    call("FSClose")
    check(0)
    emit(0x38600000, 0x38800000)
    call("FlushVol")
    check(0)
    emit(0x38601234, 0xB07E0110, 0x387E0160, 0x38800000, 0x38BE0110)
    call("FSOpen")
    check(-43)
    emit(0xA87E0110)
    check(0x1234)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(704)
    data[384:393] = b"Disk:File"
    data[336:347] = b"\x0aProbe.data"
    data[352:365] = b"\x0cmissing.file"
    data[368:372] = b"WRIT"
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", n, 2) for n in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1)],
    )
    write_fragment(destination, "PPC File Probe", app, 1, [(b"PROB", 123, b"\x2a")])
    (destination / "Probe.data").write_bytes(b"ARDItest12345end")


if __name__ == "__main__":
    make_file_probe(Path(sys.argv[1]))
