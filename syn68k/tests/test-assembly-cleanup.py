#!/usr/bin/env python3
# +===================================================================
#   File:      test-assembly-cleanup.py
#
#   Summary:   Preserve debug payloads while stripping synthetic opcode function wrappers.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in syn68k/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              syn68k/COPYING.
# ===================================================================+

"""Preserve debug payloads while stripping synthetic opcode function wrappers."""
from pathlib import Path
import subprocess

cleanup = Path(__file__).resolve().parents[1] / "src" / "runtime" / "i486-cleanup.pl"
for section in (
    ".debug_info",
    ".debug_str",
    ".debug_line_str",
    ".zdebug_info",
    ".stabstr",
):
    debug = (
        f'\t.section {section},"dr"\n'
        '\t.ascii "s68k_handle_opcode_0x1234\\0"\n'
        "\t.long _s68k_handle_opcode_0x1234\n"
        '\t.ascii "_S68K_DONE_WITH\\0"\n'
    )
    # Each wrapper still needs removal after returning from a debug section.
    wrapper = (
        ".text\n.globl _s68k_handle_opcode_0x1234\n"
        "_s68k_handle_opcode_0x1234:\n\tpushl %ebp\n#APP\n"
        "_S68K_HANDLE_0x1234:\n\tmovl %eax,%ebx\n"
        "_S68K_DONE_WITH_0x1234:\n\tpopl %ebp\n\tret\n"
    )
    result = subprocess.run(
        ["perl", str(cleanup)],
        input=debug + wrapper + debug,
        text=True,
        capture_output=True,
        check=True,
    ).stdout
    assert result.count(debug) == 2, (section, result)
    assert "\tpushl" not in result and "\tpopl" not in result and "\tret" not in result
    assert "\tmovl %eax,%ebx\n" in result and "_S68K_HANDLE_0x1234:\n" in result
print("PASS: DWARF sections preserved and opcode wrappers still removed")
