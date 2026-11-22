#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_corefoundation_url_creation_probe.py
#
#   Summary:   Original URL creation profile wrapper.
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

"""Original URL creation profile wrapper."""
import sys
from pathlib import Path
from make_ppc_carbon_url_creation_probe import make_probe

if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), library="CoreFoundationLib")
