# +===================================================================
#   File:      build-fpsp.py
#
#   Summary:   Build the original Motorola FPSP and emit a relocatable guest image.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Existing repository source in syn68k/tools; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Build the original Motorola FPSP and emit a relocatable guest image."""

import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    source = root / "third_party/fpsp040"
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get("M68K_TOOL_PREFIX", "m68k-linux-gnu-")
    objects = []
    for assembly in [root / "src/runtime/fpsp-adapter.S", *sorted(source.glob("*.S"))]:
        obj = output / (assembly.stem + ".o")
        subprocess.run(
            [prefix + "gcc", "-m68040", "-I" + str(source), "-c", assembly, "-o", obj],
            check=True,
        )
        objects.append(obj)
    base = 0x10000
    elf = output / "fpsp.elf"
    subprocess.run(
        [
            prefix + "ld",
            "--emit-relocs",
            f"-Ttext={base:#x}",
            "-e",
            "fpsp_fline",
            "-o",
            elf,
            *objects,
        ],
        check=True,
    )
    binary = output / "fpsp.bin"
    subprocess.run(
        [prefix + "objcopy", "-O", "binary", "--only-section=.text", elf, binary],
        check=True,
    )
    data = binary.read_bytes()
    relocations = []
    listing = subprocess.check_output([prefix + "readelf", "-rW", elf], text=True)
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) < 4 or not fields[2].startswith("R_68K_"):
            continue
        kind = fields[2]
        if kind in ("R_68K_PC32", "R_68K_PC16", "R_68K_PC8"):
            continue
        offset, target = int(fields[0], 16) - base, int(fields[3], 16)
        if kind != "R_68K_32" or not 0 <= offset <= len(data) - 4 or not base <= target < base + len(data):
            raise ValueError(f"Unsupported FPSP relocation: {line}")
        relocations.append(offset)
    symbols = {}
    for line in subprocess.check_output([prefix + "nm", "-n", elf], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] == "T":
            symbols[fields[2]] = int(fields[0], 16) - base
    names = {name.upper(): "fpsp_" + name for name in ("fline", "bsun", "unfl", "operr", "ovfl", "snan", "unsupp")}
    names.update(
        {
            "REAL_" + name.upper(): "real_" + name
            for name in (
                "bsun",
                "fline",
                "inex",
                "operr",
                "ovfl",
                "snan",
                "trace",
                "unfl",
            )
        }
    )
    names["FMT_ERROR"] = "fpsp_fmt_error"
    header = ["/* Generated Motorola FPSP guest image; do not edit. */"]
    for name, symbol in names.items():
        offset = symbols[symbol]
        if not 0 <= offset < len(data):
            raise ValueError(f"Invalid entry point: {name}")
        header.append(f"#define FPSP_{name} {offset:#x}")
    header.append("static const unsigned char fpsp_image[] = {")
    for i in range(0, len(data), 16):
        header.append("  " + ", ".join(f"0x{b:02x}" for b in data[i : i + 16]) + ",")
    header.append("};\nstatic const unsigned fpsp_relocations[] = {")
    header.extend(f"  {offset:#x}," for offset in relocations)
    header.append("};\n")
    temporary = output / "fpsp-image.h.tmp"
    temporary.write_text("\n".join(header), encoding="utf-8")
    temporary.replace(output / "fpsp-image.h")
    print(f"Built {len(data)} FPSP bytes with {len(relocations)} absolute relocations")


if __name__ == "__main__":
    main()
