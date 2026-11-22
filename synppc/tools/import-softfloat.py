# +===================================================================
#   File:      import-softfloat.py
#
#   Summary:   Import the byte-verified floating-point dependency closure from SoftFloat 3e.
#
#   Classes:   None declared.
#
#   Functions: import_sources
#
#   Origin:    Existing repository source in synppc/tools; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Import the byte-verified floating-point dependency closure from SoftFloat 3e.

Build the official release in build/romaw-generic first, using its x86-64
Makefile, ARM-VFPv2 specialization and i386 platform.h (no __int128). The
release archive must be beside its extracted SoftFloat-3e directory. This
offline importer refuses modified input files or different existing output.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ARCHIVE_SHA256 = "21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746"
ROOTS = [
    f"f{width}_{operation}"
    for width in (32, 64)
    for operation in (
        "add",
        "sub",
        "mul",
        "div",
        "sqrt",
        "mulAdd",
        "roundToInt",
        "to_i32",
        "to_i32_r_minMag",
        "eq",
        "lt",
        "le",
        "eq_signaling",
        "isSignalingNaN",
    )
] + [
    "f32_to_f64",
    "f64_to_f32",
    "f32_to_extF80",
    "f64_to_extF80",
    "extF80_to_f64",
    "extF80_to_f32",
    "extF80_add",
    "extF80_sub",
    "extF80_mul",
    "extF80_div",
    "extF80_sqrt",
    "f64_to_f128",
    "extF80_to_f128",
    "f128_to_f64",
    "f128_to_extF80",
    "f128_to_i32",
    "f128_roundToInt",
    "f128_add",
    "f128_sub",
    "f128_mul",
    "f128_div",
    "f128_sqrt",
]


def import_sources(upstream, destination):
    upstream = upstream.resolve()
    archive = upstream.parent / "SoftFloat-3e.zip"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("The official SoftFloat 3e archive hash does not match")
    objects = sorted((upstream / "build/romaw-generic").glob("*.o"))
    if not objects:
        raise ValueError("Build the generic upstream reference first")

    def symbols(undefined):
        output = subprocess.check_output(
            [
                "nm",
                "-A",
                "-g",
                "--undefined-only" if undefined else "--defined-only",
                *objects,
            ],
            text=True,
        )
        result = {}
        for line in output.splitlines():
            if ":" in line:
                name = Path(line.split(":", 1)[0]).stem
                result.setdefault(name, set()).add(line.split()[-1])
        return result

    definitions = {symbol: obj for obj, entries in symbols(False).items() for symbol in entries}
    undefined = symbols(True)
    pending = [definitions[symbol] for symbol in ROOTS]
    selected = set()
    while pending:
        obj = pending.pop()
        if obj in selected:
            continue
        selected.add(obj)
        for symbol in undefined.get(obj, ()):
            if symbol in definitions:
                pending.append(definitions[symbol])
            elif symbol.startswith("softfloat_"):
                raise ValueError(f"Unresolved SoftFloat dependency: {symbol}")
    sources = []
    for obj in sorted(selected):
        relative = Path("source/ARM-VFPv2") / (obj + ".c")
        if not (upstream / relative).is_file():
            relative = Path("source") / (obj + ".c")
        sources.append(relative)
    files = sources + [
        Path("COPYING.txt"),
        Path("README.txt"),
        Path("source/ARM-VFPv2/specialize.h"),
    ]
    files += [path.relative_to(upstream) for path in sorted((upstream / "source/include").glob("*.h"))]
    manifest = {}
    with zipfile.ZipFile(archive) as release:
        for relative in files:
            data = release.read("SoftFloat-3e/" + relative.as_posix())
            if data != (upstream / relative).read_bytes():
                raise ValueError(f"Upstream source was modified: {relative}")
            output = destination / relative
            if output.exists() and output.read_bytes() != data:
                raise ValueError(f"Refusing to overwrite changed vendor file: {output}")
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
            manifest[relative.as_posix()] = hashlib.sha256(data).hexdigest()
    (destination / "manifest.json").write_text(
        json.dumps(
            {"archive_sha256": ARCHIVE_SHA256, "files": manifest},
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"Imported {len(sources)} C files and {len(files) - len(sources)} headers/notices")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument(
        "--destination",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "third_party/softfloat3",
    )
    arguments = parser.parse_args()
    import_sources(arguments.upstream, arguments.destination)
