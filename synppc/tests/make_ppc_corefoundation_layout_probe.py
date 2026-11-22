#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_layout_probe.py
#
#   Summary:   Original guest-layout accessor probe through direct CoreFoundationLib imports.
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

"""Original guest-layout accessor probe through direct CoreFoundationLib imports."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_carbon_cf_probe import make_layout_probe

if __name__ == "__main__":
    make_layout_probe(Path(sys.argv[1]), "CoreFoundationLib")
