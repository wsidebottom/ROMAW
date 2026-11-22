#!/usr/bin/env python3
# +===================================================================
#   File:      make_classic_access_cases.py
#
#   Summary:   Emit the native test table from the classic ABI reference cases.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in packages/carbonlib/tests; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Emit the native test table from the classic ABI reference cases."""
import json
import sys
from pathlib import Path

KINDS = (
    "lowmem_lha",
    "lowmem_lbz",
    "lowmem_lwz",
    "lowmem_stb",
    "lowmem_sth",
    "lowmem_stw",
    "lowmem_address",
    "lowmem_copy_in",
    "lowmem_rgb_out",
    "lowmem_rgb_in",
    "lowmem_index_get",
    "lowmem_index_set",
    "record_get",
    "record_set",
    "record_copy_in",
    "record_copy_out",
    "record_nonzero",
    "record_popup_get",
    "record_popup_set",
    "record_direct_get",
    "record_focus",
    "record_identity",
    "record_title",
)


def generate(source, destination):
    rows = json.loads(source.read_text())
    lines = [
        "/* Generated test data; edit classic_access_cases.json or its table generator. */",
        "enum {" + ", ".join(KINDS) + "};",
        "typedef struct { const char* name; unsigned kind, address, width, sign, guard, code; } classic_case;",
        "static const classic_case classic_cases[] = {",
    ]
    for row in rows:
        kind = row["family"] + "_" + row["kind"]
        if kind not in KINDS:
            raise ValueError(kind)
        width = row.get(
            "width", row.get("size", {"lha": 2, "lbz": 1, "lwz": 4, "stb": 1, "sth": 2, "stw": 4}.get(row["kind"], 4))
        )
        signed = row.get("signed", row["kind"] == "lha")
        address = row.get("address", row.get("offset", 0))
        lines.append(
            '{"%s", %s, %#x, %d, %d, %d, %#x},'
            % (row["name"], kind, address, width, signed, row.get("guard", False), row["code_offset"])
        )
    lines += ["};", ""]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), newline="\n")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), Path(sys.argv[2]))
