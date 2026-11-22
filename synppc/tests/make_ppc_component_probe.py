#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_component_probe.py
#
#   Summary:   Original PPC Component Manager registration, callback and lifetime fixture.
#
#   Classes:   None declared.
#
#   Functions: make_component_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC Component Manager registration, callback and lifetime fixture."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_component_probe(destination):
    names = [
        "NewRoutineDescriptor",
        "DisposeRoutineDescriptor",
        "RegisterComponent",
        "UnregisterComponent",
        "CountComponents",
        "OpenComponent",
        "CloseComponent",
        "CountComponentInstances",
        "SetComponentInstanceError",
        "GetComponentInstanceError",
        "SetComponentInstanceStorage",
        "GetComponentInstanceStorage",
        "SetComponentRefcon",
        "GetComponentRefcon",
        "ComponentManagerVersion",
        "Gestalt",
        "FindNextComponent",
        "ComponentResourceValue",
    ]
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def call(name):
        offset = 8 + 4 * names.index(name)
        emit(0x819E0000 + offset, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value, nonzero=False):
        emit(0x2C030000 | (value & 0xFFFF))
        failures.append(len(code))
        emit(0x41820000 if nonzero else 0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("ComponentManagerVersion")
    check(3)
    # The used library's real 68K INIT registers and calls a resource component.
    call("ComponentResourceValue")
    check(42)
    emit(0x3C604350, 0x60634552, 0x389E00D0)
    call("Gestalt")
    check(-5551)
    emit(0x3C604E4F, 0x60635449, 0x389E00D0)
    call("Gestalt")
    check(0)
    emit(0x807E00D0)
    check(42)
    emit(0x387E00E0)
    call("CountComponents")
    check(1)
    emit(0x38600000, 0x389E00E0)
    call("FindNextComponent")
    check(0, True)
    call("UnregisterComponent")
    check(0)
    emit(0x387E0060, 0x388003F0, 0x38A00001)
    call("NewRoutineDescriptor")
    check(0, True)
    emit(0x907E0068)
    emit(0x7C641B78, 0x387E0080, 0x38A00001, 0x38C00000, 0x38E00000, 0x39000000)
    call("RegisterComponent")
    check(0, True)
    emit(0x907E006C)
    for offset in [180, 184, 188]:
        emit(0x807E0000 + offset)
        check(1)
    emit(0x387E0080)
    call("CountComponents")
    check(1)
    emit(0x807E006C)
    call("OpenComponent")
    check(0, True)
    emit(0x907E0070)
    emit(0x807E006C)
    call("CountComponentInstances")
    check(1)
    emit(0x807E006C)
    call("UnregisterComponent")
    check(-3000)
    emit(0x807E0070, 0x3880FFD6)
    call("SetComponentInstanceError")
    emit(0x807E0070)
    call("GetComponentInstanceError")
    check(-42)
    emit(0x807E0070)
    call("GetComponentInstanceError")
    check(0)
    emit(0x807E0070, 0x389E0100)
    call("SetComponentInstanceStorage")
    emit(0x807E0070)
    call("GetComponentInstanceStorage")
    emit(0x389E0100, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E006C, 0x3880002A)
    call("SetComponentRefcon")
    emit(0x807E006C)
    call("GetComponentRefcon")
    check(42)
    emit(0x807E0070)
    call("CloseComponent")
    check(0)
    emit(0x807E0070)
    call("CloseComponent")
    check(-3000)
    emit(0x807E006C)
    call("CountComponentInstances")
    check(0)
    for offset in [180, 184]:
        emit(0x807E0000 + offset)
        check(2)
    emit(0x807E006C)
    call("UnregisterComponent")
    check(0)
    emit(0x387E0080)
    call("CountComponents")
    check(0)
    emit(0x807E0068)
    call("DisposeRoutineDescriptor")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    callback = len(code) * 4
    emit(
        0xA8A30002,
        0x2C05FFFF,
        0x41820020,
        0x2C05FFFE,
        0x41820020,
        0x2C05FFFB,
        0x41820020,
        0x38600000,
        0x4E800020,
    )
    # Each recognized control message increments a different counter.
    for offset in [180, 184, 188]:
        emit(0x80620000 + offset, 0x38630001, 0x90620000 + offset, 0x38600000, 0x4E800020)
    # Branch offsets above: open 28, close 40, register 52 bytes from branch.
    code[callback // 4 + 2] = 0x4182001C
    code[callback // 4 + 4] = 0x41820028
    code[callback // 4 + 6] = 0x41820034
    data = bytearray(320)
    struct.pack_into(">II", data, 96, callback, 0)
    struct.pack_into(">5I", data, 128, 0x50505242, 0x54455354, 0x61726469, 0x80000000, 0)
    struct.pack_into(">5I", data, 224, 0x5436384B, 0x41424920, 0x61726469, 0, 0)
    imports = [("InterfaceLib", name, 2) for name in names[:-1]]
    imports.append(("ProbeComponents", "ComponentResourceValue", 2))
    app = pef(
        code,
        data,
        imports=imports,
        main=0,
        reloc=[0x4600, 0x4A00 | (len(names) - 1), 0xA000, 96, 0x4600],
    )
    write_fragment(destination, "PPC Component Probe", app, 1)
    init = bytearray()
    errors = []

    def raw(hexadecimal):
        init.extend(bytes.fromhex(hexadecimal))

    def verify(hexadecimal):
        raw(hexadecimal)
        errors.append(len(init))
        raw("66000000")

    # Two result slots: Get1Resource's output becomes RegisterComponentResource's input.
    raw("598f598f2f3c74686e673f3c0080a81f3f3c00017012a82a2c1f4a86")
    errors.append(len(init))
    raw("67000000")
    raw("598f2f067007a82a2e1f4a87")
    errors.append(len(init))
    raw("67000000")
    # Generic CallComponent: CP header followed by the opaque instance and result.
    raw("598f2f072f3c0000002a7000a82a")
    verify("0c9f0000002a")
    raw("558f2f077008a82a")
    verify("0c5f0000")
    # TSM application registration has real per-process state and duplicate errors.
    for selector, error in [(20, 0), (20, -2503), (21, 0), (21, -2504)]:
        raw("558f303c")
        init.extend(struct.pack(">H", selector))
        raw("aa54")
        verify("0c5f" + struct.pack(">h", error).hex())
        # NULL-document input-window state follows registration: inline for
        # registered processes, floating input for unregistered processes.
        raw("3f3ca55a42a7701daa54")
        verify("0c5f" + ("005a" if selector == 20 else "015a"))
    # An unknown non-null document produces false, preserving the Boolean pad.
    raw("3f3ca55a2f3c00000100701daa54")
    verify("0c5f005a")
    # With no active TSM document, the event remains unconsumed. Boolean
    # Pascal results occupy the high byte of a two-byte stack slot.
    raw("3f3ca55a42a77004aa54")
    verify("0c5f005a")
    raw("2f3cdeadbeef701caa54")
    verify("0c9f00000000")
    # A missing current TSM document cannot be a Unicode document.
    raw("3f3ca55a7021aa54")
    verify("0c5f005a")
    # GetDefaultInputMethodInfo clears two four-byte outputs and returns
    # false for Roman script. Keep a sentinel in the unused Boolean byte.
    raw("518f2ebcdeadbeef2f7cdeadbeef0004")
    raw("3f3ca55a4267486f0004486f000c7020aa54")
    verify("0c5f005a")
    verify("0c9f00000000")
    verify("0c9f00000000")
    # Register and send the private notification through its actual 68K stack ABI.
    raw("598f")
    callback_lea = len(init)
    raw("41fa00002f082f3c5445535442a7303c0301aa77")
    verify("0c9f00000000")
    raw("598f2f3c5445535442a7303c0300aa77")
    verify("0c9f00000000")
    raw("4e75")
    bad_init = len(init)
    raw("558f2f3c435045522f3c00000001303c0401abf1548f4e75")
    for branch in errors:
        struct.pack_into(">h", init, branch + 2, bad_init - branch - 2)
    notify = len(init)
    # Pascal void(kind, data, context): type is at 16(a6), data12, context8.
    raw("4e5600000cae544553540010")
    bad_notify_branches = []
    for comparison in ["", "4aae000c", "4aae0008"]:
        if comparison:
            raw(comparison)
        bad_notify_branches.append(len(init))
        raw("66000000")
    raw("558f2f3c4e4f54492f3c0000002a303c0401abf1548f4e5e4e74000c")
    invalid_notify = len(init)
    raw("558f2f3c435045522f3c00000002303c0401abf1548f4e5e4e74000c")
    for branch in bad_notify_branches:
        struct.pack_into(">h", init, branch + 2, invalid_notify - branch - 2)
    struct.pack_into(">h", init, callback_lea + 2, notify - callback_lea - 2)
    # A Pascal component entry answers selector42, and accepts open/close controls.
    entry = bytes.fromhex("4e560000206e000c70000c68002a00026602702a2d4000104e5e4e740008")
    thng = struct.pack(">5I4sh", 0x5436384B, 0x41424920, 0x61726469, 0, 0, b"PRBE", 128) + bytes(18)
    library = pef(
        [0x3860002A, 0x4E800020],
        bytes(8),
        exports=[("ComponentResourceValue", 2, 0, 1)],
        reloc=[0x4600],
    )
    write_fragment(
        destination,
        "ProbeComponents",
        library,
        0,
        [(b"INIT", 0, init), (b"thng", 128, thng), (b"PRBE", 128, entry)],
    )


if __name__ == "__main__":
    make_component_probe(Path(sys.argv[1]))
