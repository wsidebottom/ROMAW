#!/usr/bin/env python3
# +===================================================================
#   File:      link-a7-reference.py
#
#   Summary:   Check LINK A7 source capture against an independent m68040 emulator.
#
#   Classes:   None.
#
#   Functions: Script entry point.
#
#   Origin:    Original ROMAW regression driver; no emulator implementation imported.
#
#   Copyright and Legal notices: Applicable repository license: syn68k/COPYING.
# ===================================================================+

"""Run the assembly reference with QEMU; this is not physical CPU evidence."""

from pathlib import Path
import struct
import subprocess
import tempfile

source = Path(__file__).with_suffix(".s").resolve()
with tempfile.TemporaryDirectory(prefix="romaw-link-reference-") as directory:
    output = Path(directory)
    subprocess.run(
        ["m68k-linux-gnu-as", "-m68040", str(source), "-o", str(output / "reference.o")], check=True, timeout=30
    )
    subprocess.run(
        ["m68k-linux-gnu-ld", str(output / "reference.o"), "-o", str(output / "reference")], check=True, timeout=30
    )
    result = subprocess.run(
        ["qemu-m68k", "-cpu", "m68040", str(output / "reference")], check=True, capture_output=True, timeout=30
    )
    actual = struct.unpack(">4i", result.stdout)
    if actual != (0, -20, 0, -20):
        raise SystemExit(f"LINK A7 reference mismatch: {actual!r}")
    print("LINK.W/L A7 independent QEMU reference passed: original SP saved; final SP = original SP - 20")
