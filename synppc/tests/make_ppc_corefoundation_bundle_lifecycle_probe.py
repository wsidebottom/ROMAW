#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_bundle_lifecycle_probe.py
#
#   Summary:   Bundle lifecycle import/profile fixture.
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

"""Bundle lifecycle import/profile fixture."""
import sys
from pathlib import Path
from make_ppc_carbon_bundle_lifecycle_probe import make_probe

if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), library="CoreFoundationLib", mode="valid")
