#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_unichar_foreign_probe.py
#
#   Summary:   Original PPC probe for companion-only fallback on an unknown CF layout.
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

"""Original PPC probe for companion-only fallback on an unknown CF layout."""
import sys
from pathlib import Path

from make_ppc_unichar_probe import make_unichar_probe


if __name__ == "__main__":
    make_unichar_probe(Path(sys.argv[1]), mode="foreign")
