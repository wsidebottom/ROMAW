#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_allocator_probe.py
#
#   Summary:   Original re-exported allocator-reference fixture through CoreFoundationLib.
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

"""Original re-exported allocator-reference fixture through CoreFoundationLib."""
import sys
from pathlib import Path

from make_ppc_carbon_allocator_probe import make_allocator_probe

if __name__ == "__main__":
    make_allocator_probe(Path(sys.argv[1]), "CoreFoundationLib")
