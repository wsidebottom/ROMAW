#!/usr/bin/env python3
# +===================================================================
#   File:      ppc-native-fused.py
#
#   Summary:   Compile and verify closed integer templates for emitted PPC fused arithmetic.
#
#   Classes:   None.
#
#   Functions: main.
#
#   Origin:    Original ARDI Executor native PowerPC instruction lowering.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1])
    compiler = shlex.split(sys.argv[2])
    macros = subprocess.check_output(compiler + ["-dM", "-E", "-x", "c++", "-"], input="", text=True)
    if "#define __x86_64__ " not in macros:
        raise ValueError("native fused templates require an x64 compiler")
    output.parent.mkdir(parents=True, exist_ok=True)
    obj = output.with_suffix(".o")
    flags = [
        "-std=gnu++11",
        "-O3",
        "-fno-exceptions",
        "-fno-rtti",
        "-minline-all-stringops",
        "-mgeneral-regs-only",
        "-fno-tree-vectorize",
        "-fno-tree-slp-vectorize",
        "-fno-jump-tables",
        "-fno-tree-switch-conversion",
        "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-fno-pic",
        "-fno-pie",
        "-fno-ipa-icf",
        "-fcf-protection=branch",
    ]
    subprocess.run(compiler + flags + ["-c", str(root / "src/ppc-native-fused.cpp"), "-o", str(obj)], check=True)
    objdump = subprocess.check_output(compiler + ["-print-prog-name=objdump"], text=True).strip()
    objcopy = subprocess.check_output(compiler + ["-print-prog-name=objcopy"], text=True).strip()
    lines = ["/* Generated closed integer templates; do not edit. */"]
    for precision in "sd":
        for operation in range(28, 32):
            name = f"native_fused_{precision}{operation}"
            section = ".text." + name
            relocations = subprocess.check_output([objdump, "-r", "-j", section, str(obj)], text=True)
            assembly = subprocess.check_output([objdump, "-d", "-j", section, str(obj)], text=True)
            if "RELOCATION RECORDS" in relocations or re.search(r"\bcall\w*\s", assembly):
                raise RuntimeError(f"{name} contains an external reference or call")
            binary = output.with_suffix(".bin")
            subprocess.run([objcopy, "--only-section=" + section, "-O", "binary", str(obj), str(binary)], check=True)
            code = binary.read_bytes()
            if not 16 <= len(code) <= 16384:
                raise RuntimeError(f"{name} has invalid template size {len(code)}")
            lines.append(f"static const unsigned char {name}[] = {{")
            for index in range(0, len(code), 16):
                lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in code[index : index + 16]) + ",")
            lines.append("};")
            print(f"{name}: {len(code)} closed native bytes, no calls or relocations")
    content = "\n".join(lines) + "\n"
    if not output.exists() or output.read_text() != content:
        temporary = output.with_suffix(".tmp")
        temporary.write_text(content, newline="\n")
        os.replace(temporary, output)


if __name__ == "__main__":
    main()
