#!/usr/bin/env python3
# +===================================================================
#   File:      ppc-native-memory.py
#
#   Summary:   Compile and verify a relocatable native 750 MMU and cache kernel.
#
#   Classes:   None.
#
#   Functions: main; imports extract_elf and extract_image from the shared verifier.
#
#   Origin:    Original ARDI Executor native PowerPC memory lowering.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


# Both CPU backends use the same relocation and closed-image checks.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from native_code_image import extract_elf, extract_image


ROOTS = ("read", "write", "reserve", "conditional", "external", "translate", "cache", "sync", "snoop", "msr", "spr")


def main():
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / "native-memory"
    work.mkdir(exist_ok=True)
    macros = subprocess.check_output(compiler + ["-dM", "-E", "-x", "c++", "-"], input="", text=True)
    windows = "#define _WIN32 " in macros
    if "#define __x86_64__ " not in macros:
        raise ValueError("native memory templates require an x64 compiler")

    def tool(name):
        return subprocess.check_output(compiler + ["-print-prog-name=" + name], text=True).strip()

    flags = [
        "-std=gnu++11",
        "-O3",
        "-flto",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-strict-aliasing",
        "-finline-limit=100000",
        "-minline-all-stringops",
        "-mgeneral-regs-only",
        "-fno-tree-vectorize",
        "-fno-tree-slp-vectorize",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-fno-jump-tables",
        "-fno-tree-switch-conversion",
        "-fno-ipa-icf",
        "-fno-pic",
        "-fno-pie",
        "-fcf-protection=branch",
        "-U_FORTIFY_SOURCE",
        "-D_FORTIFY_SOURCE=0",
        "-I" + str(root / "include"),
    ]
    if not windows:
        flags.append("-mcmodel=large")
    objects = []
    for name in ("mmu750", "cache750", "l2cache750", "bus750", "system750", "ppc-native-memory"):
        obj = work / (name + ".o")
        subprocess.run(compiler + flags + ["-c", str(root / "src" / (name + ".cpp")), "-o", str(obj)], check=True)
        objects.append(str(obj))
    combined = work / "combined.o"
    partial_flags = ["-Wl,--disable-runtime-pseudo-reloc"] if windows else []
    subprocess.run(
        compiler
        + flags
        + partial_flags
        + ["-r", "-nostdlib", "-flinker-output=nolto-rel", *objects, "-o", str(combined)],
        check=True,
    )
    image = work / "linked.image"
    # Only reachable sections must be closed: unrelated clock routines in the
    # shared source can contain compiler division helpers. The final linker
    # rejects every unresolved live reference; no import libraries are supplied.
    link = [
        tool("ld"),
        "-m",
        "i386pep",
        "--image-base=0",
        "--section-alignment=16",
        "--file-alignment=16",
        "--no-insert-timestamp",
        "--gc-sections",
        "--no-undefined",
        "--disable-runtime-pseudo-reloc",
        "--disable-auto-import",
        "--entry=" + "native_memory_read",
    ]
    if not windows:
        link = [
            tool("ld"),
            "-m",
            "elf_x86_64",
            "-Ttext-segment=0",
            "--emit-relocs",
            "--gc-sections",
            "--no-undefined",
            "-z",
            "noexecstack",
            "--entry=native_memory_read",
        ]
    link += ["--undefined=" + "native_memory_" + name for name in ROOTS]
    subprocess.run(link + [str(combined), "-o", str(image)], check=True)
    symbols = {}
    for line in subprocess.check_output([tool("nm"), "-n", str(image)], text=True).splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+\w\s+(\S+)", line)
        if match:
            symbols[match[2]] = int(match[1], 16)
    code, fixups, entries = (extract_image if windows else extract_elf)(
        image.read_bytes(), symbols, tuple("native_memory_" + name for name in ROOTS)
    )
    lines = [
        "/* Generated closed native 750 memory kernel; do not edit. */",
        "static const unsigned char native_memory_code[] = {",
    ]
    for at in range(0, len(code), 16):
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in code[at : at + 16]) + ",")
    lines += ["};", "static const ppc_native_memory_relocation native_memory_relocations[] = {"]
    # A sentinel keeps the array standard C++ even for position-independent code.
    lines += [f"    {{{at}, {target}}}," for at, target in fixups] + ["    {0, 0}", "};"]
    lines.append(f"static const unsigned native_memory_relocation_count = {len(fixups)};")
    for name, entry in zip(ROOTS, entries):
        lines.append(f"static const unsigned native_memory_{name}_offset = {entry};")
    text = "\n".join(lines) + "\n"
    if not output.exists() or output.read_text() != text:
        temporary = output.with_suffix(".tmp")
        temporary.write_text(text, newline="\n")
        os.replace(temporary, output)
    print(
        f"Native 750 memory: {len(code)} bytes, {len(fixups)} verified pointer relocations, {len(entries)} roots, no imports"
    )


if __name__ == "__main__":
    main()
