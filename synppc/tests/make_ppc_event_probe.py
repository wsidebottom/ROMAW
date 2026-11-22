# +===================================================================
#   File:      make_ppc_event_probe.py
#
#   Summary:   Original PPC PostEvent queue, argument-width and masked-event checks.
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

"""Original PPC PostEvent queue, argument-width and masked-event checks."""

from pathlib import Path
import sys

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination):
    names = [
        "PostEvent",
        "EventAvail",
        "GetNextEvent",
        "SetEventMask",
        "FlushEvents",
        "MaxApplZone",
        "MoreMasters",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0008 + names.index(name) * 4,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("MaxApplZone")
    call("MoreMasters")
    emit(0x3860FFFF)
    call("SetEventMask")
    emit(0x38601000, 0x389E0080)
    call("EventAvail")
    check(0)
    emit(0x3860000C, 0x3880002A)
    call("PostEvent")
    check(0)
    emit(0x38601000, 0x389E0080)
    call("EventAvail")
    check(1)
    emit(0x38601000, 0x38800000)
    call("FlushEvents")
    emit(0x38601000, 0x389E0080)
    call("EventAvail")
    check(0)
    emit(0x3860000C, 0x3C8089AB, 0x6084CDEF)
    call("PostEvent")
    check(0)
    for name in ("EventAvail", "GetNextEvent"):
        emit(0x38601000, 0x389E0080)
        call(name)
        check(1)
        emit(0xA07E0080)
        check(12)
        # Packed EventRecord.message lies at offset 2, not a host alignment.
        emit(0x807E0082, 0x3C8089AB, 0x6084CDEF, 0x7C632050)
        check(0)
    emit(0x38601000, 0x389E0080)
    call("EventAvail")
    check(0)
    for invalid in (-32768, -1, 16, 23, 31, 32, 32767):
        emit(0x38600000 | (invalid & 65535), 0x38800000)
        call("PostEvent")
        check(1)
    emit(0x3860EFFF)
    call("SetEventMask")
    emit(0x3860000C, 0x3880002A)
    call("PostEvent")
    check(1)
    emit(0x38601000, 0x389E0080)
    call("EventAvail")
    check(0)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for at in failures:
        code[at] |= (bad - at) * 4
    app = pef(
        code,
        bytes(160),
        imports=[("InterfaceLib", name, 2) for name in names],
        main=0,
        reloc=[0x4600, 0x4A00 + len(names) - 1],
    )
    write_fragment(destination, "PPC Event Probe", app, 1)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
