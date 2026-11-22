#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_shared_probe.py
#
#   Summary:   Original multi-fragment PPC fixture; no Apple binaries required.
#
#   Classes:   None declared.
#
#   Functions: pef, make_shared_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original multi-fragment PPC fixture; no Apple binaries required."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment


def pef(words, data, imports=(), exports=(), main=-1, init=-1, term=-1, reloc=(), *, code_kind=0):
    code = b"".join(struct.pack(">I", word) for word in words)
    strings = bytearray()
    libraries = []
    previous_library = None
    symbols = []
    for library, symbol, kind in imports:
        if library != previous_library:
            libraries.append([len(strings), len(symbols), 0])
            previous_library = library
            strings += library.encode() + b"\0"
        symbols.append((kind << 24) | len(strings))
        strings += symbol.encode() + b"\0"
        libraries[-1][2] += 1
    symbol_exports = []
    keys = []
    for name, kind, value, section in exports:
        symbol_exports.append(struct.pack(">IIh", (kind << 24) | len(strings), value, section))
        strings += name.encode() + b"\0"
        keys.append(len(name) << 16)
    op = b"".join(struct.pack(">H", v) for v in reloc)
    instruction_offset = 56 + 24 * len(libraries) + 4 * len(symbols) + (12 if op else 0)
    string_offset = instruction_offset + len(op)
    hash_offset = (string_offset + len(strings) + 3) & ~3
    loader = struct.pack(
        ">14I",
        1 if main >= 0 else 0xFFFFFFFF,
        max(main, 0),
        1 if init >= 0 else 0xFFFFFFFF,
        max(init, 0),
        1 if term >= 0 else 0xFFFFFFFF,
        max(term, 0),
        len(libraries),
        len(symbols),
        bool(op),
        instruction_offset,
        string_offset,
        hash_offset,
        0,
        len(exports),
    )
    for offset, first, count in libraries:
        loader += struct.pack(">6I", offset, 0, 0, count, first, 0)
    loader += b"".join(struct.pack(">I", s) for s in symbols)
    if op:
        loader += struct.pack(">HHII", 1, 0, len(reloc), 0)
    loader += op + strings
    loader += bytes(hash_offset - len(loader))
    loader += struct.pack(">I", len(exports) << 18)
    loader += b"".join(struct.pack(">I", key) for key in keys) + b"".join(symbol_exports)
    code_offset = (128 + len(loader) + 15) & ~15
    data_offset = (code_offset + len(code) + 15) & ~15
    header = b"Joy!peffpwpc" + struct.pack(">5I", 1, 0, 0, 0, 0) + struct.pack(">HHI", 3, 2, 0)
    sections = b""
    for payload, offset, kind in [
        (code, code_offset, code_kind),
        (data, data_offset, 1),
        (loader, 128, 4),
    ]:
        sections += struct.pack(
            ">6I4B",
            0xFFFFFFFF,
            0,
            len(payload),
            len(payload),
            len(payload),
            offset,
            kind,
            0,
            4,
            0,
        )
    result = bytearray(data_offset + len(data))
    result[:124] = header + sections
    result[128 : 128 + len(loader)] = loader
    result[code_offset : code_offset + len(code)] = code
    result[data_offset:] = data
    return result


def make_shared_probe(destination):
    # Init validates CFragInitBlock's file locator and connection, then sets data.
    code = [
        0x80620018,
        0x4E800020,
        0x8083000C,
        0x2C040001,
        0x40820028,
        0x80830008,
        0x2C040000,
        0x4182001C,
        0x3880002A,
        0x90820018,
        0x38600000,
        0x4E800020,
        0x80620018,
        0x2C03002A,
        0x40820008,
        0x4E800020,
        0,
    ]
    data = struct.pack(">6I", 0, 0, 8, 0, 48, 0) + bytes(8)
    code += [0x80620020, 0x80630000, 0x4E800020]
    data += bytes(8) + struct.pack(">II", 68, 0)
    implementation = pef(
        code,
        data,
        imports=[("ProbePrereq", "PrereqValue", 1), ("ProbeCycle", "Dummy", 2)],
        exports=[
            ("GetValue", 2, 0, 1),
            ("ProbeValue", 1, 24, 1),
            ("GetPrereq", 2, 40, 1),
        ],
        init=8,
        term=16,
        reloc=[0x4602, 0x0080, 0x4A01, 0x4600],
    )
    # Put Impl first in the registry to reproduce the former DFS cycle bug.
    write_fragment(destination, "ProbeImpl", implementation, 0, file_name="Z ProbeImpl")
    prerequisite = pef(
        [0x3860002A, 0x90620008, 0x38600000, 0x4E800020],
        bytes(12),
        exports=[("PrereqValue", 1, 8, 1)],
        init=0,
        reloc=[0x4600],
    )
    write_fragment(destination, "ProbePrereq", prerequisite, 0)
    cycle_code = [
        0x38600000,
        0x4E800020,
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x819E0010,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0x2C03002A,
        0x4082001C,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x38600000,
        0x4E800020,
        0,
    ]
    cycle = pef(
        cycle_code,
        struct.pack(">6I", 0, 0, 8, 0, 0, 0),
        imports=[("ProbeImpl", "GetPrereq", 2)],
        exports=[("Dummy", 2, 0, 1)],
        init=8,
        reloc=[0x4601, 0x4A00],
    )
    write_fragment(destination, "ProbeCycle", cycle, 0)
    forwarding = pef(
        [0x4E800020],
        bytes(8),
        imports=[("ProbeImpl", "GetValue", 2), ("ProbeImpl", "ProbeValue", 1)],
        exports=[("GetValue", 2, 0, -3), ("ProbeValue", 1, 1, -3)],
    )
    write_fragment(destination, "ProbeForward", forwarding, 0)
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def check(value):
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)

    def check_word(value):
        emit(0x3C800000 | (value >> 16), 0x60840000 | (value & 0xFFFF), 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(0)
    check(42)
    emit(0x809E000C, 0x80640000)
    check(42)
    emit(0x809E0010, 0x80840000, 0x88640041, 0x70630001)
    check(1)
    emit(0x809E0014, 0x80640000)
    check_word(0x400921FB)
    emit(0x809E0018, 0x80640000)
    check(0)
    emit(0x809E001C, 0x80640000)
    check_word(0x47EFFFFF)
    emit(0x809E0020, 0x80640000)
    check_word(0x7FEFFFFF)
    emit(0x809E0024, 0xA0640012)
    check(5)
    emit(0xA064002C)
    check(1)
    emit(0x387E0060)
    call(8)
    check(5)
    emit(0x387E0068, 0x3880002A, 0x38A00004)
    call(9)
    emit(0x807E0068)
    check_word(0x2A2A2A2A)
    # MathLib uses FPR1 for floating arguments and return values.
    emit(0xC83E0070)
    call(13)
    emit(0xD83E0078, 0x807E0078)
    check_word(0x40080000)  # sqrt(9) == 3
    emit(0x38600004)
    call(14)
    check(1)  # one supported script
    emit(0x38600008, 0x38800001)
    call(15)
    check(0)
    emit(0x38600008)
    call(14)
    check(1)
    call(17)
    emit(0x88830006, 0x7C832378)
    check(1)  # shared smFontForce byte
    emit(0x38600000, 0x38800004)
    call(16)
    check(1)
    # Open a synthetic native library connection and look up a PPC vector.
    emit(
        0x387E0080,
        0x3C807077,
        0x60847063,
        0x38A00002,
        0x38DE00C0,
        0x38FE00C4,
        0x391E00C8,
    )
    call(10)
    check(0)
    emit(0x807E00C0, 0x389E0090, 0x38BE00CC, 0x38DE00D0)
    call(11)
    check(0)
    emit(0x887E00D0)
    check(2)  # transition vector class
    emit(
        0xC83E0070,
        0x819E00CC,
        0x800C0000,
        0x804C0004,
        0x7C0903A6,
        0x4E800421,
        0xD83E0078,
        0x807E0078,
    )
    check_word(0x40080000)
    # Stack-dispatched OSDispatch reaches the existing Process Manager.
    emit(0x8060103C, 0x388003AE, 0x38A00037, 0x38DE00D8)
    call(18)
    check(0)
    # Missing symbols return fragSymbolNotFound and clear the output.
    emit(0x807E00C0, 0x389E00A0, 0x38BE00CC, 0x38DE00D0)
    call(11)
    check(0xF50E)
    emit(0x807E00CC)
    check(0)
    emit(
        0x80601768,
        0x38800000,
        0x6084FFAE,
        0x38A00005,
        0x80DE00C0,
        0x38FE00A0,
        0x391E00CC,
        0x393E00D0,
    )
    call(18)
    check(0xF50E)
    # A find-only ID is borrowed and cannot close its launch-pinned library.
    emit(0x807E00C0, 0x907E00D4, 0x387E00C0)
    call(12)
    check(0xF50F)
    emit(0x807E00C0, 0x809E00D4, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    # An explicit reference load owns a separate handle that can be released.
    emit(0x387E0080, 0x3C807077, 0x60847063, 0x38A00001, 0x38DE00C0, 0x38FE00C4, 0x391E00C8)
    call(10)
    check(0)
    emit(0x387E00C0)
    call(12)
    check(0)
    emit(0x807E00C0)
    check(0)
    # CarbonLib reads classic low-memory trap vectors then calls Mixed Mode.
    emit(0x80601450, 0x38800020)
    call(18)  # CurResFile, signed 16-bit result
    # Allocate through register-based CallOSTrapUniversalProc and release.
    emit(0x80600478, 0x3C800003, 0x60843932, 0x38A00000 | 0xA51E, 0x38C00010)
    call(19)
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    call(20)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(256)
    data[96:102] = b"Hello\0"
    data[112:120] = struct.pack(">d", 9.0)
    data[128:136] = b"\x07MathLib"
    data[144:149] = b"\x04sqrt"
    data[160:168] = b"\x07Missing"
    app = pef(
        code,
        data,
        imports=[
            ("ProbeForward", "GetValue", 2),
            ("ProbeForward", "ProbeValue", 1),
            ("StdCLib", "__p_CType", 1),
            ("MathLib", "pi", 1),
            ("MathLib", "_FE_DFL_ENV", 1),
            ("StdCLib", "_FLT_MAX", 1),
            ("StdCLib", "_DBL_MAX", 1),
            ("StdCLib", "_iob", 1),
            ("StdCLib", "strlen", 2),
            ("StdCLib", "memset", 2),
            ("InterfaceLib", "GetSharedLibrary", 2),
            ("InterfaceLib", "FindSymbol", 2),
            ("InterfaceLib", "CloseConnection", 2),
            ("MathLib", "sqrt", 2),
            ("InterfaceLib", "GetScriptManagerVariable", 2),
            ("InterfaceLib", "SetScriptManagerVariable", 2),
            ("InterfaceLib", "GetScriptVariable", 2),
            ("InterfaceLib", "LMGetIntlSpec", 2),
            ("InterfaceLib", "CallUniversalProc", 2),
            ("InterfaceLib", "CallOSTrapUniversalProc", 2),
            ("InterfaceLib", "DisposePtr", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A14],
    )
    write_fragment(destination, "PPC Shared Probe", app, 1)


if __name__ == "__main__":
    make_shared_probe(Path(sys.argv[1]))
