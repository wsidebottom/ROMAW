#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_encoding_foreign_probe.py
#
#   Summary:   Original foreign-profile byte-extraction and fastest-encoding fixture.
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

"""Original foreign-profile byte-extraction and fastest-encoding fixture."""
import sys
from pathlib import Path

from make_ppc_carbon_cf_encoding_probe import make_encoding_probe


if __name__ == "__main__":
    make_encoding_probe(Path(sys.argv[1]), mode="foreign")
