#!/usr/bin/env python3
# +===================================================================
#   File:      build-native040.py
#
#   Summary:   Compile and verify closed native 68040 floating and MMU kernels.
#
#   Classes:   None.
#
#   Functions: main; imports extract_elf and extract_image from the shared verifier.
#
#   Origin:    ARDI Executor 68040 template build, sharing the native code-image verifier.
#
#   Copyright and Legal notices: Applicable license: syn68k/COPYING.
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


ROOTS = (
    "fadd",
    "fsub",
    "fmul",
    "fdiv",
    "fsqrt",
    "fmove",
    "fabs",
    "fneg",
    "fsgldiv",
    "fsglmul",
    "fcmp",
    "ftst",
    "read",
    "write",
    "write_rmw",
    "translate",
    "ptest",
    "flush",
    "control",
    "reset",
    "ea",
    "from_i32",
    "from_f32",
    "from_f64",
    "to_i32",
    "to_i16",
    "to_i8",
    "to_f32",
    "to_f64",
    "bus_read",
    "bus_write",
    "bus_write_rmw",
    "movec_read",
    "movec_write",
    "load_fprs",
    "store_fprs",
    "flat_read",
    "flat_write",
    "flat_span",
    "span",
    "stop",
    "cache_access",
    "cache_maintain",
    "memory_cache_maintain",
    "bus_move16",
    "cache_snoop",
    "bus_snoop",
    "rte",
    "restore_writeback",
    "movem",
)


def main():
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    configuration = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else root / "obj/src/include"
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / "native040"
    work.mkdir(exist_ok=True)
    macros = subprocess.check_output(compiler + ["-dM", "-E", "-x", "c", "-"], input="", text=True)
    windows = "#define _WIN32 " in macros
    if "#define __x86_64__ " not in macros:
        raise ValueError("native 68040 templates require an x64 compiler")

    def tool(name):
        return subprocess.check_output(compiler + ["-print-prog-name=" + name], text=True).strip()

    flags = [
        "-std=gnu11",
        "-O3",
        "-flto",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-strict-aliasing",
        "-fwrapv",
        "-finline-limit=100000",
        "-minline-all-stringops",
        "-mgeneral-regs-only",
        "-fno-tree-vectorize",
        "-fno-tree-slp-vectorize",
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
        "-I" + str(root / "src/runtime/include"),
        "-I" + str(configuration),
        "-I" + str(root / "src/include"),
        "-DRUNTIME",
        "-DNONDIRECT",
        "-DSYN68K_NATIVE040_TEMPLATE=1",
    ]
    if not windows:
        flags.append("-mcmodel=large")
    objects = []
    for name in (
        "mmu040",
        "mmu040-memory",
        "cache040",
        "native040-cache",
        "native040-mmu",
        "native040-fpu",
        "native040-ea",
        "native040-control",
        "native040-registers",
        "native040-flat",
    ):
        obj = work / (name + ".o")
        # Arithmetic and EA each contain their complete context-bound bodies.
        # Keep their fixed R12 context local to each TU: GCC LTO partitions can
        # otherwise place the register declaration after unrelated functions.
        compile_flags = (
            [flag for flag in flags if flag != "-flto"] if name in ("native040-fpu", "native040-ea") else flags
        )
        subprocess.run(
            compiler + compile_flags + ["-c", str(root / "src/runtime" / (name + ".c")), "-o", str(obj)], check=True
        )
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
        "--entry=" + "native040_read",
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
            "--entry=native040_read",
        ]
    link += ["--undefined=" + "native040_" + name for name in ROOTS]
    subprocess.run(link + [str(combined), "-o", str(image)], check=True)
    symbols = {}
    for line in subprocess.check_output([tool("nm"), "-n", str(image)], text=True).splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+\w\s+(\S+)", line)
        if match:
            symbols[match[2]] = int(match[1], 16)
    code, fixups, entries = (extract_image if windows else extract_elf)(
        image.read_bytes(), symbols, tuple("native040_" + name for name in ROOTS)
    )
    lines = [
        "/* Generated closed native 68040 arithmetic/MMU kernel; do not edit. */",
        "static const unsigned char native040_code[] = {",
    ]
    for at in range(0, len(code), 16):
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in code[at : at + 16]) + ",")
    lines += ["};", "static const Syn68kNative040Relocation native040_relocations[] = {"]
    # A sentinel keeps the array standard C++ even for position-independent code.
    lines += [f"    {{{at}, {target}}}," for at, target in sorted(fixups)] + ["    {0, 0}", "};"]
    lines.append(f"static const unsigned native040_relocation_count = {len(fixups)};")
    for name, entry in zip(ROOTS, entries):
        lines.append(f"static const unsigned native040_{name}_offset = {entry};")
    text = "\n".join(lines) + "\n"
    if not output.exists() or output.read_text() != text:
        temporary = output.with_suffix(".tmp")
        temporary.write_text(text, newline="\n")
        os.replace(temporary, output)
    print(
        f"Native 68040: {len(code)} bytes, {len(fixups)} verified pointer relocations, {len(entries)} roots, no imports"
    )


if __name__ == "__main__":
    main()
