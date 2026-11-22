#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_bundle_state_foreign_probe.py
#
#   Summary:   Bundle state batch import and profile fixture.
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

"""Bundle state batch import and profile fixture."""
import sys
from pathlib import Path
from make_ppc_carbon_bundle_state_probe import make_state_probe

if __name__ == "__main__":
    make_state_probe(Path(sys.argv[1]), library="CarbonLib", mode="foreign")
