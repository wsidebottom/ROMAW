#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_string_pointer_probe.py
#
#   Summary:   Original CoreFoundationLib borrowed string pointer probe.
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

"""Original CoreFoundationLib borrowed string pointer probe."""
import sys
from pathlib import Path

from make_ppc_carbon_string_pointer_probe import make_pointer_probe


if __name__ == "__main__":
    make_pointer_probe(Path(sys.argv[1]), library="CoreFoundationLib")
