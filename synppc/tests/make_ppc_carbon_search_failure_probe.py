#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_carbon_search_failure_probe.py
#
#   Summary:   Original native CFArray comparator validation and failure fixtures.
#
#   Classes:   None declared.
#
#   Functions: make_search_failure_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original native CFArray comparator validation and failure fixtures."""
import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe
from make_ppc_carbon_native_probe import write_carbon_fallback


def make_search_failure_probe(destination, mode="invalid"):
    if mode not in ("invalid", "failure"):
        raise ValueError(mode)
    write_carbon_fallback(destination)
    imports = [("CarbonLib", "CFArrayBSearchValues", 2), ("InterfaceLib", "ROMAWCarbonSearchFailure", 2)]
    p = Probe(imports)
    struct.pack_into(">5I", p.data, 132, 0, 3, 0, 1, 2)
    if mode == "failure":
        callback = Probe(imports)
        callback.call("ROMAWCarbonSearchFailure")
        callback.emit(0x83E10034, 0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
        p.routines.append((640, callback.code))
    p.emit(0x387E0080, 0x38800000, 0x38A00003, 0x38C00001, 0x38FE0280, 0x3900FFFF)
    p.call("CFArrayBSearchValues")
    p.emit(0)
    p.finish(destination, "PPC Carbon Search Failure")


if __name__ == "__main__":
    make_search_failure_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "invalid")
