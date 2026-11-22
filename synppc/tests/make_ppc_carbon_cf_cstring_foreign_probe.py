#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_cstring_foreign_probe.py
#
#   Summary:   Original C-string conversion fixture for foreign CoreFoundation layouts.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original C-string conversion fixture for foreign CoreFoundation layouts."""
import sys
from pathlib import Path

from make_ppc_carbon_cf_pascal_probe import make_pascal_probe


if __name__ == "__main__":
    make_pascal_probe(Path(sys.argv[1]), mode="foreign", c_string=True)
