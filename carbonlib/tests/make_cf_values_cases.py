# +===================================================================
#   File:      make_cf_values_cases.py
#
#   Summary:   Generate signature and offset tables from CarbonLib cohort manifests.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in carbonlib/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Generate signature and offset tables from CarbonLib cohort manifests."""
import json
import sys
from pathlib import Path


def generate(source, destination, table="values_cases"):
    if not table.isidentifier():
        raise ValueError("Invalid C case-table identifier")
    rows = json.loads(source.read_text())["entries"]
    lines = [
        "/* Generated from the collection/value cohort manifest. */",
        "#ifndef CF_VALUES_CASE_TYPE",
        "#define CF_VALUES_CASE_TYPE",
        "typedef struct { const char* name; unsigned offset, count, returns_value; } values_case;",
        "#endif",
        f"static const values_case {table}[] = {{",
    ]
    for row in rows:
        lines.append(
            '{"%s", %#x, %d, %d},'
            % (row["name"], row["code_offset"], row["argument_count"], row["return_type"] == "uint32")
        )
    lines += ["};", ""]
    destination.write_text("\n".join(lines), newline="\n")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3] if len(sys.argv) > 3 else "values_cases")
