#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_popup_probe.py
#
#   Summary:   Generate original PPC checks for dynamic popups and window update clipping.
#   Classes:   None declared.
#   Functions: make_popup_probe
#   Origin:    Original regression fixture using the repository PEF generator.
#   Copyright and Legal notices: See synppc/COPYING.
# ===================================================================+

"""Check popup menu binding, ownership and window update region isolation."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_popup_probe(destination):
    first_control_id = 1024  # Reserve a separate range in the shared application resource fork.
    names = [
        "InitWindows",
        "NewWindow",
        "SetPort",
        "DisposeWindow",
        "GetNewControl",
        "DisposeControl",
        "NewMenu",
        "AppendMenu",
        "InsertMenu",
        "DeleteMenu",
        "DisposeMenu",
        "CountMenuItems",
        "DrawControlInCurrentPort",
        "GetControlValue",
        "GetControlMaximum",
        "SetControlValue",
        "GetCPixel",
        "BeginUpdate",
        "EndUpdate",
        "NewGWorld",
        "DisposeGWorld",
        "SetOrigin",
    ]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(0x819E0008 + 4 * names.index(name), 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def menu():
        emit(0x3860004D, 0x389E0068)
        call("NewMenu")
        emit(0x907E0078, 0x389E0090)
        call("AppendMenu")
        emit(0x807E0078, 0x3880FFFF)
        call("InsertMenu")

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("InitWindows")
    emit(0x38600000, 0x389E00C0, 0x38BE0068, 0x38C00001, 0x38E00000, 0x3900FFFF, 0x39200000, 0x39400000)
    call("NewWindow")
    emit(0x907E0070)
    call("SetPort")
    for index, late in enumerate((False, True, True)):
        if not late:
            menu()
        emit(0x38600000 | (first_control_id + index), 0x809E0070)
        call("GetNewControl")
        emit(0x907E0074, 0x2C030000)
        failures.append(len(code))
        emit(0x41820000)
        if late:
            menu()
        emit(0x807E0074)
        call("DrawControlInCurrentPort")
        emit(0x807E0074)
        call("GetControlMaximum")
        check(2)
        emit(0x807E0074)
        call("GetControlValue")
        check(1)
        # A black arrow pixel inside the control proves the menu was bound.
        emit(0x38600069, 0x38800009, 0x38BE007C)
        call("GetCPixel")
        emit(0xA87E007C)
        check(0)
        # Append after binding, then select the new item through the live menu.
        emit(0x807E0078, 0x389E00A0)
        call("AppendMenu")
        emit(0x807E0074)
        call("DrawControlInCurrentPort")
        emit(0x807E0074)
        call("GetControlMaximum")
        check(3)
        emit(0x807E0074, 0x38800003)
        call("SetControlValue")
        emit(0x807E0074)
        call("GetControlValue")
        check(3)
        emit(0x807E0074)
        call("DisposeControl")
        emit(0x807E0078)
        call("CountMenuItems")
        check(3)  # Control disposal must leave the caller-owned menu alive.
        emit(0x3860004D)
        call("DeleteMenu")
        emit(0x807E0078)
        call("DisposeMenu")
    # Offscreen SetOrigin must not translate the window's saved visible
    # region. EndUpdate must restore the same local window bounds.
    emit(0x387E00D0, 0x38800008, 0x38BE00C0, 0x38C00000, 0x38E00000, 0x39000000)
    call("NewGWorld")
    check(0)
    emit(0x807E0070)
    call("BeginUpdate")
    emit(0x807E00D0)
    call("SetPort")
    emit(0x3860008C, 0x3880008C)
    call("SetOrigin")
    emit(0x807E0070)
    call("SetPort")
    emit(0x807E0070)
    call("EndUpdate")
    for offset, expected in ((2, 0), (4, 0), (6, 32), (8, 128)):
        emit(0x807E0070, 0x80630018, 0x80630000, 0xA8630000 | offset)
        check(expected)
    # The update window's own origin still translates its saved region.
    emit(0x807E0070)
    call("BeginUpdate")
    emit(0x38600003, 0x38800004)
    call("SetOrigin")
    emit(0x807E0070)
    call("EndUpdate")
    emit(0x38600000, 0x38800000)
    call("SetOrigin")
    for offset, expected in ((2, 0), (4, 0), (6, 32), (8, 128)):
        emit(0x807E0070, 0x80630018, 0x80630000, 0xA8630000 | offset)
        check(expected)
    emit(0x807E00D0)
    call("DisposeGWorld")
    emit(0x807E0070)
    call("DisposeWindow")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(224)
    struct.pack_into(">4h", data, 192, 40, 40, 72, 168)
    data[144:148] = b"\x03A;B"
    data[160:162] = b"\x01C"
    app = pef(
        code,
        data,
        imports=[("CarbonLib" if n == "NewWindow" else "InterfaceLib", n, 2) for n in names],
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1)],
    )
    controls = [
        (b"CNTL", first_control_id + i, struct.pack(">4hhBBhhhI", 2, 2, 22, 126, 0, 1, 0, 0, 77, proc, 0) + b"\0")
        for i, proc in enumerate((1008, 1008, 400))
    ]
    write_fragment(destination, "PPC Popup Probe", app, 1, controls)


if __name__ == "__main__":
    make_popup_probe(Path(sys.argv[1]))
