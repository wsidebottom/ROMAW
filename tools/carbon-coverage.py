# +===================================================================
#   File:      carbon-coverage.py
#
#   Summary:   Audit native Carbon bindings against local PEF metadata, without executing it.
#
#   Classes:   None declared.
#
#   Functions: native_entries, application_imports, trace_symbols, reference_exports, audit, main
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Audit native Carbon bindings against local PEF metadata, without executing it.

The report measures bindings, not behavioral compatibility. Apple reference
metadata, application bytes and generated reports remain local ignored assets.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def native_entries(source):
    entries = {}
    companion_scopes = set()
    pattern = (
        r'\{\s*"([^"]+)"\s*,[^{}]*?,\s*(0x[0-9a-fA-F]+)'
        r'(?:\s*,\s*(?:"([^"]+)"|NULL))?(?:\s*,\s*([A-Z_]+(?:\s*\|\s*[A-Z_]+)*))?\s*\}'
    )
    for name, offset, library, flags in re.findall(pattern, source, re.S):
        flags = [flag.strip() for flag in flags.split("|")] if flags else []
        if len(set(flags)) != len(flags) or set(flags) - {
            "ROMLIB_CARBON_PPC_ONLY",
            "ROMLIB_CARBON_COMPANION_ONLY",
            "ROMLIB_CARBON_EXTENSION",
            "ROMLIB_CARBON_PRELOAD_REFERENCE",
        }:
            raise ValueError(f"Unrecognized native binding flags: {name}")
        if "ROMLIB_CARBON_PRELOAD_REFERENCE" in flags and not library:
            raise ValueError(f"Reference preload needs a source library: {name}")
        companion_only = "ROMLIB_CARBON_COMPANION_ONLY" in flags
        scope = (library, name)
        key = scope if companion_only else name
        if key in entries or (library and scope in companion_scopes):
            raise ValueError(f"Duplicate native binding: {name}")
        if library:
            companion_scopes.add(scope)
        entry = {"offset": int(offset, 16), "library": library or None}
        if companion_only:
            if not library:
                raise ValueError(f"Companion binding needs a source library: {name}")
            entry["companion_only"] = True
        if "ROMLIB_CARBON_EXTENSION" in flags:
            if library or entry["offset"] != 0xFFFFFFFF or companion_only:
                raise ValueError(f"Extension binding cannot declare a reference location: {name}")
            entry["extension"] = True
        entries[key] = entry
    if len(entries) != len(re.findall(r'\{\s*"([^"]+)"\s*,', source)):
        raise ValueError("Unparsed native binding; update the audit for its registry format")
    data_pattern = (
        r'CARBON_DATA\(\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]+)"\s*,'
        r"\s*0x[0-9a-fA-F]{1,8}\s*,\s*0x[0-9a-fA-F]{1,8}\s*\)"
    )
    callback_pattern = (
        r'CARBON_CALLBACK_DATA\(\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]+)"\s*,'
        r"\s*ROMLIB_CARBON_DATA_(?:TYPE_CALLBACKS|TYPE_HASH_CALLBACKS|STRING_HASH_CALLBACKS)\s*,\s*(0x[0-9a-fA-F]+)\s*\)"
    )
    allocator_pattern = (
        r'CARBON_SYSTEM_ALLOCATOR_DATA\(\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"([^"]+)"\s*,'
        r"\s*(0x[0-9a-fA-F]+)\s*\)"
    )
    layout_data = re.findall(callback_pattern, source) + re.findall(allocator_pattern, source)
    data_rows = re.findall(data_pattern, source) + [row[:3] for row in layout_data]
    if len(data_rows) != len(re.findall(r"^\s+CARBON_(?:(?:CALLBACK|SYSTEM_ALLOCATOR)_)?DATA\(", source, re.M)):
        raise ValueError("Unparsed native data binding")
    for name, offset, library in data_rows:
        if name in entries:
            raise ValueError(f"Duplicate native binding: {name}")
        entries[name] = {"offset": int(offset, 16), "library": library, "data": True}
    for name, _, _, offset in layout_data:
        entries[name]["data_offset"] = int(offset, 16)
    # clang-format may merge consecutive initializer macros onto one line or wrap their arguments.
    invocations = re.sub(r"^[ \t]*#[^\n]*", "", source, flags=re.M)
    key_rows = re.findall(
        r'\bCARBON_CF_KEY(?:_ALIAS)?\(\s*"([^"]+)"\s*,\s*(0x[0-9a-f]+)\s*,'
        r'\s*(0x[0-9a-f]+)\s*,\s*(?:"[^"]+"|0x[0-9a-f]+)\s*\)',
        invocations,
    )
    if len(key_rows) != len(re.findall(r"\bCARBON_CF_KEY(?:_ALIAS)?\(", invocations)):
        raise ValueError("Unparsed CoreFoundation key binding")
    singleton_rows = re.findall(
        r'\bCARBON_CF_SINGLETON\(\s*"([^"]+)"\s*,\s*(0x[0-9a-f]+)\s*,\s*(0x[0-9a-f]+)\s*\)',
        invocations,
    )
    if len(singleton_rows) != len(re.findall(r"\bCARBON_CF_SINGLETON\(", invocations)):
        raise ValueError("Unparsed CoreFoundation singleton binding")
    for name, index, slot in key_rows + singleton_rows:
        if name in entries:
            raise ValueError(f"Duplicate native binding: {name}")
        entries[name] = {
            "offset": int(index, 16),
            "library": "CoreFoundationLib",
            "data": True,
            "data_offset": int(slot, 16),
        }
    nqd_rows = re.findall(
        r"\bNQD_(?:TOOLBOX|OFFSCREEN)\(\s*(NQD\w+)\s*,\s*0x[0-9a-f]+\s*,"
        r"\s*0x[0-9a-f]+\s*,\s*\w+\s*,\s*(0x[0-9a-f]+)\s*\)",
        invocations,
    )
    if len(nqd_rows) != len(re.findall(r"\bNQD_(?:TOOLBOX|OFFSCREEN)\(", invocations)):
        raise ValueError("Unparsed NQD Toolbox binding")
    nqd_native_rows = re.findall(r"\bNQD_NATIVE\(\s*(\w+)\s*,\s*\w+\s*,\s*(0x[0-9a-f]+)\s*\)", invocations)
    if len(nqd_native_rows) != len(re.findall(r"\bNQD_NATIVE\(", invocations)):
        raise ValueError("Unparsed NQD native binding")
    for name, offset in nqd_rows + nqd_native_rows:
        scope = ("NQD", name)
        if scope in companion_scopes:
            raise ValueError(f"Duplicate native binding: {name}")
        companion_scopes.add(scope)
        entries[scope] = {"offset": int(offset, 16), "library": "NQD", "companion_only": True}
    if not entries:
        raise ValueError("No native bindings found")
    return entries


def application_imports(path):
    spec = importlib.util.spec_from_file_location("classic_disassemble", ROOT / "tools/classic-disasm/disassemble.py")
    parser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parser)
    sections, imports, _, _, _ = parser.parse_pef(path.read_bytes())
    loader = next(section["payload"] for section in sections if section["kind"] == 4)
    symbols = 56 + parser.u32(loader, 24) * 24
    return [
        dict(item, weak=bool(parser.u32(loader, symbols + 4 * item["index"]) & 0x80000000))
        for item in imports
        if item["library"] == "CarbonLib"
    ]


def trace_symbols(trace):
    return set(re.findall(r"PPC service CarbonLib:([^\s]+)", trace))


def reference_exports(reference):
    exports = {}
    for item in reference["exports"]:
        name = item["name"]
        if name in exports:
            raise ValueError(f"Duplicate reference export: {name}")
        exports[name] = item
    return exports


def audit(reference, native, imports=(), observed=(), companions=None):
    if "CarbonLib" in (companions or {}):
        raise ValueError("CarbonLib metadata belongs in the main reference, not companion metadata")
    companions = {name: reference_exports(metadata) for name, metadata in (companions or {}).items()}
    exports = reference_exports(reference)
    definitions = [(key[1] if isinstance(key, tuple) else key, item) for key, item in native.items()]
    main_native = {
        name: item for name, item in native.items() if not item.get("companion_only") and not item.get("extension")
    }
    extensions = {name: item for name, item in native.items() if item.get("extension")}
    imported = {item["index"]: item for item in reference.get("imports", [])}
    for name, location in definitions:
        if location.get("extension"):
            if name in exports:
                raise ValueError(f"Extension binding also appears in the main reference: {name}")
            continue
        if location.get("companion_only"):
            library = location["library"]
            item = companions.get(library, {}).get(name, {})
            if item.get("symbol_class") not in (0, 2) or item.get("code_offset") != location["offset"]:
                raise ValueError(f"Missing or mismatched companion reference: {library}:{name}")
            continue
        if name not in exports:
            raise ValueError(f"Native binding absent from reference: {name}")
        item = exports[name]
        offset, library = location["offset"], location["library"]
        classes = (1,) if location.get("data") else (0, 2)
        if library and item.get("section") == -3:
            origin = imported.get(offset, {})
            matched = (
                item.get("offset") == offset
                and origin.get("library") == library
                and origin.get("name") == name
                and origin.get("symbol_class") in classes
            )
        else:
            matched = item.get("code_offset") == offset
        if item["symbol_class"] not in classes or not matched:
            raise ValueError(f"Native/reference symbol mismatch: {name}")
    rows = []
    for item in imports:
        binding = main_native.get(item["name"]) or extensions.get(item["name"])
        classes = (1,) if binding and binding.get("data") else (0, 2)
        implemented = binding is not None and item["symbol_class"] in classes
        called = item["name"] in observed and item["symbol_class"] in (0, 2)
        rows.append(
            dict(item, native_archive=implemented, native_call_observed=called, in_reference=item["name"] in exports)
        )
    pending = [item for name, item in sorted(exports.items()) if name not in main_native]
    referenced_companions = {item["library"] for item in native.values() if item["library"]}
    for item in exports.values():
        if item.get("section") == -3:
            origin = imported.get(item["offset"])
            if not origin or not origin.get("library"):
                raise ValueError(f"Missing reference re-export origin: {item['name']}")
            referenced_companions.add(origin["library"])
    libraries = {
        "CarbonLib": {
            "reference_exports": len(exports),
            "native_archive_exports": len(main_native),
            "pending_reference_exports": len(pending),
        }
    }
    for library, items in companions.items():
        bindings = {name: item for name, item in definitions if item["library"] == library}
        for name, binding in bindings.items():
            classes = (1,) if binding.get("data") else (0, 2)
            item = items.get(name, {})
            if item.get("symbol_class") not in classes:
                raise ValueError(f"Native symbol absent from companion reference: {library}:{name}")
            if "data_offset" in binding and (item.get("offset") != binding["data_offset"] or item.get("section") != 1):
                raise ValueError(f"Native data record offset mismatch: {library}:{name}")
        libraries[library] = {
            "reference_exports": len(items),
            "native_archive_exports": len(bindings),
            "pending_reference_exports": len(items) - len(bindings),
        }
    return {
        "scope": "CarbonLib main PEF fragment and supplied companion metadata; application counts cover CarbonLib imports",
        "claims_full_compatibility": False,
        "reference_exports": len(exports),
        "native_archive_exports": len(main_native),
        "native_archive_total_exports": len(native),
        "native_companion_only_exports": len(native) - len(main_native) - len(extensions),
        "native_extension_exports": sorted(extensions),
        "native_code_exports": sum("code_offset" in exports[name] for name in main_native),
        "native_data_exports": sum(bool(item.get("data")) for item in main_native.values()),
        "native_reexport_exports": sum(exports[name].get("section") == -3 for name in main_native),
        "libraries": libraries,
        "unverified_companion_libraries": sorted(referenced_companions - companions.keys()),
        "pending_reference_exports": len(pending),
        "application_imports": len(rows),
        "application_native_imports": sum(row["native_archive"] for row in rows),
        "application_other_native_calls_observed": sum(
            row["native_call_observed"] and not row["native_archive"] for row in rows
        ),
        "application_pending_strong": sum(not row["native_archive"] and not row["weak"] for row in rows),
        "application_pending_weak": sum(not row["native_archive"] and row["weak"] for row in rows),
        "application": sorted(rows, key=lambda row: (row["native_archive"], row["weak"], row["name"])),
        "pending": pending,
        "note": "Pending entries may already have native bridge delegates or real PEF fallback; neither is counted as a new archive implementation.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Local CarbonLib pef.json")
    parser.add_argument(
        "--companion-reference",
        action="append",
        default=[],
        metavar="LIBRARY=PATH",
        help="Companion PEF metadata; otherwise discover sibling data-LIBRARY/pef.json",
    )
    parser.add_argument("--application", type=Path, help="Optional local PPC PEF application")
    parser.add_argument(
        "--trace", type=Path, help="Optional ROMAW stderr trace, measuring calls rather than API completeness"
    )
    parser.add_argument("--output", type=Path, default=ROOT / "build/carbon-coverage.json")
    args = parser.parse_args()
    sources = [ROOT / "carbonlib/src" / name for name in ("exports.c", "cf_key_entries.h", "nqd_entries.h")]
    native = native_entries("\n".join(source.read_text() for source in sources))
    companion_paths = {}
    for item in args.companion_reference:
        name, separator, path = item.partition("=")
        if not separator or not name or not path or name == "CarbonLib" or name in companion_paths:
            parser.error("Companion references must use distinct non-CarbonLib LIBRARY=PATH names")
        companion_paths[name] = Path(path)
    for item in native.values():
        library = item["library"]
        if library and library not in companion_paths:
            candidate = args.reference.parent.parent / f"data-{library}" / "pef.json"
            if candidate.is_file():
                companion_paths[library] = candidate
    companions = {name: json.loads(path.read_text()) for name, path in companion_paths.items()}
    imports = application_imports(args.application) if args.application else []
    observed = trace_symbols(args.trace.read_text(errors="replace")) if args.trace else ()
    report = audit(json.loads(args.reference.read_text()), native, imports, observed, companions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({key: value for key, value in report.items() if key not in {"pending", "application"}}, indent=2))


if __name__ == "__main__":
    main()
