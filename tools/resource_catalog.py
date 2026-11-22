# +===================================================================
#   File:      resource_catalog.py
#
#   Summary:   Read authoritative Rez declarations, shared typed fields and artwork.
#
#   Classes:   None declared.
#
#   Functions: rez_string, read_rez, load_scope, resource_path, native_path, load_label_menu
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Read Rez metadata and literal data without a host Rez compiler.

Quoted includes inside typed bodies share fields while keeping resource records
independent. Classic dialog and text layouts are encoded directly; private Finder layouts
remain available to the native frontend and a full Rez compiler. External artwork in
$$read expressions is converted by the System builder to the requested type.
"""

import re
import struct

from rez_types import LABEL_MENU_FIELDS, compile_resource
from finder_guest_resources import finder_guest_payload
from native_code_resources import INTL1_LOCALIZATION_OFFSET, intl1_localization_payload
from pathlib import Path
from build_version import version_string

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / "system/resources"
FINDER_DIRECTORY = ROOT / "finder/resources"
ICONS = ROOT / "bitmaps"
ATTRIBUTES = {"sysheap": 64, "purgeable": 32, "locked": 16, "protected": 8, "preload": 4}
QUOTED = r'"(?:\\.|[^"\\])*"'
COMMENTS = re.compile(QUOTED + r"|'(?:\\.|[^'\\])*'|/\*.*?\*/|//[^\n]*", re.S)


def rez_string(value):
    """Decode a quoted Rez byte string, preserving MacRoman names exactly."""
    result = bytearray()
    index = 1
    while index < len(value) - 1:
        if value[index] != "\\":
            result.extend(value[index].encode("mac_roman"))
            index += 1
        elif re.match(r"\\0x[0-9a-fA-F]{2}", value[index:]):
            result.append(int(value[index + 3 : index + 5], 16))
            index += 5
        else:
            escapes = {"\\": 92, '"': 34, "'": 39, "n": 10, "r": 13, "t": 9}
            if index + 1 >= len(value) - 1 or value[index + 1] not in escapes:
                raise ValueError("Invalid Rez string escape")
            result.append(escapes[value[index + 1]])
            index += 2
    return bytes(result)


def _balanced(source, start, opening, closing):
    depth = 0
    tokens = re.compile(QUOTED + r"|'(?:\\.|[^'\\])*'|[" + re.escape(opening + closing) + "]")
    for token in tokens.finditer(source, start):
        if token[0] == opening:
            depth += 1
        elif token[0] == closing:
            depth -= 1
            if depth == 0:
                return source[start + 1 : token.start()], token.end()
    raise ValueError("Unterminated Rez declaration")


def _metadata(kind, header):
    tokens = re.findall(QUOTED + r"|[^,\s]+|,", header)
    if not tokens or any(token != "," for token in tokens[1::2]) or len(tokens) % 2 == 0:
        raise ValueError("Invalid Rez resource header")
    fields = tokens[::2]
    if not re.fullmatch(r"-?[0-9]+", fields[0]):
        raise ValueError("Invalid Rez resource ID")
    rid = int(fields.pop(0))
    name = rez_string(fields.pop(0)) if fields and fields[0].startswith('"') else None
    if not -32768 <= rid <= 32767 or (name is not None and len(name) > 255):
        raise ValueError("Rez ID or name exceeds Macintosh limits")
    if len(fields) != len(set(fields)) or not ATTRIBUTES.keys() >= set(fields):
        raise ValueError("Invalid or duplicate Rez attributes")
    kind = rez_string('"' + kind + '"')
    if len(kind) != 4:
        raise ValueError("Rez resource type must contain four bytes")
    return dict(
        type=kind.decode("mac_roman"),
        type_hex=kind.hex(),
        id=rid,
        attributes=sum(ATTRIBUTES[field] for field in fields),
        name_hex=None if name is None else name.hex(),
    )


def _include_fields(source, path, root, active):
    """Expand quoted field-fragment includes without sharing resource storage."""
    tokens = re.compile(QUOTED + r"|'(?:\\.|[^'\\])*'|^[ \t]*#include[ \t]+(" + QUOTED + r")[ \t]*(?:\n|$)", re.M)

    def expand(match):
        if match[1] is None:
            return match[0]
        fragment = (path.parent / rez_string(match[1]).decode("mac_roman")).resolve()
        if not fragment.is_relative_to(root):
            raise ValueError("Rez include escapes its resource scope")
        if fragment in active:
            raise ValueError("Cyclic Rez include")
        fields = COMMENTS.sub(
            lambda token: " " if token[0].startswith(("/*", "//")) else token[0],
            fragment.read_text(encoding="utf-8"),
        )
        return "\n" + _include_fields(fields, fragment, root, active | {fragment}) + "\n"

    return tokens.sub(expand, source)


def read_rez(path, root=None, _active=None):
    """Read local declaration/field includes, typed and literal data, and artwork."""
    path = Path(path).resolve()
    root = path.parent if root is None else Path(root).resolve()
    if not path.is_relative_to(root):
        raise ValueError("Rez include escapes its resource scope")
    active = set() if _active is None else _active
    if path in active:
        raise ValueError("Cyclic Rez include")
    active.add(path)
    source = COMMENTS.sub(
        lambda match: " " if match[0].startswith(("/*", "//")) else match[0],
        path.read_text(encoding="utf-8").replace("@ROMAW_VERSION@", version_string()),
    )
    records = []
    position = 0
    while source[position:].strip():
        position += len(source[position:]) - len(source[position:].lstrip())
        include = re.match(r"#include\s+(" + QUOTED + r")", source[position:])
        if include:
            filename = rez_string(include[1]).decode("mac_roman")
            # These layout templates contain no instances. Expanding them
            # requires Types.r from a full Rez installation.
            if filename not in {"resource_types.r", "Types.r"}:
                records.extend(read_rez(path.parent / filename, root, active))
            position += include.end()
            continue
        template = re.match(r"type\s+'[^'\n]{4}'\s*\{", source[position:])
        if template:
            _, position = _balanced(source, position + template.end() - 1, "{", "}")
            position += len(source[position:]) - len(source[position:].lstrip())
            if source[position : position + 1] != ";":
                raise ValueError("Missing Rez type terminator")
            position += 1
            continue
        declaration = re.match(r"(data|resource)\s+'((?:\\.|[^'\\])*)'\s*\(", source[position:])
        if not declaration:
            raise ValueError(f"Unsupported Rez declaration in {path}: {source[position:position + 60]!r}")
        header, position = _balanced(source, position + declaration.end() - 1, "(", ")")
        entry = _metadata(declaration[2], header)
        position += len(source[position:]) - len(source[position:].lstrip())
        if source[position : position + 1] != "{":
            raise ValueError("Missing Rez resource body")
        body, position = _balanced(source, position, "{", "}")
        position += len(source[position:]) - len(source[position:].lstrip())
        if source[position : position + 1] != ";":
            raise ValueError("Missing Rez declaration terminator")
        position += 1
        entry["file"] = path.relative_to(root).as_posix()
        if declaration[1] == "data":
            literal = r'\$"[0-9a-fA-F\s]*"'
            external = r"\$\$read\s*\(\s*(" + QUOTED + r")\s*\)"
            picture = re.fullmatch(r"\s*\$\$pict\s*\(\s*(" + QUOTED + r")\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*", body)
            if re.fullmatch(r"\s*\$\$finderCode\s*\(\s*\)\s*", body):
                try:
                    entry["payload"] = finder_guest_payload(bytes.fromhex(entry["type_hex"]), entry["id"])
                except KeyError as error:
                    raise ValueError("Unknown native Finder procedure") from error
            elif picture:
                filename = rez_string(picture[1]).decode("mac_roman")
                origin = tuple(map(int, picture.groups()[1:]))
                if (
                    entry["type"] != "PICT"
                    or Path(filename).suffix.lower() not in (".png", ".bmp")
                    or any(not -32768 <= coordinate <= 32767 for coordinate in origin)
                ):
                    raise ValueError(
                        "Picture declarations require PNG/BMP artwork, PICT type and valid frame coordinates"
                    )
                entry["file"] = (path.parent.relative_to(root) / filename).as_posix()
                entry["picture_origin"] = origin
            elif re.fullmatch(r"(?:\s*" + literal + r")*\s*", body):
                entry["payload"] = b"".join(bytes.fromhex(value) for value in re.findall(r'\$"([^"]*)"', body))
            elif re.fullmatch(r"(?:\s*" + literal + r")+\s*\$\$intl1Code\s*\(\s*\)\s*", body):
                prefix = b"".join(bytes.fromhex(value) for value in re.findall(r'\$"([^"]*)"', body))
                if entry["type"] != "INTL" or entry["id"] != 1 or len(prefix) != INTL1_LOCALIZATION_OFFSET:
                    raise ValueError("Native localization requires INTL 1's complete 330-byte data record")
                entry["payload"] = prefix + intl1_localization_payload()
            elif re.fullmatch(r"(?:\s*" + external + r")+\s*", body):
                files = [rez_string(value).decode("mac_roman") for value in re.findall(external, body)]
                files = [(path.parent.relative_to(root) / name).as_posix() for name in files]
                artwork = all(Path(name).suffix.lower() in {".icns", ".ico", ".cur", ".png"} for name in files)
                if len(files) > 1:
                    if entry["type_hex"] != b"SICN".hex() or not artwork:
                        raise ValueError("Only SICN resources can concatenate artwork files")
                    entry.pop("file")
                    entry["files"] = files
                else:
                    entry["file"] = files[0]
                if artwork and entry["type_hex"] != b"CURS".hex():
                    entry["icon"] = True
            else:
                raise ValueError(f"Unsupported Rez data expression in {path}")
        else:
            body = _include_fields(body, path, root, active)
            native_localization = (
                entry["type"] == "INTL" and entry["id"] == 1 and bool(re.search(r"\bNativeLocalization\b", body))
            )
            if native_localization:
                # Encode the data record with the existing typed serializer;
                # the native entry itself belongs to the code-veneer encoder.
                body = re.sub(r"\bNativeLocalization\b", "20085", body)

            def read_icon(filename):
                # Import lazily: mac_icons also uses this catalog for palettes.
                from mac_icons import resource

                if Path(filename).suffix.lower() != ".icns":
                    raise ValueError("System-error artwork must be an ICNS file")
                return resource(resource_path(root, filename).read_bytes(), b"ICON")

            payload = compile_resource(
                entry["type"],
                body,
                rez_string,
                read_icon,
                lambda filename: resource_path(root, filename).read_bytes(),
            )
            if payload is not None:
                if native_localization:
                    if len(payload) != INTL1_LOCALIZATION_OFFSET + 2 or payload[-2:] != b"\x4e\x75":
                        raise ValueError("Unexpected typed INTL 1 localization record")
                    payload = payload[:-2] + intl1_localization_payload()
                entry["payload"] = payload
        records.append(entry)
    active.remove(path)
    keys = [(entry["type_hex"], entry["id"]) for entry in records]
    if len(keys) != len(set(keys)):
        raise ValueError("Duplicate Rez resource identity")
    return records


def load_scope(directory, scope=None):
    directory = Path(directory).resolve()
    scope = scope or ("finder" if directory == FINDER_DIRECTORY or directory.name == "finder" else "system")
    if scope not in {"system", "finder"}:
        raise ValueError(f"Unknown resource scope: {scope}")
    directory = directory if (directory / "catalog.r").is_file() else directory / scope
    return {"resources": read_rez(directory / "catalog.r")}, directory


def resource_path(directory, filename):
    """Artwork is shared; all other paths remain within their resource scope."""
    directory = Path(directory).resolve()
    relative = Path(filename)
    if relative.suffix.lower() in (".icns", ".ico", ".cur", ".pict", ".bmp", ".png"):
        directory = ICONS
        if relative.parts[0] == "bitmaps":
            relative = Path(*relative.parts[1:])
    path = (directory / relative).resolve()
    if not path.is_relative_to(directory):
        raise ValueError(f"Resource path escapes its source directory: {filename}")
    return path


def native_path(name):
    """Native configuration has fixed source paths outside Macintosh forks."""
    return {
        "application_manifest": ROOT / "platform/os/cygwin32/ROMAW.manifest",
        "label_menu": FINDER_DIRECTORY / "label-menu.r",
    }[name]


def load_label_menu(path):
    """Decode the single typed native label-menu resource into build parameters."""
    entries = read_rez(path)
    if len(entries) != 1 or entries[0]["type"] != "lMnu" or entries[0]["id"] != 0:
        raise ValueError("Expected one lMnu resource with ID zero")
    payload = entries[0].get("payload", b"")
    header_size = 2 * len(LABEL_MENU_FIELDS)
    if len(payload) < header_size:
        raise ValueError("Truncated label-menu geometry")
    menu = dict(zip(LABEL_MENU_FIELDS, struct.unpack_from(">" + "h" * len(LABEL_MENU_FIELDS), payload)))
    if any(menu[name] <= 0 for name in LABEL_MENU_FIELDS[:6]) or not 0 <= menu["separator_gray"] <= 255:
        raise ValueError("Invalid label-menu geometry or separator colour")
    if not 0 <= menu["icon_color_index"] <= 255:
        raise ValueError("Invalid label-menu color-table index")
    offset = header_size

    def string():
        nonlocal offset
        if offset >= len(payload):
            raise ValueError("Truncated label-menu string")
        size = payload[offset]
        offset += 1
        if size > len(payload) - offset:
            raise ValueError("Truncated label-menu string")
        value = payload[offset : offset + size].decode("mac_roman")
        offset += size
        return value

    labels = []
    for _ in range(8):
        name = string()
        if len(payload) - offset < 3:
            raise ValueError("Truncated label-menu colour")
        labels.append(dict(name=name, rgb=list(payload[offset : offset + 3])))
        offset += 3
    menu["labels"] = labels
    menu["check_bitmap"] = [string() for _ in range(8)]
    if any(len(row) != 9 or set(row) - {".", "#"} for row in menu["check_bitmap"]):
        raise ValueError("The checkmark must be an 8 by 9 bitmap of dots and hashes")
    if offset != len(payload):
        raise ValueError("Unexpected trailing label-menu data")
    return menu
