#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_cf_values_probe.py
#
#   Summary:   Exercise foreign-profile fallback through the CoreFoundation namespace.
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

"""Exercise foreign-profile fallback through the CoreFoundation namespace."""
import sys
from pathlib import Path
from make_ppc_carbon_cf_values_probe import make_foreign_probe

if __name__ == "__main__":
    make_foreign_probe(Path(sys.argv[1]), "CoreFoundationLib")
