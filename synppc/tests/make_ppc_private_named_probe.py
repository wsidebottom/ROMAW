#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_private_named_probe.py
#
#   Summary:   CFM private named connection fixture.
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

"""CFM private named connection fixture."""
import sys
from pathlib import Path
from make_ppc_private_copy_probe import make_private_probe

if __name__ == "__main__":
    make_private_probe(Path(sys.argv[1]), "named", sys.argv[2] if len(sys.argv) > 2 else "valid")
