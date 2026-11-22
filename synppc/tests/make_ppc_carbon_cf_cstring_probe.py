#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_cf_cstring_probe.py
#
#   Summary:   Original CF C-string conversion ABI and original-PPC differential fixtures.
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

"""Original CF C-string conversion ABI and original-PPC differential fixtures."""
import argparse
from pathlib import Path

from make_ppc_carbon_cf_pascal_probe import make_pascal_probe


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("mode", nargs="?", default="valid")
    parser.add_argument("--real", action="store_true")
    parser.add_argument("--library", choices=("CarbonLib", "CoreFoundationLib"), default="CarbonLib")
    args = parser.parse_args()
    make_pascal_probe(args.destination, args.library, args.real, args.mode, c_string=True)
