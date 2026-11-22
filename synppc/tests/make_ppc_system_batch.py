#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_system_batch.py
#
#   Summary:   Pack flat PowerPC checks into one reporting application with private code and data sections.
#
#   Classes:   None.
#
#   Functions: collect_checks, make_batch, write_batch, main
#
#   Origin:    Original PEF composition of this repository's original PowerPC regression programs.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+
import argparse
import struct
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
import make_ppc_probe as toolbox
import make_ppc_system_probe as system


def collect_checks(destination):
    checks = []

    def capture(_destination, name, image, kind):
        assert kind == 1
        checks.append((name, bytes(image)))

    with patch.object(system, "write_fragment", capture), patch.object(toolbox, "write_fragment", capture):
        system.make_probe(destination)
        toolbox.make_probe(destination)
        toolbox.make_probe(destination, cached=True)
        toolbox.make_probe(destination, cached=True, l2=True)
    return checks


def make_batch(destination, fail_case=None):
    checks = collect_checks(destination)
    write_batch(destination, "PPC 750 Batch Probe", "PPC 750 batch", checks, fail_case=fail_case)


def _relocate_imports(ops, indexes):
    result = []
    next_import = 0
    i = 0
    while i < len(ops):
        op = ops[i]
        i += 1
        if op & 0xFE00 == 0x4A00:
            count = (op & 511) + 1
        elif op & 0xFE00 == 0x6000:
            next_import, count = op & 511, 1
        elif op & 0xFC00 == 0xA400:
            next_import, count = (op & 1023) << 16 | ops[i], 1
            i += 1
        else:
            result.append(op)
            if op & 0xFC00 == 0xA000:  # SetPosition uses two words.
                result.append(ops[i])
                i += 1
            elif not (op < 0x4A00 or 0x8000 <= op < 0x9000):
                raise ValueError(f"Unsupported batch relocation {op:#06x}")
            continue
        assert next_import + count <= len(indexes)
        for index in indexes[next_import : next_import + count]:
            if index < 512:
                result.append(0x6000 | index)
            else:
                result.extend((0xA400 | index >> 16, index & 65535))
        next_import += count
    return result


def write_batch(
    destination,
    app,
    banner,
    checks,
    *,
    fail_case=None,
    extra_resources=(),
    debug_library="InterfaceLib",
    exit_at_end=False,
):
    if fail_case and fail_case not in [name for name, _ in checks]:
        raise ValueError("Unknown failure case: " + fail_case)
    sections = [b"", b""]
    modules = []
    vectors = []
    libraries = {debug_library: {("DebugStr", 2): None}}
    if exit_at_end:
        libraries[debug_library]["ExitToShell", 2] = None
    for name, image in checks:
        assert struct.unpack_from(">HH", image, 32) == (3, 2)
        payloads = []
        for index, kind in enumerate((0, 1, 4)):
            header = struct.unpack_from(">6I4B", image, 40 + index * 28)
            assert header[6] == kind and header[2] == header[3] == header[4]
            payloads.append(image[header[5] : header[5] + header[4]])
        code, data, loader = payloads
        fields = struct.unpack_from(">14I", loader)
        assert fields[8] == 1 and fields[4] == 0xFFFFFFFF and fields[13] == 0
        assert fields[0] == 1 and fields[2] in (1, 0xFFFFFFFF)
        entry = fields[3] if fields[2] != 0xFFFFFFFF else fields[1]
        address, toc = struct.unpack_from(">II", data, entry)
        if name == fail_case:
            code = bytearray(code)
            struct.pack_into(">II", code, address, 0x38600001, 0x4E800020)
            code = bytes(code)
        code_index, data_index = len(sections), len(sections) + 1
        vectors.append((code_index, data_index, address, toc))
        sections.extend((code, data))
        local_imports = []
        for library in range(fields[6]):
            name_at, old, current, count, first, flags = struct.unpack_from(">6I", loader, 56 + library * 24)
            assert (old, current, flags) == (0, 0, 0)
            library_name = loader[fields[10] + name_at :].split(b"\0", 1)[0].decode()
            assert first == len(local_imports)
            for symbol in range(first, first + count):
                value = struct.unpack_from(">I", loader, 56 + fields[6] * 24 + symbol * 4)[0]
                symbol_name = loader[fields[10] + (value & 0xFFFFFF) :].split(b"\0", 1)[0].decode()
                symbol = (symbol_name, value >> 24)
                libraries.setdefault(library_name, {}).setdefault(symbol, None)
                local_imports.append((library_name, symbol))
        relocation_at = 56 + fields[6] * 24 + fields[7] * 4
        target, _, count, offset = struct.unpack_from(">HHII", loader, relocation_at)
        assert target == 1
        ops = struct.unpack_from(f">{count}H", loader, fields[9] + offset)
        modules.append((code_index, data_index, ops, local_imports))

    symbols, library_records, import_indexes = [], [], {}
    strings = bytearray()
    for library, imports in libraries.items():
        library_records.append((len(strings), 0, 0, len(imports), len(symbols), 0))
        strings += library.encode() + b"\0"
        for symbol, kind in imports:
            import_indexes[library, (symbol, kind)] = len(symbols)
            symbols.append(kind << 24 | len(strings))
            strings += symbol.encode() + b"\0"
    relocations = []
    for code_index, data_index, ops, local_imports in modules:
        # Every module keeps its original code/data section bases and TOC.
        ops = [0x6200 | code_index, 0x6400 | data_index] + _relocate_imports(
            ops, [import_indexes[key] for key in local_imports]
        )
        relocations.append((data_index, struct.pack(f">{len(ops)}H", *ops)))

    debugstr_index = import_indexes[debug_library, ("DebugStr", 2)]
    code = []
    data = bytearray(16)  # Main and initializer transition vectors.
    for _, _, address, toc in vectors:
        data += struct.pack(">II", address, toc)
    report_import = len(data)
    data += bytes(4)
    exit_import = len(data)
    if exit_at_end:
        data += bytes(4)

    def emit(*words):
        code.extend(words)

    def string(text):
        encoded = text.encode("ascii")
        assert len(encoded) <= 255
        offset = len(data)
        data.extend(bytes([len(encoded)]) + encoded)
        assert offset < 32768
        return offset

    def report(offset):
        emit(0x80810034)  # Reload the batch TOC after calling another fragment.
        emit(0x38640000 | offset, 0x81840000 | report_import)
        emit(0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x90410034)
    failures = []
    for index, (name, _) in enumerate(checks):
        vector = 16 + index * 8
        report(string(name + ": running"))
        emit(0x80810034, 0x81840000 | vector, 0x80440000 | (vector + 4), 0x38600000, 0x7D8903A6, 0x4E800421)
        emit(0x2C030000)
        failures.append((len(code), name))
        emit(0x40820000)
        report(string(name + ": passed"))
    report(string(f"{banner}: {len(checks)} checks passed"))
    emit(0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    for branch, name in failures:
        code[branch] |= (len(code) - branch) * 4
        report(string(name + ": FAILED"))
        emit(0x80010044, 0x7C0803A6, 0x38210040, 0x38600001, 0x4E800020)
    struct.pack_into(">I", data, 0, len(code) * 4)
    if exit_at_end:
        emit(0x9421FFC0, 0x81820000 | exit_import, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)
        emit(0)  # The default ExitToShell vector must end the application.
    else:
        emit(0x38600000, 0x4E800020)  # Main only returns after the reporting initializer passed.
    sections[0] = b"".join(struct.pack(">I", word) for word in code)
    sections[1] = bytes(data)
    wrapper_ops = [0x4601]
    for code_index, data_index, _, _ in vectors:
        wrapper_ops += [0x6200 | code_index, 0x6400 | data_index, 0x4600]
    wrapper_ops += [0x6000 | debugstr_index]
    if exit_at_end:
        wrapper_ops.append(0x6000 | import_indexes[debug_library, ("ExitToShell", 2)])
    relocations.insert(0, (1, struct.pack(">" + "H" * len(wrapper_ops), *wrapper_ops)))

    op_at = 56 + len(library_records) * 24 + len(symbols) * 4 + len(relocations) * 12
    ops = b"".join(stream for _, stream in relocations)
    string_at = op_at + len(ops)
    hash_at = (string_at + len(strings) + 3) & ~3
    loader = struct.pack(
        ">14I",
        1,
        0,
        1,
        8,
        0xFFFFFFFF,
        0,
        len(library_records),
        len(symbols),
        len(relocations),
        op_at,
        string_at,
        hash_at,
        0,
        0,
    )
    loader += b"".join(struct.pack(">6I", *record) for record in library_records)
    loader += b"".join(struct.pack(">I", value) for value in symbols)
    offset = 0
    for target, stream in relocations:
        loader += struct.pack(">HHII", target, 0, len(stream) // 2, offset)
        offset += len(stream)
    loader += ops + strings
    loader += bytes(hash_at - len(loader)) + bytes(4)
    instances = len(sections)
    sections.append(loader)
    header = b"Joy!peffpwpc" + struct.pack(">5I", 1, 0, 0, 0, 0)
    header += struct.pack(">HHI", len(sections), instances, 0)
    offset = (40 + len(sections) * 28 + 15) & ~15
    contents = bytearray(offset)
    contents[:40] = header
    for index, payload in enumerate(sections):
        kind = 4 if index == instances else index & 1
        struct.pack_into(
            ">6I4B",
            contents,
            40 + index * 28,
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
        contents += payload
        padding = (-len(contents)) & 15
        contents += bytes(padding)
        offset = len(contents)
    toolbox.write_fragment(destination, app, contents, 1, extra_resources=extra_resources)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--fail-case")
    args = parser.parse_args()
    make_batch(args.destination, args.fail_case)


if __name__ == "__main__":
    main()
