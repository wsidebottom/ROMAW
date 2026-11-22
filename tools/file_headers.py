#!/usr/bin/env python3
# +===================================================================
#   File:      file_headers.py
#
#   Summary:   Add and check descriptive file headers while preserving original source notices.
#
#   Classes:   Style
#
#   Functions: is_generated_artifact, style_for, prologue_end, header_span, without_header, validate_header, retained_origin,
#              introductory_text, role_for, summary_for, parsers, walk, declaration_name, expand_trap_definitions,
#              c_symbols, metadata, render_header, add_header, candidates, maintain_headers, main
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Add and check descriptive file headers while preserving original source notices.

C/C++ API discovery uses tree-sitter 0.25.2, tree-sitter-c 0.24.1 and
tree-sitter-cpp 0.23.4. Checking existing headers needs only Python's standard library.
"""

import argparse
import ast
import json
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parent.parent
LAYOUT = json.loads((ROOT / "tools/source-layout.json").read_text())
ROOTS = set(LAYOUT["source_roots"])
BORDER = "=" * 67
UPSTREAM = {
    "ar-lib",
    "config.guess",
    "config.sub",
    "install-sh",
    "missing",
    "depcomp",
    "ltmain.sh",
    "test-driver",
    "ylwrap",
}
C_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp", ".m"}
GENERATED = re.compile(
    rb"(?im)^\s*(?:/\*|//|\*|#|;|dnl)?\s*(?:automatically generated|generated (?:by|from)|"
    rb"this file (?:is|was) (?:automatically )?generated|do not edit|"
    rb"Microsoft Visual C\+\+ generated (?:include file|resource script))\b"
)
ROLES = {
    "runtime": "Macintosh startup, traps and runtime services",
    "memory": "Macintosh heaps, handles and temporary memory",
    "files": "Macintosh File Manager and host-file operations",
    "hfs": "HFS volumes and catalog operations",
    "quickdraw": "Macintosh QuickDraw graphics",
    "display": "display refresh and pixel conversion",
    "controls": "Macintosh Control Manager operations",
    "dialogs": "Macintosh Dialog Manager operations",
    "lists": "Macintosh List Manager operations",
    "menus": "Macintosh Menu Manager operations",
    "windows": "Macintosh Window Manager operations",
    "classic-ui": "shared classic window, menu and control drawing",
    "frontend-sdl": "Windows SDL frontend integration",
    "text": "Macintosh fonts, scripts and international text",
    "textedit": "Macintosh TextEdit operations",
    "appleevents": "Macintosh AppleEvent handling",
    "events": "Macintosh event handling and virtual interrupts",
    "sound": "Macintosh sound playback and conversion",
    "printing": "printing and print settings",
    "numerics": "Macintosh numeric and floating-point operations",
    "devices": "Macintosh device and communications services",
    "options": "ROMAW options and preferences",
    "ui": "ROMAW application appearance and help",
    "finder": "the native ROMAW Finder",
    "services": "Macintosh Toolbox services",
    "host": "host operating-system support",
    "diagnostics": "ROMAW diagnostics and tracing",
    "tools": "development and source-generation tools",
    "tests": "regression tests and test fixtures",
    "resources": "classic Macintosh resources",
    "include": "public and internal interfaces",
    "config": "host-platform configuration and integration",
}
SUMMARIES = {
    "runtime/check_structs.c": "Verifies structure sizes and offsets imposed by the Macintosh ABI.",
    "system/util/configure.sh": "Processes compiler, host, build and file-format options for ROMAW configuration.",
    "syn68k/src/runtime/68k.defines.scm": "Defines 68K addressing-mode constraints and reusable instruction-generator forms.",
    "syn68k/src/runtime/i486-cleanup.pl": "Filters redundant assembly around generated syn68k handlers, retaining required code and debug sections.",
    "syn68k/src/runtime/rangetree.c": "Maps 68K address ranges to translated blocks using a red-black tree.",
}


@dataclass(frozen=True)
class Style:
    language: str
    opening: bytes
    closing: bytes
    prefix: str = ""


def is_generated_artifact(data):
    """Identify generator-owned output while retaining maintained files with explicit metadata."""
    return bool(GENERATED.search(data[:2048])) and not re.search(rb"(?m)^(?:# |; |dnl )?  File:", data[:1024])


def style_for(name, data):
    """Select syntax only for maintained code, never reference or generated artifacts."""
    path = Path(name)
    if not path.parts or (path.parts[0] not in ROOTS and name not in LAYOUT["root_build_files"]) or ".." in path.parts:
        return None
    if any(p in {"third_party", "obj", "build"} or p.startswith("%") for p in path.parts):
        return None
    if path.name in UPSTREAM or path.suffix in {".orig", ".historical", ".header-tmp", ".format-tmp"} or b"\0" in data:
        return None
    if is_generated_artifact(data):
        return None
    suffix = path.suffix
    if suffix in C_SUFFIXES or name.endswith(".h.in"):
        declarations = re.sub(rb"/\*.*?\*/|//[^\n]*", b"", data, flags=re.S)
        cpp_header = (suffix == ".h" or name.endswith(".h.in")) and re.search(
            rb"(?m)^\s*(?:namespace\s+\w|template\s*<|class\s+\w)", declarations
        )
        language = "cpp" if suffix in {".cc", ".cpp", ".hpp"} or cpp_header else "c"
        return Style(language, ("/*+" + BORDER).encode(), (BORDER + "+*/").encode())
    if suffix in {".s", ".S", ".r", ".rc", ".y", ".ld", ".inc"} or (suffix == ".tmpl" and path.name.startswith("gen")):
        return Style(
            "assembly" if suffix in {".s", ".S"} else "declarations",
            ("/*+" + BORDER).encode(),
            (BORDER + "+*/").encode(),
        )
    if suffix in {".scm", ".rawdef"}:
        return Style(
            "scheme" if suffix == ".scm" else "imports", ("; +" + BORDER).encode(), ("; " + BORDER + "+").encode(), "; "
        )
    if suffix == ".ac":
        return Style("configure", ("dnl +" + BORDER).encode(), ("dnl " + BORDER + "+").encode(), "dnl ")
    if suffix == ".manifest":
        return Style("xml", ("<!--+" + BORDER).encode(), (BORDER + "+-->").encode())
    language = {
        ".py": "python",
        ".sh": "shell",
        ".bash": "shell",
        ".pl": "perl",
        ".pm": "perl",
        ".ps1": "powershell",
        ".psm1": "powershell",
        ".awk": "awk",
        ".gdb": "gdb",
        ".am": "make",
        ".make": "make",
        ".mk": "make",
        ".tmpl": "template",
    }.get(suffix)
    if path.name.startswith(("Makefile", "makefile")):
        language = "make"
    elif name.endswith(".gdb.in"):
        language = "gdb"
    elif language is None and data.startswith(b"#!"):
        first = data.splitlines()[0]
        language = "python" if b"python" in first else "perl" if b"perl" in first else "shell"
    if language:
        return Style(language, ("# +" + BORDER).encode(), ("# " + BORDER + "+").encode(), "# ")
    return None


def prologue_end(data, style):
    """Keep BOMs, executable shebangs, Python encoding cookies and XML declarations first."""
    offset = 3 if data.startswith(b"\xef\xbb\xbf") else 0
    lines = data[offset:].splitlines(keepends=True)
    used = 0
    if lines and lines[0].startswith(b"#!"):
        used = 1
    if style.language == "python":
        for index, line in enumerate(lines[:2]):
            if re.match(rb"^[ \t\f]*#.*?coding[:=][ \t]*[-\w.]+", line):
                used = max(used, index + 1)
    if style.language == "xml" and lines and lines[0].lstrip().startswith(b"<?xml"):
        used = 1
    return offset + sum(map(len, lines[:used]))


def header_span(data, style):
    start = prologue_end(data, style)
    if not data[start:].startswith(style.opening + b"\n"):
        return None
    end = data.find(b"\n" + style.closing + b"\n", start)
    if end < 0:
        raise ValueError("Unterminated file header")
    end += len(style.closing) + 2
    if data[end : end + 1] == b"\n":
        end += 1
    return start, end


def without_header(data, style):
    span = header_span(data, style)
    return data if span is None else data[: span[0]] + data[span[1] :]


def validate_header(name, data, style):
    span = header_span(data, style)
    if span is None:
        return False
    text = data[span[0] : span[1]].decode("ascii", errors="replace")
    fields = {
        key: value.strip()
        for key, value in re.findall(
            r"(?m)^\s*(?:#\s*|;\s*|dnl\s*)?(File|Summary|Classes|Functions|Origin):\s*(.+)$", text
        )
    }
    return (
        fields.get("File") == Path(name).name
        and all(fields.get(k) for k in ("Summary", "Classes", "Functions", "Origin"))
        and bool(re.search(r"copyright|licen[cs]e|legal notices|public.domain|all rights reserved", text, re.I))
    )


def retained_origin(data, style):
    """Retain explicit provenance when refreshing generated API metadata."""
    span = header_span(data, style)
    if span is None:
        return ""
    text = data[span[0] : span[1]].decode("ascii", errors="replace")
    if re.search(
        r"\bcopyright\s*(?:\(c\)|[0-9])|SPDX-License-Identifier|permission is hereby granted|all rights reserved|licensed under",
        text,
        re.I,
    ) or any(c > 127 for c in data[span[0] : span[1]]):
        raise ValueError("Header contains embedded attribution or license text; update its metadata manually")
    pieces = []
    reading = False
    for line in text.splitlines():
        if style.prefix and line.startswith(style.prefix):
            line = line[len(style.prefix) :]
        elif style.prefix and line == style.prefix.rstrip():
            line = ""
        if line.startswith("  Origin:"):
            pieces = [line.partition(":")[2].strip()]
            reading = True
        elif reading:
            if not line.strip():
                break
            pieces.append(line.strip())
    origin = " ".join(pieces)
    return "" if origin.startswith(("Existing repository source in ", "Repository-maintained ")) else origin


def introductory_text(data, style):
    body = data[prologue_end(data, style) :].decode("utf-8", errors="replace").lstrip()
    pieces = []
    while body:
        if body.startswith("/*"):
            end = body.find("*/")
            if end < 0:
                break
            pieces.append(re.sub(r"(?m)^\s*\* ?", "", body[2:end]).strip())
            body = body[end + 2 :].lstrip()
        elif body.startswith(("//", "#", ";", "dnl ")):
            line, _, body = body.partition("\n")
            if line.startswith("#") and re.match(r"#\s*(?:include|define|if|pragma|undef|error)\b", line):
                break
            pieces.append(re.sub(r"^(?://|#|;|dnl)\s*", "", line))
            body = body.lstrip()
        else:
            break
    return "\n".join(pieces)


def role_for(name):
    path = Path(name)
    if path.parts[0] in ROLES:
        return ROLES[path.parts[0]]
    if path.parts[0] in {"carbonlib", "classic-ui", "disk-images", "finder-metadata"}:
        return path.parts[0].replace("-", " ") + " library support"
    if path.parts[0] == "syn68k":
        return "syn68k 68K translation and runtime support"
    if path.parts[0] == "synppc":
        return "synppc PowerPC execution and fragment loading"
    if path.parts[0] == "packages":
        return path.parts[1].replace("-", " ") + " package support"
    return next((ROLES[p] for p in path.parts[1:] if p in ROLES), "ROMAW build and development support")


def summary_for(name, data, style, intro):
    if name in SUMMARIES:
        return SUMMARIES[name]
    if style.language == "python":
        try:
            doc = ast.get_docstring(ast.parse(data))
            if doc:
                return doc.split("\n\n")[0].replace("\n", " ")
        except (SyntaxError, UnicodeError):
            pass
    paragraphs = re.split(r"\n\s*\n", intro)
    for paragraph in paragraphs:
        if paragraph.strip() and not re.search(
            r"copyright|license|permission|rights reserved|\$[A-Za-z]+:|warranty|warranties|DO NOT|contributors:|created by|^;+|^=+",
            paragraph,
            re.I,
        ):
            first = re.split(r"\n(?:Format:|https?://|Reference:)", paragraph)[0]
            if len(first) > 15 and re.search(r"[A-Za-z]{3}", first) and not re.match(r"[-*=]{6}", first):
                return " ".join(first.split())
    title = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", Path(name).stem).replace("_", " ").replace("-", " ").lower()
    verb = "Declares" if Path(name).suffix in {".h", ".hpp"} or name.endswith(".h.in") else "Implements"
    if style.language == "make":
        return f"Defines build targets and dependencies for {role_for(name)}."
    if style.language == "configure":
        return f"Configures platform and dependency checks for {role_for(name)}."
    if style.language == "gdb":
        return f"Provides {title} debugger commands for {role_for(name)}."
    return f"{verb} {title} support for {role_for(name)}."


_PARSERS = None


def parsers():
    global _PARSERS
    if _PARSERS is None:
        try:
            from tree_sitter import Language, Parser
            import tree_sitter_c
            import tree_sitter_cpp
        except ImportError as exc:
            raise RuntimeError(
                "Install tree-sitter==0.25.2 tree-sitter-c==0.24.1 tree-sitter-cpp==0.23.4 to add C/C++ headers"
            ) from exc
        _PARSERS = {
            "c": Parser(Language(tree_sitter_c.language())),
            "cpp": Parser(Language(tree_sitter_cpp.language())),
        }
    return _PARSERS


def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


def declaration_name(node):
    if node is None:
        return ""
    if node.type in {
        "identifier",
        "field_identifier",
        "qualified_identifier",
        "operator_name",
        "destructor_name",
        "type_identifier",
    }:
        return node.text.decode("ascii", errors="backslashreplace")
    child = node.child_by_field_name("declarator")
    return declaration_name(child) if child else ""


def expand_trap_definitions(data):
    """Present ARDI's A/P declaration macros to the parser as ordinary signatures."""
    pattern = re.compile(rb"(?m)^[ \t]*([AP])\d+\s*\(")
    parts, end = [], 0
    for match in pattern.finditer(data):
        if match.start() < end:
            continue
        cursor, depth = match.end(), 1
        while cursor < len(data) and depth:
            if data[cursor : cursor + 2] == b"/*":
                close = data.find(b"*/", cursor + 2)
                cursor = len(data) if close < 0 else close + 2
                continue
            depth += (data[cursor] == ord("(")) - (data[cursor] == ord(")"))
            cursor += 1
        args = re.sub(rb"/\*.*?\*/", b" ", data[match.end() : cursor - 1], flags=re.S).split(b",", 3)
        if depth or len(args) < 3 or args[0].strip() not in {b"PUBLIC", b"PRIVATE"}:
            continue
        visibility, result, name = (arg.strip() for arg in args[:3])
        if not re.fullmatch(rb"[A-Za-z_]\w*", name):
            continue
        if match[1] == b"P":
            name = b"C_" + name
        prefix = b"static " if visibility == b"PRIVATE" else b""
        parts.extend([data[end : match.start()], prefix + result + b" " + name + b"(void)"])
        end = cursor
    return b"".join(parts) + data[end:]


def c_symbols(data, language):
    data = expand_trap_definitions(data)
    tree = parsers()[language].parse(data)
    functions, classes, used = set(), set(), set()
    for node in walk(tree.root_node):
        if node.type in {"struct_specifier", "class_specifier"}:
            name = node.child_by_field_name("name")
            if node.child_by_field_name("body") and name and name.text not in {b"PACKED", b"PUBLIC", b"PRIVATE"}:
                classes.add(name.text.decode("ascii", errors="backslashreplace"))
            if node.child_by_field_name("body") and node.parent and node.parent.type == "type_definition":
                alias = declaration_name(node.parent.child_by_field_name("declarator"))
                if alias:
                    classes.add(alias)
        if node.type == "type_identifier":
            used.add(node.text.decode("ascii", errors="backslashreplace"))
        if node.type != "function_definition":
            continue
        declarator = node.child_by_field_name("declarator")
        name = declaration_name(declarator)
        if not name or re.fullmatch(r"[AP]\d+", name) or name in {"if", "for", "while", "switch", "catch"}:
            continue
        ancestors, parent = [], node.parent
        while parent:
            ancestors.append(parent)
            parent = parent.parent
        if any(p.type == "function_definition" for p in ancestors):
            continue
        if any(p.type == "namespace_definition" and p.child_by_field_name("name") is None for p in ancestors):
            continue
        member = any(p.type in {"class_specifier", "struct_specifier"} for p in ancestors)
        prefix = data[node.start_byte : declarator.start_byte] if declarator else b""
        if not member and re.search(rb"\b(?:static|PRIVATE)\b", prefix):
            continue
        scopes = [
            p.child_by_field_name("name").text.decode("ascii", errors="backslashreplace")
            for p in reversed(ancestors)
            if p.type in {"namespace_definition", "class_specifier", "struct_specifier"}
            and p.child_by_field_name("name")
        ]
        qualifier = "::".join(scopes)
        functions.add(qualifier + "::" + name if qualifier and not name.startswith(qualifier + "::") else name)
    standard_types = rb"string|wstring|string_view|u16string|u32string|vector|map|set|unordered_map|unordered_set|array|pair|tuple|optional|unique_ptr|shared_ptr|weak_ptr|thread|mutex|recursive_mutex|condition_variable|atomic|atomic_flag|unique_lock|lock_guard|function|exception|runtime_error|invalid_argument|bad_alloc|numeric_limits|error_code|ifstream|ofstream|fstream|istringstream|ostringstream|filesystem::(?:path|directory_entry|directory_iterator|recursive_directory_iterator|file_status|space_info)"
    used.update("std::" + m.decode() for m in re.findall(rb"\bstd::(" + standard_types + rb")\b", data))
    return {"functions": sorted(functions), "classes": sorted(classes), "used": sorted(used)}


def metadata(name, data, style, symbols=None, known_classes=()):
    intro = introductory_text(data, style)
    classes = "None."
    functions = "No explicitly exported functions."
    if style.language in {"c", "cpp"}:
        symbols = symbols or c_symbols(data, style.language)
        if style.language == "cpp":
            declared = symbols["classes"]
            used = sorted(
                set(symbols["used"]) & (set(known_classes) | {n for n in symbols["used"] if n.startswith("std::")})
            )
            classes = (
                "; ".join(
                    filter(
                        None,
                        [
                            "Declared: " + ", ".join(declared) if declared else "",
                            "Used: " + ", ".join(used) if used else "",
                        ],
                    )
                )
                or "None identified in this file."
            )
        if Path(name).suffix in {".h", ".hpp"} or name.endswith(".h.in"):
            functions = "Interface declarations and inline definitions; see the declarations below."
        else:
            functions = ", ".join(symbols["functions"]) or "No externally linked function definitions."
    elif style.language == "python":
        try:
            module = ast.parse(data)
            classes = ", ".join(n.name for n in module.body if isinstance(n, ast.ClassDef)) or "None declared."
            functions = (
                ", ".join(
                    n.name
                    for n in module.body
                    if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and not n.name.startswith("_")
                )
                or "No public module functions."
            )
        except (SyntaxError, UnicodeError):
            functions = "Script entry point; see definitions below."
    elif style.language == "assembly":
        functions = (
            ", ".join(sorted(set(m.decode() for m in re.findall(rb"(?m)^\s*\.(?:globl|global)\s+([\w.$]+)", data))))
            or "Local assembly entry points; see labels below."
        )
    elif style.language == "powershell":
        classes = ", ".join(n.decode() for n in re.findall(rb"(?im)^\s*class\s+([\w]+)", data)) or "None declared."
        if Path(name).suffix == ".psm1":
            functions = "Module definitions: " + ", ".join(
                n.decode() for n in re.findall(rb"(?im)^\s*function\s+([\w-]+)", data)
            )
    elif style.language == "awk":
        functions = (
            ", ".join(m.decode() for m in re.findall(rb"(?m)^\s*function\s+(\w+)\s*\(", data))
            or "Pattern/action entry points."
        )
    elif style.language == "imports":
        names = re.findall(rb"(?m)^\s*([A-Za-z_]\w*)@\d+", data)
        functions = (
            "Imported procedures: " + ", ".join(m.decode() for m in names)
            if names
            else "Import-definition declarations."
        )
    elif style.language == "gdb":
        names = re.findall(rb"(?m)^\s*define\s+([\w-]+)", data)
        functions = (
            "Debugger commands: " + ", ".join(n.decode() for n in names)
            if names
            else "Debugger command sequence; no exported functions."
        )
    origin_lines = [
        line.strip()
        for line in intro.splitlines()
        if re.search(
            r"https?://|derived from|based on|adapted from|ported from|written by|created by|original.*reference|^Format:|^Reference:",
            line,
            re.I,
        )
    ]
    origin = (
        " ".join(origin_lines)
        or f"Existing repository source in {Path(name).parent.as_posix()}; original attribution and provenance comments are preserved below."
    )
    license_path = next(
        (
            p / filename
            for p in [ROOT / Path(name).parent, *(ROOT / Path(name).parent).parents]
            if p == ROOT or ROOT in p.parents
            for filename in ("LICENSE", "COPYING")
            if (p / filename).is_file()
        ),
        ROOT / "COPYING",
    )
    legal = (
        "Original public-domain and warranty notices are retained below."
        if re.search(r"public domain|not copyrighted", intro, re.I)
        else f"File-specific notices, where present, are retained below. License reference: {license_path.relative_to(ROOT).as_posix()}."
    )
    return {
        "File": Path(name).name,
        "Summary": summary_for(name, data, style, intro),
        "Classes": classes,
        "Functions": functions,
        "Origin": origin,
        "Copyright and Legal notices": legal,
    }


def render_header(fields, style):
    lines = [style.opening.decode()]
    for key in ("File", "Summary", "Classes", "Functions", "Origin", "Copyright and Legal notices"):
        if len(lines) > 1:
            lines.append(style.prefix.rstrip())
        value = fields[key]
        if style.opening.startswith(b"/*"):
            value = value.replace("/*", "/ *").replace("*/", "* /")
        elif style.opening.startswith(b"<!--"):
            value = value.replace("--", "- -")
        value = value.encode("ascii", errors="backslashreplace").decode()
        indent = style.prefix + "  "
        label = f"{key}:".ljust(11) if key != "Copyright and Legal notices" else key + ": "
        first = indent + label
        continuation = indent + " " * (11 if key != "Copyright and Legal notices" else 11)
        lines.extend(
            textwrap.wrap(
                " ".join(value.split()),
                width=120,
                initial_indent=first,
                subsequent_indent=continuation,
                break_long_words=False,
                break_on_hyphens=False,
            )
        )
    lines.append(style.closing.decode())
    return ("\n".join(lines) + "\n\n").encode()


def add_header(name, data, style, symbols=None, known_classes=(), refresh=False):
    if header_span(data, style) is not None and not refresh:
        if not validate_header(name, data, style):
            raise ValueError(f"{name}: repair the incomplete header or regenerate it with --refresh")
        return data
    body = without_header(data, style)
    try:
        existing_origin = retained_origin(data, style) if refresh else ""
    except ValueError as exc:
        raise ValueError(f"{name}: {exc}") from exc
    position = prologue_end(body, style)
    prefix = body[:position]
    if prefix and prefix != b"\xef\xbb\xbf" and not prefix.endswith(b"\n"):
        prefix += b"\n"
    fields = metadata(name, body, style, symbols, known_classes)
    if existing_origin:
        fields["Origin"] = existing_origin
    return prefix + render_header(fields, style) + body[position:]


def candidates(paths=()):
    names = paths or subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT
    ).decode().split("\0")
    for name in sorted(set(names)):
        if not name:
            continue
        path = ROOT / name
        if not path.is_file() or path.is_symlink():
            continue
        data = path.read_bytes()
        style = style_for(name, data)
        if style:
            yield name, data, style


def maintain_headers(check=False, refresh=False, paths=()):
    entries = list(candidates(paths))
    missing = [name for name, data, style in entries if not validate_header(name, data, style) or b"\r\n" in data]
    if check:
        return len(entries), missing
    symbols = {}
    known = set()
    targets = {name for name, _, _ in entries} if refresh else set(missing)
    needs_classes = any(name in targets and style.language == "cpp" for name, _, style in entries)
    if targets:
        for name, data, style in entries:
            if style.language in {"c", "cpp"} and (name in targets or needs_classes):
                symbols[name] = c_symbols(without_header(data, style), style.language)
                known.update(symbols[name]["classes"])
    changed = []
    for name, original, style in entries:
        path = ROOT / name
        discovered = symbols.get(name)
        for attempt in range(3):
            if not path.is_file():
                break
            latest = path.read_bytes()
            if latest != original:
                original = latest
                style = style_for(name, original)
                if style is None:
                    break
                discovered = (
                    c_symbols(without_header(original, style), style.language)
                    if style.language in {"c", "cpp"}
                    else None
                )
            updated = add_header(name, original, style, discovered, known, refresh).replace(b"\r\n", b"\n")
            if updated == original:
                break
            if path.read_bytes() != original:
                continue
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    dir=path.parent, prefix=f".{path.name}.", suffix=".header-tmp", delete=False
                ) as stream:
                    stream.write(updated)
                    temporary = Path(stream.name)
                temporary.chmod(path.stat().st_mode)
                if path.read_bytes() != original:
                    continue
                temporary.replace(path)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
            changed.append(name)
            break
        else:
            raise RuntimeError(f"{name} keeps changing; retry without overwriting the edit")
    return len(entries), changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--refresh", action="store_true", help="Regenerate metadata in existing headers")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()
    count, changed = maintain_headers(args.check, args.refresh, args.paths)
    print(
        f"{'Checked' if args.check else 'Updated'} {count} source file headers; {len(changed)} {'missing or invalid' if args.check else 'changed'}."
    )
    if args.check and changed:
        print("\n".join(changed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
