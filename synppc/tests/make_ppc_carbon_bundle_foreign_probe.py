#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_bundle_foreign_probe.py
#
#   Summary:   Original PPC bundle-symbol probe for unknown CoreFoundation profiles.
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

"""Original PPC bundle-symbol probe for unknown CoreFoundation profiles."""
import sys
from pathlib import Path

from make_ppc_carbon_bundle_probe import make_bundle_probe


if __name__ == "__main__":
    make_bundle_probe(Path(sys.argv[1]), mode="foreign")
