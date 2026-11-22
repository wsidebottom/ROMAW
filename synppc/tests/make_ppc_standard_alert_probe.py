#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_standard_alert_probe.py
#
#   Summary:   Generate original PPC checks for standard alert buttons and callbacks.
#
#   Classes:   None.
#
#   Functions: make_standard_alert_probe
#
#   Origin:    Original Executor regression fixture; no Apple executable code.
#
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+
"""Check StandardAlert keyboard routing, packed parameters and modal filters."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_standard_alert_probe(destination):
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | (value & 65535))
        failures.append(len(code))
        emit(0x40820000)

    def alert(parameters, result, key=None, kind=3):
        if key is not None:
            emit(0x38600003, 0x38800000 | key)
            call(1)
            check(0)
        emit(0x38600000 | kind, 0x389E0040, 0x38BE0060)
        emit(0x38DE00E0 if parameters else 0x38C00000, 0x38FE0110)
        call(0)
        check(-50 if kind == 4 else 0)
        emit(0xA07E0110)
        check(result)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call(4)
    alert(False, 1, 13)  # default OK label and Return handling
    for label, offset in [(128, 230), (160, 234), (192, 238)]:
        emit(0x387E0000 | label, 0x907E0000 | offset)
    alert(True, 3, 13, kind=1)  # note icon; third button configured as the default
    alert(True, 2, 27)  # configured Cancel receives Escape
    emit(0x3860FFFF, 0x907E00E6, 0x907E00EA, 0x38600000, 0x907E00EE, 0x38600001, 0xB07E00F2)
    alert(True, 1, 13)  # -1 requests the standard OK/Cancel labels
    alert(True, 0, kind=4)  # malformed type does not open a dialog
    emit(0x387E0140, 0x38800FD0, 0x38A00001)
    call(2)
    emit(0x907E0148, 0x907E00E2)
    alert(True, 2)  # custom PPC modal filter chooses Cancel
    for offset in (336, 338, 340):
        emit(0xA87E0000 | offset)
        check(-1)  # untouched dialog background must be white
    emit(0x807E0148)
    call(3)
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    callback = len(code) * 4
    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x93E1003C, 0x7C5E1378, 0x7CBF2B78)
    call(6)  # The modal filter explicitly selects its dialog port.
    emit(0x38600004, 0x38800004, 0x38BE0150)
    call(5)
    emit(0x38C00002, 0xB0DF0000, 0x83C10038, 0x83E1003C, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600001, 0x4E800020)
    data = bytearray(352)
    for offset, text in [
        (64, b"Alert probe"),
        (96, b"Explanation"),
        (128, b"First"),
        (160, b"Cancel"),
        (192, b"Third"),
    ]:
        data[offset : offset + len(text) + 1] = bytes([len(text)]) + text
    data[224:226] = b"\x01\x01"  # movable with Help button
    struct.pack_into(">HHH", data, 242, 3, 2, 0x300A)
    struct.pack_into(">II", data, 320, callback, 0)
    fragment = pef(
        code,
        data,
        imports=[
            ("AppearanceLib", "StandardAlert", 2),
            ("InterfaceLib", "PostEvent", 2),
            ("InterfaceLib", "NewRoutineDescriptor", 2),
            ("InterfaceLib", "DisposeRoutineDescriptor", 2),
            ("InterfaceLib", "InitWindows", 2),
            ("InterfaceLib", "GetCPixel", 2),
            ("InterfaceLib", "SetPort", 2),
        ],
        main=0,
        reloc=[0x4600, 0x4A06, 0xA000, 320, 0x4600],
    )
    # Legacy malformed tables declared six entries but supplied only five.
    # The trailing heap block must never become the window's content color.
    colors = struct.pack(">IHH", 0, 0, 5)
    for index in range(5):
        component = 65535 if index in (0, 4) else 0
        colors += struct.pack(">4H", index, component, component, component)
    write_fragment(destination, "PPC Standard Alert Probe", fragment, 1, [(b"wctb", 0, colors)])


if __name__ == "__main__":
    make_standard_alert_probe(Path(sys.argv[1]))
