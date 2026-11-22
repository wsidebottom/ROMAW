#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_service_failure_probe.py
#
#   Summary:   Original PPC fixtures for native-entry and invalid-service diagnostics.
#
#   Classes:   None declared.
#
#   Functions: make_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC fixtures for native-entry and invalid-service diagnostics."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination, mode="pointer"):
    if mode not in ("pointer", "service-id"):
        raise ValueError(mode)
    code = [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378]
    if mode == "pointer":
        # The required copy crosses the 32-bit guest address limit. The bridge
        # must reject it before calling the host memcpy, with its API identified.
        code += [0x3860FFFF, 0x38802000, 0x38A00004, 0x819E0008, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421]
    else:
        # Model corrupt/stale service code without introducing an Apple binary.
        code += [0x1800FFFF]
    code += [0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020]
    image = pef(code, bytes(32), imports=[("StdCLib", "memcpy", 2)], main=0, reloc=[0x4600, 0x4A00])
    write_fragment(destination, "PPC Service Failure Probe", image, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "pointer")
