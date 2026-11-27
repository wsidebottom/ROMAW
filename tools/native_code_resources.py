#!/usr/bin/env python3
# +===================================================================
#   File:      native_code_resources.py
#
#   Summary:   Encode and audit C-backed System resource entry veneers.
#
#   Classes:   None.
#
#   Functions: intl1_localization_payload, validate_intl1_localization, audit_native_resources,
#              validate_alert_procedures.
#
#   Origin:    ROMAW Intl1Rec layout and the original bundled no-op localRtn.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

import struct

from finder_guest_resources import FINDER_HANDLERS, finder_guest_payload

INTL1_LOCALIZATION_OFFSET = 330
INTL1_LOCALIZATION_SLOT = 16
EXECUTABLE_TYPES = frozenset(
    {
        b"CODE",
        b"dnrp",
        b"INIT",
        b"DRVR",
        b"PACK",
        b"CDEF",
        b"WDEF",
        b"MDEF",
        b"MBDF",
        b"LDEF",
        b"snth",
        b"dcmp",
        b"gest",
        b"hcsl",
        b"FKEY",
        b"DDEF",
        b"GDEF",
        b"nift",
        b"ndrv",
        b"ptch",
        b"boot",
    }
)


def intl1_localization_payload():
    """Save A0 without changing CCR, then tail-call the C no-op callback."""
    # MOVEM.L A0,-(SP); MOVEA.L $58.w,A0; MOVEA.L slot*4(A0),A0; JMP (A0).
    # The callback restores A0 and consumes only the return address, matching
    # the original RTS exactly, including registers, flags and caller arguments.
    return struct.pack(">7H", 0x48E7, 0x0080, 0x2078, 0x0058, 0x2068, INTL1_LOCALIZATION_SLOT * 4, 0x4ED0)


def validate_intl1_localization(payload):
    """Do not allow an embedded executable tail to escape the code-resource audit."""
    if len(payload) != INTL1_LOCALIZATION_OFFSET + len(intl1_localization_payload()):
        raise ValueError("Unexpected INTL 1 localization extent")
    if payload[INTL1_LOCALIZATION_OFFSET:] != intl1_localization_payload():
        raise ValueError("Unexpected executable INTL 1 localization hook")


def audit_native_resources(records, handlers, require_system=False):
    """Validate every executable-bearing bundled resource, including embedded code."""
    entries = []
    found = set()
    found_localization = False
    for kind, identity, _, _, payload in records:
        key = kind, identity
        if key == (b"INTL", 1):
            validate_intl1_localization(payload)
            found_localization = True
            entries.append((kind, identity, INTL1_LOCALIZATION_OFFSET, 16, "ROMlib_intl1_definition()"))
        elif key in handlers:
            slot, target = handlers[key]
            expected = (
                finder_guest_payload(*key)
                if key in FINDER_HANDLERS
                else struct.pack(">5H", 0x2078, 0x0058, 0x2068, slot * 4, 0x4ED0)
            )
            if payload != expected:
                raise ValueError(f"Unexpected executable resource {key!r}")
            found.add(key)
            entries.append((kind, identity, 0, slot, target))
            if key == (b"hcsl", 0):
                entries.append((kind, identity, 26, 15, "ROMlib_finder_definition(4)"))
        elif kind in EXECUTABLE_TYPES:
            raise ValueError(f"Unreviewed executable resource {key!r}")
    if require_system and (found != handlers.keys() or not found_localization):
        raise ValueError("Missing System dispatch resource")
    return entries


def validate_alert_procedures(payload):
    """Require native handler identities in the early-boot DSAT procedure records."""
    if len(payload) < 2:
        raise ValueError("Truncated DSAT record count")
    count = struct.unpack_from(">H", payload)[0]
    position = 2
    handlers = []
    known = {-21000} | set(range(-21112, -21100))
    for _ in range(count):
        if position + 4 > len(payload):
            raise ValueError("Truncated DSAT record header")
        identity, length = struct.unpack_from(">hH", payload, position)
        position += 4
        if length > len(payload) - position:
            raise ValueError("Truncated DSAT record payload")
        if -22000 < identity <= -21000:
            if length != 4 or struct.unpack_from(">i", payload, position)[0] not in known:
                raise ValueError("Unreviewed executable DSAT procedure")
            handlers.append(struct.unpack_from(">i", payload, position)[0])
        position += length
    if position != len(payload):
        raise ValueError("Trailing DSAT bytes")
    return handlers
