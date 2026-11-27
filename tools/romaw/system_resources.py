#!/usr/bin/env python3
# +===================================================================
#   File:      system_resources.py
#
#   Summary:   Build the embedded System from extracted resources.
#
#   Classes:   None declared.
#
#   Functions: publish, read_resources, build_fork, apple_double, icon_inputs, generate_alert_table, generate
#
#   Origin:    Existing repository source in executor/src/tools; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Build the embedded System from extracted resources."""

import argparse
import os
import struct
import tempfile
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import mac_icons
import pictures
from resource_catalog import load_scope, read_rez, resource_path
from finder_guest_resources import FINDER_HANDLERS, finder_guest_payload
from native_code_resources import audit_native_resources, validate_alert_procedures

# Decompiled 68000 tail calls. The handlers already have maintained C bodies.
CODE_HANDLERS = {
    (b"dnrp", 0): (17, "ROMlib_mactcp_resolver_definition()"),
    (b"CDEF", 0): (0, "P_cdef0"),
    (b"CDEF", 1): (1, "P_cdef16"),
    (b"WDEF", 0): (2, "P_wdef0"),
    (b"WDEF", 1): (3, "P_wdef16"),
    (b"MDEF", 0): (4, "P_mdef0"),
    (b"LDEF", 0): (5, "P_ldef0"),
    (b"MBDF", 0): (6, "P_mbdf0"),
    (b"snth", 5): (7, "P_snth5"),
    (b"CDEF", 63): (9, "P_cdef1008"),
    (b"dcmp", 0): (10, "P_dcmp_template"),
}

FONT_TYPES = {b"FONT", b"NFNT", b"FOND", b"sfnt"}


def publish(path, data):
    """Do not expose a partial include when co-products regenerate in parallel."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def read_resources(directory):
    directory = directory.resolve()
    catalog, directory = load_scope(directory)
    records = []
    seen = set()
    for entry in catalog["resources"]:
        kind = bytes.fromhex(entry["type_hex"])
        key = kind, entry["id"]
        if len(kind) != 4 or key in seen or not -32768 <= key[1] <= 32767:
            raise ValueError("Invalid or duplicate resource identity")
        seen.add(key)
        files = entry.get("files", [entry.get("file")])
        split = "files" in entry
        if split and (not entry.get("icon") or kind != b"SICN" or not isinstance(files, list) or not files):
            raise ValueError("Only SICN resources can list multiple icon files")
        name = entry["name_hex"]
        name = None if name is None else bytes.fromhex(name)
        if name is not None and len(name) > 255:
            raise ValueError("Resource name exceeds Str255")
        parts = []
        for filename in files:
            path = resource_path(directory, filename)
            if "payload" in entry:
                payload = entry["payload"]
            elif "picture_origin" in entry:
                payload = pictures.resource(path.read_bytes(), *entry["picture_origin"])
            elif kind == b"CURS":
                payload = mac_icons.windows_cursor(path.read_bytes())
            elif entry.get("icon"):
                payload = mac_icons.resource(path.read_bytes(), kind, 32 if split else None)
            elif path.suffix == ".r":
                raise ValueError(f"Typed resource {key!r} requires a full Rez compiler")
            else:
                payload = path.read_bytes()
            parts.append(payload)
        payload = b"".join(parts)
        records.append((kind, key[1], entry["attributes"], name, payload))
    return records


def build_fork(records):
    groups = {}
    data = bytearray()
    names = bytearray()
    for kind, rid, attrs, name, payload in records:
        if len(data) > 0xFFFFFF:
            raise ValueError("Resource data exceeds 24-bit offsets")
        name_offset = 0xFFFF if name is None else len(names)
        if name is not None:
            names += bytes([len(name)]) + name
        reference = struct.pack(">hHII", rid, name_offset, (attrs & ~2) << 24 | len(data), 0)
        groups.setdefault(kind, []).append(reference)
        data += struct.pack(">I", len(payload)) + payload
        if len(data) % 2:
            data.append(0)
    types = bytearray(struct.pack(">H", (len(groups) - 1) & 0xFFFF))
    references = bytearray()
    for kind, refs in groups.items():
        types += struct.pack(">4sHH", kind, len(refs) - 1, 2 + len(groups) * 8 + len(references))
        references += b"".join(refs)
    name_offset = 28 + len(types) + len(references)
    if name_offset + len(names) > 65535:
        raise ValueError("Resource map exceeds 16-bit offsets")
    header = struct.pack(">IIII", 256, 256 + len(data), len(data), name_offset + len(names))
    resource_map = header + bytes(8) + struct.pack(">HH", 28, name_offset)
    return header + bytes(240) + data + resource_map + types + references + names


def apple_double(resource_fork):
    info = b"FFILDMOV" + bytes(24)
    return (
        struct.pack(">II16sH", 0x51607, 0x20000, b"Mac OS X        ", 2)
        + struct.pack(">IIIIII", 9, 50, 32, 2, 82, len(resource_fork))
        + info
        + resource_fork
    )


def icon_inputs(directory):
    """Quote whitespace in shared artwork paths expanded into GNU Make prerequisites."""
    return " ".join(
        path.as_posix().replace(" ", "\\ ").replace("\t", "\\\t")
        for path in sorted(directory.rglob("*"))
        if path.is_file() and path.suffix.lower() in {".icns", ".ico", ".cur", ".pict", ".bmp", ".png"}
    )


def generate_alert_table(source, output):
    """Embed the writable fallback independently of Resource Manager availability."""
    records = read_rez(source)
    if {(r["type"], r["id"]) for r in records} != {("DSAT", 0), ("DSAT", 2)} or len(records) != 2:
        raise ValueError("Expected DSAT 0 and 2 fallback resources")
    lines = ["/* Generated from system/resources/system/syserr.r; do not edit. */"]
    for entry in sorted(records, key=lambda r: r["id"]):
        name = "myalerttab" if entry["id"] == 0 else "myalerttab2"
        payload = entry["payload"]
        validate_alert_procedures(payload)
        lines.append(f"PRIVATE unsigned char {name}[] = {{")
        for offset in range(0, len(payload), 16):
            lines.append("    " + ", ".join(f"0x{x:02x}" for x in payload[offset : offset + 16]) + ",")
        lines.append("};")
    publish(output, ("\n".join(lines) + "\n").encode("ascii"))


def generate(directory, output):
    records = read_resources(directory)
    dispatch = ["/* Decompiled System code resource dispatch; generated. */"]
    for kind, _, _, _, _ in records:
        if kind in FONT_TYPES:
            raise ValueError(f"Font resource {kind!r} belongs in the external suitcase")
    entries = audit_native_resources(records, CODE_HANDLERS | FINDER_HANDLERS, require_system=True)
    for _, _, _, slot, handler in entries:
        dispatch.append(f"ROMLIB_SYSTEM_DEFINITION ({slot}, {handler})")
    fork = build_fork(records)
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated from system/resources/system; do not edit. */",
        "static const unsigned char embedded_system[] = {",
    ]
    for offset in range(0, len(fork), 16):
        lines.append("  " + ", ".join(f"0x{x:02x}" for x in fork[offset : offset + 16]) + ",")
    lines.append("};")
    publish(output, ("\n".join(lines) + "\n").encode("ascii"))
    publish(
        output.with_name("system_code.inc"),
        ("\n".join(dispatch) + "\n").encode("ascii"),
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icon-inputs", type=Path)
    parser.add_argument("--alert-table", action="store_true")
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    if args.icon_inputs is not None:
        print(icon_inputs(args.icon_inputs))
    elif args.source is None or args.output is None:
        parser.error("source and output are required")
    elif args.alert_table:
        generate_alert_table(args.source, args.output)
    else:
        generate(args.source, args.output)
