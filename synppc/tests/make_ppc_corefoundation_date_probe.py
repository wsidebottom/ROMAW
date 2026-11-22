#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_date_probe.py
#
#   Summary:   Original PPC CoreFoundationLib floating-result ABI fixture.
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

"""Original PPC CoreFoundationLib floating-result ABI fixture."""
import sys
from pathlib import Path

from make_ppc_carbon_date_probe import make_date_probe

if __name__ == "__main__":
    make_date_probe(Path(sys.argv[1]), "CoreFoundationLib")
