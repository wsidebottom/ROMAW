#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_cfstrings_probe.py
#
#   Summary:   CoreFoundation namespace wrapper for the native string ABI profile.
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

"""CoreFoundation namespace wrapper for the native string ABI profile."""
import sys
from pathlib import Path
from make_ppc_carbon_cfstrings_probe import make_probe

if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), library="CoreFoundationLib")
