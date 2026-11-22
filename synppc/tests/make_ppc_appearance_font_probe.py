#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_appearance_font_probe.py
#
#   Summary:   Generate an original PPC fixture for Appearance view-font outputs.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original regression fixture using the documented Appearance ABI.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    p = Probe(
        [("AppearanceLib", "GetViewsFont", 2)]
        + [
            ("InterfaceLib", name, 2)
            for name in ("GetScriptManagerVariable", "GetScriptVariable", "SetScriptVariable", "DebugStr")
        ]
    )
    # Name output has a full Str255 buffer. Size is deliberately unaligned,
    # surrounded by byte guards so a host-sized output store is detected.
    struct.pack_into(">I", p.data, 0x200, 0xA5A5A5A5)
    struct.pack_into(">I", p.data, 0x304, 0xA5A5A5A5)
    p.emit(0x3860FFFF, 0x389E0204, 0x38BE0305)
    p.call("GetViewsFont")
    p.expect(0, "default script resolves the configured view font")
    p.emit(0x887E0204)
    p.nonzero("view font has a Pascal name")
    p.emit(0xA07E0305)
    p.nonzero("view font has a nonzero big-endian size")
    p.emit(0x907E0320)
    for offset in (0x304, 0x307):
        p.emit(0x887E0000 | offset)
        p.expect(0xA5, "size output preserves adjacent bytes")
    p.emit(0x807E0200)
    p.expect(0xA5A5A5A5, "name output preserves its leading guard")

    p.emit(0x38600012)
    p.call("GetScriptManagerVariable")
    p.emit(0x907E0324, 0x389E0204, 0x38BE0305)
    p.call("GetViewsFont")
    p.expect(0, "explicit system script resolves the view font")
    p.emit(0xA07E0305, 0x809E0320)
    p.equal("default and explicit system scripts have the same size")

    # Change the Script Manager's configured application font size, retaining
    # its family. The view-font adapter must observe the real shared setting.
    p.emit(0x807E0324, 0x38800056)
    p.call("GetScriptVariable")
    p.emit(0x907E0328, 0x5465001E, 0x60A50015, 0x807E0324, 0x38800056)
    p.call("SetScriptVariable")
    p.expect(0, "configure a distinct view-font size")
    p.emit(0x3860FFFF, 0x389E0204, 0x38BE0305)
    p.call("GetViewsFont")
    p.expect(0, "resolve the changed view-font setting")
    p.emit(0xA07E0305)
    p.expect(21, "view font uses the Script Manager's configured size")
    p.emit(0x807E0324, 0x38800056, 0x80BE0328)
    p.call("SetScriptVariable")
    p.expect(0, "restore original font settings")

    for name, size in ((0, 0x305), (0x204, 0)):
        p.emit(0x3860005A, 0x987E0204, 0xB07E0305)
        p.emit(0x3860FFFF, (0x389E0000 | name) if name else 0x38800000, (0x38BE0000 | size) if size else 0x38A00000)
        p.call("GetViewsFont")
        p.expect(-50, "both GetViewsFont outputs are required")
        p.emit(0x887E0204)
        p.expect(0x5A, "invalid call preserves font name")
        p.emit(0xA07E0305)
        p.expect(0x5A, "invalid call preserves font size")
    marker = b"APPEARANCE_FONT_OK"
    p.data[0x340 : 0x341 + len(marker)] = bytes([len(marker)]) + marker
    p.emit(0x387E0340)
    p.call("DebugStr")
    p.finish(destination, "PPC Appearance Font Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
