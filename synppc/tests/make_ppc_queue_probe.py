#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_queue_probe.py
#
#   Summary:   Generate original PPC checks for native Toolbox queue links and packed header guards.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original fixture for the existing native Enqueue/Dequeue register-vector services.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    p = Probe([("InterfaceLib", name, 2) for name in ("Enqueue", "Dequeue", "DebugStr")])
    struct.pack_into(">IHIII", p.data, 0x1FC, 0x31415926, 0x55AA, 0, 0, 0x27182818)
    for offset in (0x240, 0x260, 0x280):
        struct.pack_into(">IHI", p.data, offset, 0xDEADBEEF, 0x1234, 0x01020304)

    def link(field, node, label):
        p.emit(0x807E0000 | field, (0x389E0000 | node) if node else 0x38800000)
        p.equal(label)

    def queue(name, node):
        p.emit(0x387E0000 | node, 0x389E0200)
        p.call(name)

    for offset in (0x240, 0x260, 0x280):
        queue("Enqueue", offset)
        link(0x202, 0x240, "queue head retains the first guest element")
        link(0x206, offset, "queue tail points to the last guest element")
        link(offset, 0, "enqueue clears the new tail's next pointer")
    queue("Enqueue", 0x260)
    link(0x240, 0x260, "duplicate enqueue preserves the first link")
    link(0x260, 0x280, "duplicate enqueue preserves the second link")
    link(0x280, 0, "duplicate enqueue does not create a cycle")
    queue("Dequeue", 0x260)
    p.expect(0, "remove a middle element")
    link(0x240, 0x280, "middle removal repairs the preceding guest link")
    link(0x206, 0x280, "middle removal preserves the tail")
    queue("Dequeue", 0x260)
    p.expect(-1, "removing an absent element reports qErr")
    queue("Dequeue", 0x240)
    p.expect(0, "remove the head")
    link(0x202, 0x280, "head advances to the remaining element")
    queue("Dequeue", 0x280)
    p.expect(0, "remove the final element")
    link(0x202, 0, "empty queue has a null head")
    link(0x206, 0, "empty queue has a null tail")
    for offset, value in ((0x1FC, 0x31415926), (0x20A, 0x27182818)):
        p.emit(0x807E0000 | offset)
        p.expect(value, "packed queue header preserves adjacent bytes")
    p.emit(0xA07E0200)
    p.expect(0x55AA, "queue flags are preserved")
    for offset in (0x240, 0x260, 0x280):
        p.emit(0xA07E0000 | (offset + 4))
        p.expect(0x1234, "queue entry type is preserved")
        p.emit(0x807E0000 | (offset + 6))
        p.expect(0x01020304, "queue entry payload is preserved")
    marker = b"QUEUE_OK"
    p.data[0x300 : 0x301 + len(marker)] = bytes([len(marker)]) + marker
    p.emit(0x387E0300)
    p.call("DebugStr")
    p.finish(destination, "PPC Queue Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
