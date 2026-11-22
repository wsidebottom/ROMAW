#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_appearance_state_probe.py
#
#   Summary:   Generate original PPC checks for native Appearance drawing-state ownership and restoration.
#
#   Classes:   Probe, imported fixture builder.
#
#   Functions: make_probe.
#
#   Origin:    Original fixture; Appearance/ToolboxUtils state contract checked against local System 9 reference.
#
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

import struct
import sys
from pathlib import Path

from make_ppc_carbon_cf_probe import Probe


def make_probe(destination):
    appearance = (
        "GetThemeDrawingState",
        "SetThemeDrawingState",
        "DisposeThemeDrawingState",
        "NormalizeThemeDrawingState",
    )
    toolbox = (
        "GetGWorld",
        "NewGWorld",
        "SetGWorld",
        "DisposeGWorld",
        "PenSize",
        "MoveTo",
        "PenMode",
        "PenPat",
        "BackPat",
        "TextMode",
        "TextFont",
        "TextSize",
        "TextFace",
        "RGBForeColor",
        "RGBBackColor",
        "GetPenState",
        "GetForeColor",
        "GetBackColor",
        "EraseRect",
        "GetCPixel",
        "DebugStr",
        "NewPixPat",
        "MakeRGBPat",
        "PenPixPat",
        "BackPixPat",
        "OpenPort",
        "ClosePort",
        "ForeColor",
        "BackColor",
    )
    accessors = ("GetPortTextFont", "GetPortTextSize", "GetPortTextFace", "GetPortTextMode")
    p = Probe(
        [("AppearanceLib", name, 2) for name in appearance]
        + [("InterfaceLib", name, 2) for name in toolbox]
        + [("CarbonLib", name, 2) for name in accessors]
        + [("NQD", name, 2) for name in ("QDGetPatternOrigin", "QDSetPatternOrigin")]
    )
    p.data.extend(bytes(4096 - len(p.data)))
    struct.pack_into(">4h", p.data, 0x240, 0, 0, 16, 16)
    struct.pack_into(">I", p.data, 0x1FC, 0x31415926)
    struct.pack_into(">I", p.data, 0x204, 0x27182818)
    struct.pack_into(">3H", p.data, 0x250, 0x1234, 0x5678, 0x9ABC)
    struct.pack_into(">3H", p.data, 0x258, 0xABCD, 0x7654, 0x3210)
    p.data[0x260:0x268] = bytes.fromhex("aa55aa55aa55aa55")
    p.data[0x268:0x270] = bytes.fromhex("0f0f0f0f0f0f0f0f")
    p.emit(0x387E0188, 0x389E018C)
    p.call("GetGWorld")
    p.emit(0x387E0180, 0x38800020, 0x38BE0240, 0x38C00000, 0x38E00000, 0x39000000)
    p.call("NewGWorld")
    p.expect(0, "create real offscreen color port")
    p.emit(0x807E0180, 0x38800000)
    p.call("SetGWorld")

    def scalar(name, first, second=None):
        p.emit(0x38600000 | (first & 65535))
        if second is not None:
            p.emit(0x38800000 | (second & 65535))
        p.call(name)

    scalar("PenSize", 3, 4)
    scalar("MoveTo", 5, 6)
    scalar("PenMode", 10)
    scalar("TextMode", 2)
    scalar("TextFont", 3)
    scalar("TextSize", 17)
    scalar("TextFace", 1)
    for name, offset in (("RGBForeColor", 0x250), ("RGBBackColor", 0x258), ("PenPat", 0x260), ("BackPat", 0x268)):
        p.emit(0x387E0000 | offset)
        p.call(name)
    p.emit(0x3C60FFFD, 0x60630007)
    p.call("QDSetPatternOrigin")

    def pixels(offset):
        p.emit(0x387E0240)
        p.call("EraseRect")
        for index, x in enumerate((0, 1, 4, 7)):
            p.emit(0x38600000 | x, 0x38800000, 0x38BE0000 | (offset + 6 * index))
            p.call("GetCPixel")

    pixels(0x300)
    p.emit(0x387E0200)
    p.call("GetThemeDrawingState")
    p.expect(0, "capture real color/pen state")
    for offset, guard in ((0x1FC, 0x31415926), (0x204, 0x27182818)):
        p.emit(0x807E0000 | offset)
        p.expect(guard, "opaque output is one guest pointer wide")
    p.emit(0x387E0208)
    p.call("GetThemeDrawingState")
    p.expect(0, "two snapshots retain independent ownership")
    p.call("NormalizeThemeDrawingState")
    p.expect(0, "normalize through actual QuickDraw services")
    p.emit(0x387E0280)
    p.call("GetPenState")
    p.emit(0x807E0284)
    p.expect(0x00010001, "normalization restores unit pen size")
    p.emit(0xA07E0288)
    p.expect(8, "normalization restores patCopy")
    scalar("MoveTo", 9, 10)
    scalar("TextFont", 0)
    scalar("TextSize", 22)
    scalar("TextFace", 2)
    p.emit(0x807E0200, 0x38800000)
    p.call("SetThemeDrawingState")
    p.expect(0, "restore while retaining state")
    p.emit(0x387E0280)
    p.call("GetPenState")
    for offset, expected in ((0x280, 0x00060005), (0x284, 0x00040003), (0x28A, 0xAA55AA55), (0x28E, 0xAA55AA55)):
        p.emit(0x807E0000 | offset)
        p.expect(expected, "pen position, dimensions and pattern are restored")
    p.emit(0xA07E0288)
    p.expect(10, "pen mode is restored")
    for name, offset, colors in (
        ("GetForeColor", 0x2A0, (0x1234, 0x5678, 0x9ABC)),
        ("GetBackColor", 0x2A8, (0xABCD, 0x7654, 0x3210)),
    ):
        p.emit(0x387E0000 | offset)
        p.call(name)
        for index, color in enumerate(colors):
            p.emit(0xA07E0000 | (offset + 2 * index))
            p.expect(color, "RGB components retain guest byte order")
    p.emit(0x387E02B0)
    p.call("QDGetPatternOrigin")
    p.emit(0x807E02B0)
    p.expect(0xFFFD0007, "pattern origin is restored")
    for name, expected in zip(accessors, (0, 22, 2, 2)):
        p.emit(0x807E0180)
        p.call(name)
        p.expect(expected, "state restores text mode without reverting font selection")
    pixels(0x340)
    for offset in range(0, 24, 4):
        p.emit(0x807E0300 | offset, 0x809E0340 | offset)
        p.equal("restored background produces the same pixels")
    p.emit(0x807E0200, 0x38800001)
    p.call("SetThemeDrawingState")
    p.expect(0, "restore can dispose its snapshot")
    p.emit(0x807E0200)
    p.call("DisposeThemeDrawingState")
    p.expect(-50, "disposed snapshot cannot be disposed twice")
    p.emit(0x807E0208)
    p.call("DisposeThemeDrawingState")
    p.expect(0, "disposing another snapshot does not restore it")
    for name in ("GetThemeDrawingState", "SetThemeDrawingState", "DisposeThemeDrawingState"):
        scalar(name, 0, 0 if name == "SetThemeDrawingState" else None)
        p.expect(-50, "null state arguments fail explicitly")

    p.emit(0x3860FFFC)
    p.call("GetThemeDrawingState")
    p.expect(-50, "out-of-range output is rejected before allocation")
    p.emit(0x3C60DEAD, 0x60630000)
    p.call("DisposeThemeDrawingState")
    p.expect(-50, "unknown opaque token is rejected")

    # Non-monochrome pixel patterns remain valid while normalization installs
    # temporary simple patterns. Restore the original borrowed pattern handles.
    for slot, color, install in ((0x220, 0x250, "PenPixPat"), (0x224, 0x258, "BackPixPat")):
        p.call("NewPixPat")
        p.nonzero("allocate a real color pixel pattern")
        p.emit(0x907E0000 | slot, 0x389E0000 | color)
        p.call("MakeRGBPat")
        p.emit(0x807E0000 | slot)
        p.call(install)
    pixels(0x300)
    p.emit(0x387E0200)
    p.call("GetThemeDrawingState")
    p.expect(0, "capture color pixel patterns")
    p.call("NormalizeThemeDrawingState")
    p.expect(0, "normalize without invalidating borrowed color patterns")
    p.emit(0x807E0200, 0x38800001)
    p.call("SetThemeDrawingState")
    p.expect(0, "restore original color pixel patterns")
    pixels(0x340)
    for offset in range(0, 24, 4):
        p.emit(0x807E0300 | offset, 0x809E0340 | offset)
        p.equal("restored color background produces the same pixels")

    # Exercise the storage bound and release/reuse without exhausting the host.
    for index in range(512):
        p.emit(0x387E0800 | (4 * index))
        p.call("GetThemeDrawingState")
        p.expect(0, "bounded snapshot allocation")
    p.emit(0x387E0210)
    p.call("GetThemeDrawingState")
    p.expect(-108, "snapshot capacity reports memFullErr")
    p.emit(0x807E0210)
    p.expect(0, "failed capture clears the output token")
    for index in range(512):
        p.emit(0x807E0800 | (4 * index))
        p.call("DisposeThemeDrawingState")
        p.expect(0, "release all bounded snapshots")
    p.emit(0x387E0210)
    p.call("GetThemeDrawingState")
    p.expect(0, "released snapshot capacity is reusable")
    # Leave this last token to context teardown intentionally.
    p.emit(0x807E0188, 0x809E018C)
    p.call("SetGWorld")
    p.emit(0x807E0180)
    p.call("DisposeGWorld")
    # A real monochrome GrafPort has no color-pattern handles. Exercise that
    # path separately from the offscreen CGrafPort above.
    p.emit(0x387E0400)
    p.call("OpenPort")
    scalar("ForeColor", 33)
    scalar("BackColor", 30)
    scalar("PenSize", 7, 8)
    scalar("MoveTo", 2, 3)
    scalar("PenMode", 9)
    scalar("TextMode", 3)
    p.emit(0x387E0200)
    p.call("GetThemeDrawingState")
    p.expect(0, "capture monochrome port state")
    p.call("NormalizeThemeDrawingState")
    p.expect(0, "normalize monochrome port")
    p.emit(0x807E0200, 0x38800001)
    p.call("SetThemeDrawingState")
    p.expect(0, "restore and dispose monochrome state")
    p.emit(0x387E0280)
    p.call("GetPenState")
    for offset, expected in ((0x280, 0x00030002), (0x284, 0x00080007)):
        p.emit(0x807E0000 | offset)
        p.expect(expected, "monochrome pen state is restored")
    p.emit(0xA07E0288)
    p.expect(9, "monochrome pen mode is restored")
    for name, expected in (("GetForeColor", 0), ("GetBackColor", 65535)):
        p.emit(0x387E02A0)
        p.call(name)
        for offset in (0x2A0, 0x2A2, 0x2A4):
            p.emit(0xA07E0000 | offset)
            p.expect(expected, "monochrome color indices are restored")
    p.emit(0x807E0188, 0x809E018C)
    p.call("SetGWorld")
    p.emit(0x387E0400)
    p.call("ClosePort")
    marker = b"APPEARANCE_STATE_OK"
    p.data[0x380 : 0x381 + len(marker)] = bytes([len(marker)]) + marker
    p.emit(0x387E0380)
    p.call("DebugStr")
    p.finish(destination, "PPC Appearance State Probe")


if __name__ == "__main__":
    make_probe(Path(sys.argv[1]))
