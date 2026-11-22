#!/usr/bin/env python3
# +===================================================================
#   File:      gen-toolbox.py
#
#   Summary:   Build native/PPC import-name to Toolbox dispatcher index mappings.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in synppc/tools; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Build native/PPC import-name to Toolbox dispatcher index mappings."""
import pathlib, re, sys

source = pathlib.Path(sys.argv[1]).read_text()
table = source.split("toolstuff_t toolstuff[0x400] = {", 1)[1].split("};", 1)[0]


table = re.sub(
    r"#if defined\(CYGWIN32\) && defined\(SDL\) && defined\(SYN68K\)\n(.*?)#else\n.*?#endif",
    lambda m: m[1],
    table,
    flags=re.S,
)
if re.search(r"^\s*#", table, re.M):
    raise SystemExit("Unexpected Toolbox table conditional")
# Accept both spaced C initializers and compact C++-style braced lists.
rows = re.findall(r"^\s*\{\s*\{\s*([^,{}]+?)\s*,\s*([^{}]+?)\s*\}\s*,", table, re.M)
if len(rows) != 1024:
    raise SystemExit(f"Expected 1024 Toolbox entries, found {len(rows)}")
lines = ["/* Generated from emutraptables.c; do not edit. */"]
for index, (function, magic) in enumerate(rows):
    if function.startswith("C_") and magic.strip().startswith("PTOC_"):
        lines.append('  {"%s", %d},' % (function[2:], index))
result = chr(10).join(lines) + chr(10)
p = pathlib.Path(sys.argv[2])
p.parent.mkdir(parents=True, exist_ok=True)
if not p.exists() or p.read_text() != result:
    p.write_bytes(result.encode())
