#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_callback_failure_probe.py
#
#   Summary:   A failing PPC event callback must unwind a long native WaitNextEvent call.
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

"""A failing PPC event callback must unwind a long native WaitNextEvent call."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination, mode="valid"):
    if mode not in ("valid", "event-avail"):
        raise ValueError(mode)
    names = [
        "NewRoutineDescriptor",
        "SetToolTrapAddress",
        "WaitNextEvent",
        "ROMAWCallbackFailure",
    ]
    code = []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + 4 * index, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    emit(
        0x9421FFC0,
        0x7C0802A6,
        0x90010044,
        0x93C10038,
        0x7C5E1378,
        0x387E0040,
        0x38800390,
        0x38A00001,
    )
    call(0)
    emit(0x38800000 | (0xA971 if mode == "event-avail" else 0xA970))
    call(1)
    emit(0x3860FFFF, 0x389E0060, 0x3CA07FFF, 0x60A5FFFF, 0x38C00000)
    call(2)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    callback = len(code) * 4
    emit(0x81820014, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421, 0x4E800020)
    data = bytearray(128)
    struct.pack_into(">II", data, 64, callback, 0)
    app = pef(
        code,
        data,
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A03, 0xA000, 64, 0x4600],
    )
    write_fragment(destination, "PPC Callback Failure Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
