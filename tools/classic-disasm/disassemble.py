# +===================================================================
#   File:      disassemble.py
#
#   Summary:   Extract local classic Mac reference files and annotate their PEF listings.
#
#   Classes:   None declared.
#
#   Functions: u32, u16, cstring, safe_name, write_json, unpack_pattern, relocations, parse_pef, fragment_names,
#              pef_listing, analyze_file, main
#
#   Origin:    Existing repository source in tools/classic-disasm; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Extract local classic Mac reference files and annotate their PEF listings.

PEF layout and relocation interpretation follow synppc/src/pef-loader.cpp.
This is static analysis: embedded data may decode as instructions, and indirect
calls are not a complete call graph. Apple reference bytes stay in ignored output.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from mac_resources import AppleDouble, ResourceFork, extent, unpack

CODE_TYPES = {
    b"CODE",
    b"code",
    b"scod",
    b"INIT",
    b"DRVR",
    b"PACK",
    b"ptch",
    b"PTCH",
    b"gpch",
    b"proc",
    b"dcmp",
    b"WDEF",
    b"CDEF",
    b"MDEF",
    b"LDEF",
    b"MBDF",
    b"FKEY",
    b"CGLU",
}
FOCUS = re.compile(r"Thread|ForkAsync|HCopyFile|PBRead|PBWrite|IOCompletion")


def u32(data, offset):
    return unpack(">I", data, offset)[0]


def u16(data, offset):
    return unpack(">H", data, offset)[0]


def cstring(data, offset):
    extent(data, offset, 1)
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("Unterminated PEF name")
    return data[offset:end].decode("mac_roman")


def safe_name(name):
    return re.sub(r"[^a-zA-Z0-9_.-]", "_", name) or "unnamed"


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n")


def unpack_pattern(data, size):
    position = 0
    output = bytearray()

    def take(length):
        nonlocal position
        chunk = extent(data, position, length)
        position += length
        return chunk

    def count():
        value = 0
        for _ in range(5):
            byte = take(1)[0]
            value = (value << 7) | (byte & 127)
            if value > 0xFFFFFFFF:
                raise ValueError("PEF pattern count overflow")
            if not byte & 128:
                return value
        raise ValueError("Overlong PEF pattern count")

    def emit(chunk):
        if len(chunk) > size - len(output):
            raise ValueError("PEF pattern output overflow")
        output.extend(chunk)

    while position < len(data):
        op = take(1)[0]
        kind, amount = op >> 5, op & 31
        amount = amount or count()
        if amount > size:
            raise ValueError("Oversized PEF pattern")
        if kind == 0:
            emit(bytes(amount))
        elif kind == 1:
            emit(take(amount))
        elif kind == 2:
            times = count() + 1
            if amount * times > size - len(output):
                raise ValueError("PEF repeat output overflow")
            emit(take(amount) * times)
        elif kind in (3, 4):
            custom, times = count(), count()
            if amount * (times + 1) + custom * times > size - len(output):
                raise ValueError("PEF interleave output overflow")
            common = take(amount) if kind == 3 else bytes(amount)
            if amount or custom:
                for index in range(times + 1):
                    emit(common)
                    if index < times:
                        emit(take(custom))
        else:
            raise ValueError(f"Unsupported PEF pattern {kind}")
    if len(output) != size:
        raise ValueError("Short PEF pattern output")
    return bytes(output)


def relocations(ops, size, import_count):
    """Return relocation targets, retaining section/import identities."""
    result = {}
    position = ip = import_index = 0
    section_c, section_d = 0, 1
    repeat_at, repeat_left = None, 0
    budget = size * 8 + len(ops) * 16 + 1024

    def skip(amount):
        nonlocal position
        if not 0 <= amount <= size - position:
            raise ValueError("PEF relocation position overflow")
        position += amount

    def add(kind, index):
        if kind == "import" and not 0 <= index < import_count:
            raise ValueError("PEF import index overflow")
        if position + 4 > size:
            raise ValueError("PEF relocation write overflow")
        result[position] = (kind, index)
        skip(4)

    while ip < len(ops):
        budget -= 1
        if budget < 0:
            raise ValueError("Excessive PEF relocation work")
        start = ip
        op = u16(ops, ip)
        ip += 2
        if op >> 14 == 0:
            skip(((op >> 6) & 255) * 4)
            for _ in range(op & 63):
                add("section", section_d)
        elif op >> 13 == 2:
            sub, count = (op >> 9) & 15, (op & 511) + 1
            for _ in range(count):
                if sub in (0, 1):
                    add("section", section_c if sub == 0 else section_d)
                elif sub in (2, 3):
                    add("section", section_c)
                    add("section", section_d)
                    if sub == 2:
                        skip(4)
                elif sub == 4:
                    add("section", section_d)
                    skip(4)
                elif sub == 5:
                    add("import", import_index)
                    import_index += 1
                else:
                    raise ValueError(f"Unknown PEF relocation {op:04x}")
        elif op >> 13 == 3:
            sub, index = (op >> 9) & 15, op & 511
            if sub == 0:
                import_index = index
                add("import", import_index)
                import_index += 1
            elif sub == 1:
                section_c = index
            elif sub == 2:
                section_d = index
            elif sub == 3:
                add("section", index)
            else:
                raise ValueError(f"Unknown PEF relocation {op:04x}")
        elif op >> 12 == 8:
            skip((op & 4095) + 1)
        elif op >> 12 == 9 or op >> 10 == 0x2C:
            chunks, times = ((op >> 8) & 15) + 1, (op & 255) + 1
            if op >> 10 == 0x2C:
                chunks = ((op >> 6) & 15) + 1
                times = ((op & 63) << 16) | u16(ops, ip)
                ip += 2
            if start < chunks * 2:
                raise ValueError("PEF repeat precedes stream")
            if repeat_at is None:
                repeat_at, repeat_left = start, times
            if repeat_at != start:
                raise ValueError("Nested PEF relocation repeat")
            if repeat_left:
                repeat_left -= 1
                ip = start - chunks * 2
            else:
                repeat_at = None
        elif op >> 10 in (0x28, 0x29):
            value = ((op & 1023) << 16) | u16(ops, ip)
            ip += 2
            if op >> 10 == 0x28:
                if value > size:
                    raise ValueError("PEF relocation position overflow")
                position = value
            else:
                import_index = value
                add("import", import_index)
                import_index += 1
        elif op >> 10 == 0x2D:
            sub = (op >> 6) & 15
            value = ((op & 63) << 16) | u16(ops, ip)
            ip += 2
            if sub == 0:
                add("section", value)
            elif sub == 1:
                section_c = value
            elif sub == 2:
                section_d = value
            else:
                raise ValueError(f"Unknown PEF relocation {op:04x}")
        else:
            raise ValueError(f"Unknown PEF relocation {op:04x}")
    if repeat_at is not None:
        raise ValueError("Unfinished PEF relocation repeat")
    return result


def parse_pef(data):
    if extent(data, 0, 12) != b"Joy!peffpwpc" or u32(data, 12) != 1:
        raise ValueError("Not a version-1 PowerPC PEF")
    sections = []
    for index in range(u16(data, 32)):
        at = 40 + index * 28
        name, address, total, unpacked, packed, offset, kind, share, align, _ = unpack(">iIIIIIBBBB", data, at)
        if total > 64 * 1024 * 1024 or unpacked > total:
            raise ValueError("Invalid PEF section sizes")
        payload = extent(data, offset, packed)
        payload = unpack_pattern(payload, unpacked) if kind == 2 else payload
        if kind in (0, 1, 2, 3, 6) and len(payload) != unpacked:
            raise ValueError("PEF section size mismatch")
        sections.append(
            dict(
                index=index,
                kind=kind,
                offset=offset,
                size=total,
                unpacked_size=unpacked,
                packed_size=packed,
                address=address,
                payload=payload,
            )
        )
    loader = next(s["payload"] for s in sections if s["kind"] == 4)
    libraries, import_count, reloc_count, reloc_start, strings = unpack(">IIIII", loader, 24)
    symbols = 56 + libraries * 24
    imports = [None] * import_count
    for index in range(libraries):
        at = 56 + index * 24
        library = cstring(loader, strings + u32(loader, at))
        count, first = unpack(">II", loader, at + 12)
        if first + count > import_count:
            raise ValueError("PEF import range overflow")
        for symbol in range(first, first + count):
            value = u32(loader, symbols + symbol * 4)
            imports[symbol] = dict(
                index=symbol,
                library=library,
                name=cstring(loader, strings + (value & 0xFFFFFF)),
                symbol_class=(value >> 24) & 15,
            )
    if any(item is None for item in imports):
        raise ValueError("Unassigned PEF imports")
    relocs = {}
    for index in range(reloc_count):
        at = symbols + import_count * 4 + index * 12
        section, _, count, offset = unpack(">HHII", loader, at)
        relocs[section] = relocations(
            extent(loader, reloc_start + offset, count * 2),
            sections[section]["size"],
            import_count,
        )
    exports = []
    hash_start, power, export_count = unpack(">III", loader, 44)
    if power > 20:
        raise ValueError("Oversized PEF export hash")
    keys = hash_start + (1 << power) * 4
    table = keys + export_count * 4
    for index in range(export_count):
        value, offset, section = unpack(">IIh", loader, table + index * 10)
        length = u32(loader, keys + index * 4) >> 16
        name = extent(loader, strings + (value & 0xFFFFFF), length).decode("mac_roman")
        exports.append(dict(name=name, section=section, offset=offset, symbol_class=value >> 24))
    entries = [
        dict(name=name, section=unpack(">i", loader, at)[0], offset=u32(loader, at + 4))
        for name, at in [("main", 0), ("init", 8), ("term", 16)]
    ]
    return sections, imports, exports, relocs, entries


def fragment_names(fork):
    names = {}
    for (kind, _), (data, _, _) in fork.records.items():
        if kind != b"cfrg":
            continue
        at = 32
        for _ in range(u32(data, 28)):
            length = u16(data, at + 40)
            if length < 43:
                raise ValueError("Short cfrg member")
            member = extent(data, at, length)
            if member[:4] == b"pwpc" and member[23] == 1:
                offset = u32(member, 24)
                name = extent(member, 43, member[42]).decode("mac_roman")
                names.setdefault(offset, [])
                if name not in names[offset]:
                    names[offset].append(name)
            at += length
    return names


def pef_listing(data, directory):
    from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN

    directory.mkdir(parents=True, exist_ok=True)
    sections, imports, exports, relocs, entries = parse_pef(data)
    labels, tocs = {}, set()
    for export in exports + entries:
        section, offset = export["section"], export["offset"]
        if not 0 <= section < len(sections):
            continue
        if export.get("symbol_class", 2) == 2 and sections[section]["kind"] in (1, 2):
            vector = sections[section]["payload"]
            if offset + 8 <= len(vector):
                target = relocs.get(section, {}).get(offset)
                if target and target[0] == "section":
                    code_offset, toc = unpack(">II", vector, offset)
                    labels.setdefault((target[1], code_offset), []).append(export["name"])
                    tocs.add((section, toc))
                    export["code_section"], export["code_offset"] = (
                        target[1],
                        code_offset,
                    )
        elif sections[section]["kind"] in (0, 6):
            labels.setdefault((section, offset), []).append(export["name"])
    stubs, calls, focused = {}, [], []
    engine = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
    engine.skipdata = True
    for section in sections:
        code, index = section["payload"], section["index"]
        (directory / f"section-{index}.bin").write_bytes(code)
        if section["kind"] not in (0, 6):
            continue
        for offset in range(0, len(code) - 23, 4):
            if u32(code, offset) >> 16 != 0x8182:
                continue
            if code[offset + 4 : offset + 24] != bytes.fromhex("90410014800c0000804c00047c0903a64e800420"):
                continue
            displacement = unpack(">h", code, offset + 2)[0]
            for toc_section, toc in tocs:
                target = relocs.get(toc_section, {}).get(toc + displacement)
                if target and target[0] == "import":
                    stubs[(index, offset)] = imports[target[1]]
        for offset in range(0, len(code) - 3, 4):
            word = u32(code, offset)
            if word >> 26 == 18 and word & 1 and not word & 2:
                displacement = word & 0x3FFFFFC
                if displacement & 0x2000000:
                    displacement -= 0x4000000
                target = offset + displacement
                symbol = stubs.get((index, target))
                if symbol:
                    calls.append(dict(section=index, offset=offset, target=target, **symbol))
        section_calls = {c["offset"]: c for c in calls if c["section"] == index}
        with (directory / f"section-{index}.ppc.s").open("w", encoding="utf-8", newline="\n") as out:
            out.write("; Linear PowerPC disassembly; addresses are section-relative.\n")
            for address, size, mnemonic, operands in engine.disasm_lite(code, 0):
                for label in labels.get((index, address), []):
                    out.write(f"\n; export/entry {label}\n")
                symbol = stubs.get((index, address)) or section_calls.get(address)
                note = f" ; {symbol['library']}:{symbol['name']}" if symbol else ""
                out.write(f"{address:08x}  {code[address:address+size].hex():8s}  {mnemonic:9s} {operands}{note}\n")
        for call in section_calls.values():
            if not FOCUS.search(call["name"]):
                continue
            offset = call["offset"]
            focused.append(f"\nCALL section {index}+0x{offset:08x} {call['library']}:{call['name']}\n")
            begin, end = max(0, offset - 80), min(len(code), offset + 84)
            for address, size, mnemonic, operands in engine.disasm_lite(code[begin:end], begin):
                focused.append(f"{address:08x}  {mnemonic:9s} {operands}\n")
    metadata = dict(
        sections=[{k: v for k, v in s.items() if k != "payload"} for s in sections],
        imports=imports,
        exports=exports,
        entries=entries,
        import_stubs=[dict(section=s, offset=o, **v) for (s, o), v in stubs.items()],
        imported_calls=calls,
    )
    write_json(directory / "pef.json", metadata)
    (directory / "copy-threads.txt").write_text("".join(focused), encoding="utf-8")
    return dict(
        sections=len(sections),
        imports=len(imports),
        exports=len(exports),
        call_sites=len(calls),
        focused_calls=sum(bool(FOCUS.search(c["name"])) for c in calls),
    )


def analyze_file(input_directory, output, name):
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040

    data = (input_directory / name).read_bytes()
    double = (input_directory / ("._" + name)).read_bytes()
    resource_data = AppleDouble(double).get(2)
    fork = ResourceFork(resource_data)
    directory = output / name
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "resource-fork.bin").write_bytes(resource_data)
    names = fragment_names(fork)
    fragments, resources, warnings = [], [], []
    containers = [("data", data, names)]
    engine = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)
    engine.skipdata = True
    decoder = None
    resource_directory = directory / "resources"
    resource_directory.mkdir(exist_ok=True)
    for (kind, rid), (payload, attributes, reference) in fork.records.items():
        name_offset = unpack(">h", fork.map, reference + 2)[0]
        resource_name = ""
        if name_offset >= 0:
            at = u16(fork.map, 26) + name_offset
            resource_name = extent(fork.map, at + 1, fork.map[at]).decode("mac_roman")
        stem = f"{kind.hex()}_{rid}"
        (resource_directory / (stem + ".bin")).write_bytes(payload)
        row = dict(
            type=kind.decode("mac_roman"),
            id=rid,
            name=resource_name,
            attributes=attributes,
            size=len(payload),
            file=f"resources/{stem}.bin",
        )
        resources.append(row)
        if attributes & 1:
            try:
                if decoder is None:
                    spec = importlib.util.spec_from_file_location(
                        "classic_resource_decoder",
                        ROOT / "tools/classic-disasm/decode_resource.py",
                    )
                    decoder = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(decoder)
                payload = decoder.decode(payload)
                row["decoded_size"] = len(payload)
            except Exception as error:
                warnings.append(f"{kind!r} {rid}: resource decode: {error}")
                row["decode_error"] = str(error)
                continue
        if b"Joy!peffpwpc" in payload:
            containers.append((f"resource-{stem}", payload, {}))
        elif (
            kind in CODE_TYPES
            or re.fullmatch(rb"c[dD][0-9A-Fa-f]{2}", kind)
            or kind in {b"otgl", b"dnrp", b"cdev", b"task", b"AINI"}
        ):
            code_directory = directory / "m68k"
            code_directory.mkdir(exist_ok=True)
            (code_directory / (stem + ".bin")).write_bytes(payload)
            with (code_directory / (stem + ".s")).open("w", encoding="utf-8", newline="\n") as out:
                out.write(f"; Linear 68K listing: {kind!r} {rid} {resource_name!r}. May contain headers/data.\n")
                for address, size, mnemonic, operands in engine.disasm_lite(payload, 0):
                    out.write(f"{address:08x}  {payload[address:address+size].hex():24s} {mnemonic:10s} {operands}\n")
    for container_name, container, fragment_map in containers:
        start = 0
        while (start := container.find(b"Joy!peffpwpc", start)) >= 0:
            labels = fragment_map.get(start, [])
            label = safe_name(labels[0]) if labels else f"offset-{start:08x}"
            path = directory / "pef" / f"{container_name}-{label}"
            try:
                facts = pef_listing(container[start:], path)
                fragments.append(
                    dict(
                        container=container_name,
                        offset=start,
                        names=labels,
                        directory=path.relative_to(output).as_posix(),
                        **facts,
                    )
                )
            except Exception as error:
                warnings.append(f"{container_name}+0x{start:x}: PEF: {error}")
            start += 12
    manifest = dict(
        name=name,
        data_sha256=hashlib.sha256(data).hexdigest(),
        appledouble_sha256=hashlib.sha256(double).hexdigest(),
        resource_sha256=hashlib.sha256(resource_data).hexdigest(),
        fragments=fragments,
        resources=resources,
        warnings=warnings,
    )
    write_json(directory / "manifest.json", manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        help="Folder containing System, Finder and their ._ forks",
    )
    parser.add_argument("--output", type=Path, default=ROOT / "disasm/system9")
    parser.add_argument(
        "--all-files", action="store_true", help="Analyze every file with an AppleDouble companion in --source"
    )
    args = parser.parse_args()
    if args.all_files and not args.source:
        parser.error("--all-files requires --source")
    output = args.output.resolve()
    sys.path.insert(0, str(output / "python-packages"))
    inputs = output / "input"
    inputs.mkdir(parents=True, exist_ok=True)
    names = ("Finder", "System")
    if args.all_files:
        names = tuple(
            sorted(
                p.name
                for p in args.source.iterdir()
                if p.is_file() and not p.name.startswith("._") and p.with_name("._" + p.name).is_file()
            )
        )
        if not names:
            parser.error("No files with AppleDouble companions in --source")
    if args.source:
        for name in [item for name in names for item in (name, "._" + name)]:
            source, target = args.source / name, inputs / name
            if source.resolve() != target.resolve():
                shutil.copy2(source, target)
    manifests = [analyze_file(inputs, output, name) for name in names]
    write_json(output / "index.json", manifests)
    for item in manifests:
        print(
            f"{item['name']}: {len(item['fragments'])} PEF fragments, "
            f"{len(item['resources'])} resources, {len(item['warnings'])} warnings"
        )
    return 1 if any(item["warnings"] for item in manifests) else 0


if __name__ == "__main__":
    raise SystemExit(main())
