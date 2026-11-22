#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_stdclib_pascal_probe.py
#
#   Summary:   Original PPC Pascal String Utilities probe through direct StdCLib imports.
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

"""Original PPC Pascal String Utilities probe through direct StdCLib imports."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_carbon_pascal_probe import make_pascal_probe

if __name__ == "__main__":
    make_pascal_probe(Path(sys.argv[1]), native_library="StdCLib")
