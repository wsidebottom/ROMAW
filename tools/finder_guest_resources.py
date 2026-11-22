#!/usr/bin/env python3
# +===================================================================
#   File:      finder_guest_resources.py
#
#   Summary:   Encode minimal guest ABI dispatch veneers for native Finder procedures.
#
#   Classes:   None.
#
#   Functions: finder_guest_payload
#
#   Origin:    ROMAW native Finder procedure integration.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

import struct


FINDER_HANDLERS = {
    (b"CDEF", 128): (11, "ROMlib_finder_definition(0)"),
    (b"WDEF", 128): (12, "ROMlib_finder_definition(1)"),
    (b"gest", 128): (13, "ROMlib_finder_definition(2)"),
    (b"hcsl", 0): (14, "ROMlib_finder_definition(3)"),
}


def finder_guest_payload(kind, identity):
    """Keep code-relative state and the hcsl handler's original guest offset."""
    slot, _ = FINDER_HANDLERS[kind, identity]

    def veneer(offset, target_slot):
        # lea resource_base(pc),a1; move.l $58.w,a0;
        # move.l target_slot*4(a0),a0; jmp (a0).
        return (
            bytes.fromhex("43fa")
            + struct.pack(">h", -offset - 2)
            + bytes.fromhex("207800582068")
            + struct.pack(">H", target_slot * 4)
            + bytes.fromhex("4ed0")
        )

    payload = veneer(0, slot)
    if kind == b"gest":
        return payload.ljust(132, b"\0")  # cached Handle at +0x80
    if kind == b"hcsl":
        return payload.ljust(26, b"\0") + veneer(26, 15) + b"\0\0"  # response at +0x16
    return payload
