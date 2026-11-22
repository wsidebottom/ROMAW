# +===================================================================
#   File:      make_cf_services_cases.py
#
#   Summary:   Generate service cases for the shared host harness and binding initializers for the service registry.
#
#   Classes:   None.
#
#   Functions: generate.
#
#   Origin:    Original test-table generator for the native service cohort manifest.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

"""Generate shared harness cases or the service registry's binding initializers."""

import argparse
import json
from pathlib import Path


def generate(source, destination, bindings):
    rows = json.loads(source.read_text())["entries"]
    if bindings:
        lines = []
        for row in rows:
            flags = "ROMLIB_CARBON_PPC_ONLY"
            offset = row["main_import"]
            if row["companion_only"]:
                flags += " | ROMLIB_CARBON_COMPANION_ONLY"
                offset = row["code_offset"]
            lines.append(
                '{"%s", %s, ROMLIB_CARBON_PPC_FRAME, %#x, "CoreFoundationLib", %s},'
                % (row["name"], row["function"], offset, flags)
            )
        destination.write_text("\n".join(lines) + "\n", newline="\n")
        return
    lines = [
        "/* Generated from the service cohort manifest. */",
        "typedef struct { const char* name; unsigned offset; ROMlib_carbon_frame_entry entry; } service_case;",
        "static const service_case service_cases[] = {",
    ]
    for row in rows:
        lines.append('{"%s", %#x, %s},' % (row["name"], row["code_offset"], row["function"]))
    lines.extend(["};", ""])
    destination.write_text("\n".join(lines), newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--bindings", action="store_true")
    args = parser.parse_args()
    generate(args.source, args.destination, args.bindings)
