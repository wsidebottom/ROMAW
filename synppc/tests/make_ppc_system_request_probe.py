#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_system_request_probe.py
#
#   Summary:   Original application fixtures for explicit and known OS requirements.
#
#   Classes:   None.
#
#   Functions: make_probe.
#
#   Origin:    Original ROMAW PPC instructions and classic resource fixtures.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_probe(destination, mode="explicit"):
    target = 0x860 if mode == "known" else 0x901
    names = ["Gestalt", "Alert", "DebugStr", "GetNextEvent"]
    code = [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378]

    def call(name):
        code.extend([0x819E0008 + 4 * names.index(name), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421])

    code.extend([0x3C607379, 0x60637376, 0x389E0080])
    call("Gestalt")
    code.extend([0x807E0080, 0x2C030000 | target])
    branch = len(code)
    code.append(0x40800000)  # bge success
    if mode == "known":
        code.append(0)  # Known minimum must be applied before application code.
    else:
        if mode == "running":
            code.extend([0x38600000, 0x389E00C0])
            call("GetNextEvent")
        code.extend([0x38600100, 0x38800000])
        call("Alert")
        code.append(0)  # A startup request must not resume this rejected run.
    code[branch] |= (len(code) - branch) * 4
    code.append(0x387E0090)
    call("DebugStr")
    code.extend([0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020])
    data = bytearray(256)
    marker = b"SYSTEM_REQUIREMENT_OK"
    data[0x90 : 0x91 + len(marker)] = bytes([len(marker)]) + marker
    imports = [("InterfaceLib", name, 2) for name in names]
    image = pef(code, data, imports=imports, main=0, reloc=[0x4600, 0x4A03])
    text = b"This application requires System 9.0.1 or later."
    item = bytes(4) + struct.pack(">4hBB", 10, 10, 80, 360, 0x88, len(text)) + text
    item += bytes(len(text) & 1)
    resources = [
        (b"ALRT", 256, struct.pack(">4hHH", 50, 50, 200, 450, 256, 0x5555)),
        (b"DITL", 256, b"\0\0" + item),
    ]
    if mode == "known":
        resources.append((b"vers", 1, bytes.fromhex("086080000000") + b"\x038.6\x04test"))
    name = "PPC System Request Probe"
    write_fragment(destination, name, image, 1, resources)
    if mode == "known":
        sidecar = destination / ".AppleDouble" / name
        content = bytearray(sidecar.read_bytes())
        offset = struct.unpack_from(">I", content, 42)[0]
        content[offset : offset + 8] = b"FNDRMACS"
        sidecar.write_bytes(content)


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "explicit")
