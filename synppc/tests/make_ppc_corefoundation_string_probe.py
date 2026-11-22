#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_string_probe.py
#
#   Summary:   Original CoreFoundationLib character-reader probe.
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

"""Original CoreFoundationLib character-reader probe."""
import sys
from pathlib import Path

from make_ppc_carbon_cf_string_probe import make_string_probe


if __name__ == "__main__":
    make_string_probe(Path(sys.argv[1]), library="CoreFoundationLib")
