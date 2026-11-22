#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_process_probe.py
#
#   Summary:   Original PPC checks for ExpandMem copying and system-client nesting.
#
#   Classes:   None declared.
#
#   Functions: make_process_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC checks for ExpandMem copying and system-client nesting."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_process_probe(destination):
    # A linked library must be discoverable while its initializer is running;
    # loading the same connection must not recursively invoke that initializer.
    self_code = [0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378]
    self_failures = []

    def self_emit(*words):
        self_code.extend(words)

    def self_check():
        self_emit(0x2C030000)
        self_failures.append(len(self_code))
        self_emit(0x40820000)

    self_emit(0x807E0080)
    self_check()
    self_emit(0x38600001, 0x907E0080)
    for flags in [2, 1]:
        self_emit(
            0x387E0090,
            0x3C807077,
            0x60847063,
            0x38A00000 | flags,
            0x38DE00C0,
            0x38FE00C4,
            0x391E00C8,
            0x819E0008,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )
        self_check()
        self_emit(
            0x807E00C0,
            0x389E00A0,
            0x38BE00D0,
            0x38DE00D4,
            0x819E000C,
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )
        self_check()
        self_emit(0x807E00D0, 0x389E0080, 0x7C032000)
        self_failures.append(len(self_code))
        self_emit(0x40820000)
    self_emit(
        0x38600042,
        0x907E0080,
        0x83C10038,
        0x80010044,
        0x7C0803A6,
        0x38210040,
        0x38600000,
        0x4E800020,
    )
    bad = len(self_code)
    self_emit(0)
    for branch in self_failures:
        self_code[branch] |= (bad - branch) * 4
    self_data = bytearray(320)
    struct.pack_into(">II", self_data, 0, 0, 0)
    self_data[144:154] = b"\x09ProbeSelf"
    self_data[160:171] = b"\x0aProbeState"
    library = pef(
        self_code,
        self_data,
        imports=[
            ("InterfaceLib", "GetSharedLibrary", 2),
            ("InterfaceLib", "FindSymbol", 2),
        ],
        exports=[("ProbeState", 1, 128, 1)],
        init=0,
        reloc=[0x4600, 0x4A01],
    )
    write_fragment(destination, "ProbeSelf", library, 0)
    imports = (
        [
            ("InterfaceLib", name, 2)
            for name in [
                "GetPtrSize",
                "NewPtrSysClear",
                "BlockMoveData",
                "DisposePtr",
                "GetCurrentProcess",
                "WakeUpProcess",
                "WaitNextEvent",
                "UpTime",
                "AbsoluteToNanoseconds",
                "Microseconds",
                "GetFrontProcess",
                "CurResFile",
                "GetZone",
                "FreeMem",
                "PurgeSpace",
                "NewHandleClear",
                "HLockHi",
                "HGetState",
                "DisposeHandle",
                "CallUniversalProc",
                "NewMenu",
                "AppendMenu",
                "SetItemIcon",
                "GetItemIcon",
                "DisposeMenu",
                "IsMenuBarVisible",
                "HideMenuBar",
                "ShowMenuBar",
                "GetMBarHeight",
                "InitMenus",
                "InitWindows",
                "GetPort",
                "GetThemeMenuSeparatorHeight",
                "GetThemeMenuItemExtra",
                "GetMenuItemIconHandle",
                "GetMenuItemModifiers",
                "GetMenuItemKeyGlyph",
                "GetMenuItemFontID",
                "GetMenuItemTextEncoding",
                "GetMenuItemCommandID",
                "HideCursor",
                "SetThemeCursor",
                "LowercaseText",
                "KeyTranslate",
                "CharacterByteType",
                "LMGetCurDirStore",
                "LMSetCurDirStore",
            ]
        ]
        + [
            ("PrivateInterfaceLib", name, 2)
            for name in [
                "GetSystemClientProcess",
                "BeginSystemMode",
                "EndSystemMode",
                "LMGetKeyMap",
                "GetMap",
                "IsSystemMenu",
                "GetAuxMenuItem",
                "RealScript",
                "Font2RealScript",
            ]
        ]
        + [("ProbeSelf", "ProbeState", 1)]
    )
    names = [name for _, name, _ in imports]
    code, failures = [], []

    def emit(*words):
        code.extend(words)

    def call(name):
        emit(
            0x819E0008 + 4 * names.index(name),
            0x800C0000,
            0x804C0004,
            0x7C0903A6,
            0x4E800421,
        )

    def unequal():
        failures.append(len(code))
        emit(0x40820000)

    def check(value):
        emit(0x2C030000 | (value & 0xFFFF))
        unequal()

    def client(system=False):
        emit(0x387E0118)
        call("GetSystemClientProcess")
        check(0)
        for offset in [0, 4]:
            emit(0x807E0118 + offset)
            if system:
                check(1 if offset else 0)
            else:
                emit(0x809E0110 + offset, 0x7C032000)
                unequal()

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    call("RealScript")
    check(0)
    emit(0x38600000)
    call("Font2RealScript")
    check(0)
    for key, result, state in [
        (0, 97, 0),
        (1, 0, 132),
        (0, 0x87, 0),
        (1, 0, 132),
        (2, 0x780027, 0),
    ]:
        emit(0x387E0400, 0x38800000 | key, 0x38BE01C0)
        call("KeyTranslate")
        emit(0x3C800000 | (result >> 16), 0x60840000 | (result & 0xFFFF), 0x7C032000)
        unequal()
        emit(0x807E01C0)
        check(state)
    emit(0x387E0200, 0x38800100, 0x38A00000)
    call("LowercaseText")
    expected = bytearray(range(256))
    for value in range(256):
        try:
            lower = bytes([value]).decode("mac_roman").lower().encode("mac_roman")
        except UnicodeEncodeError:
            continue
        if len(lower) == 1:
            expected[value] = lower[0]
    for offset in range(0, 256, 4):
        value = int.from_bytes(expected[offset : offset + 4], "big")
        emit(
            0x807E0200 + offset,
            0x3C800000 | (value >> 16),
            0x60840000 | (value & 0xFFFF),
            0x7C032000,
        )
        unequal()
    for offset in [511, 768]:
        emit(0x887E0000 + offset)
        check(0xAA)
    for offset in [0, 127, 128, 255]:
        emit(0x387E0200, 0x38800000 | offset, 0x38A00000)
        call("CharacterByteType")
        check(0)
    call("HideCursor")
    emit(0xA86008D0)
    check(-1)
    emit(0x38600001)
    call("SetThemeCursor")
    check(-4)
    emit(0xA86008D0)
    check(-1)
    emit(0x38600000)
    call("SetThemeCursor")
    check(0)
    emit(0xA86008D0)
    check(0)
    emit(0x886008CC)
    check(1)
    emit(0x807E0008 + 4 * names.index("ProbeState"), 0x80630000)
    check(0x42)
    call("InitWindows")
    call("InitMenus")
    call("GetMBarHeight")
    emit(0x907E01E0, 0x387E01E4)
    call("GetPort")
    for _ in range(2):
        call("HideMenuBar")
    call("IsMenuBarVisible")
    check(0)
    call("GetMBarHeight")
    check(0)
    # The desktop region now includes the menu strip.
    emit(0x806009EE, 0x80630000, 0xA8630002)
    check(0)
    for _ in range(2):
        call("ShowMenuBar")
    call("IsMenuBarVisible")
    check(1)
    call("GetMBarHeight")
    emit(0x809E01E0, 0x7C032000)
    unequal()
    emit(0x806009EE, 0x80630000, 0xA8630002, 0x7C032000)
    unequal()
    emit(0x387E01E8)
    call("GetPort")
    emit(0x807E01E8, 0x809E01E4, 0x7C032000)
    unequal()
    emit(0x387E0130)
    call("GetPort")
    emit(0x387E0138)
    call("GetThemeMenuSeparatorHeight")
    check(0)
    emit(0x387E0134)
    call("GetPort")
    emit(0x807E0130, 0x809E0134, 0x7C032000)
    unequal()
    emit(0x38600000, 0x389E0130, 0x38BE0134)
    call("GetThemeMenuItemExtra")
    check(0)
    emit(0xA87E0130)
    check(0)
    emit(0xA87E0134)
    check(0)
    call("IsMenuBarVisible")
    check(1)
    call("GetMBarHeight")
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x40810000)
    emit(0x386000C8, 0x389E0150)
    call("NewMenu")
    emit(0x907E0140, 0x389E0150)
    call("AppendMenu")
    emit(0x807E0140, 0x38800001, 0x38A00004)
    call("SetItemIcon")
    emit(0x807E0140, 0x38800001, 0x38BE0130, 0x38DE0134)
    call("GetMenuItemIconHandle")
    check(0)
    emit(0x887E0130)
    check(0)
    emit(0x807E0134)
    check(0)
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetMenuItemModifiers")
    check(0)
    emit(0x887E0130)
    check(0)
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetMenuItemKeyGlyph")
    check(0)
    emit(0xA87E0130)
    check(0)
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetMenuItemTextEncoding")
    check(0)
    emit(0x807E0130)
    check(0)
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetMenuItemCommandID")
    check(0)
    emit(0x807E0130)
    check(0)
    for name in [
        "GetMenuItemModifiers",
        "GetMenuItemKeyGlyph",
        "GetMenuItemFontID",
        "GetMenuItemTextEncoding",
        "GetMenuItemCommandID",
    ]:
        emit(0x3C60DEAD, 0x6063BEEF, 0x907E0130)
        emit(0x807E0140, 0x38800002, 0x38BE0130)
        call(name)
        check(-50)
        emit(0x807E0130, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
        unequal()
        emit(0x807E0140, 0x38800001, 0x38A00000)
        call(name)
        check(-50)
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetMenuItemFontID")
    check(0)
    emit(0xA87E0130)
    check(0)
    # Resource icon IDs are distinct from attached icon handles.
    emit(0x807E0140, 0x38800001, 0x38BE0130)
    call("GetItemIcon")
    emit(0xA87E0130)
    check(4)
    for item, out_type, out_handle in [(2, 0x130, 0x134), (1, 0, 0x134), (1, 0x130, 0)]:
        emit(0x807E0140, 0x38800000 | item)
        emit(0x38BE0000 | out_type if out_type else 0x38A00000)
        emit(0x38DE0000 | out_handle if out_handle else 0x38C00000)
        call("GetMenuItemIconHandle")
        check(-50)
    # Unsupported auxiliary records must report an error without modifying
    # the caller's buffer; MDEFs retain their existing classic item fields.
    emit(0x3C60DEAD, 0x6063BEEF, 0x907E0130)
    emit(0x807E0140, 0x389E0150, 0x38BE0130, 0x38C00004)
    call("GetAuxMenuItem")
    check(-4)
    emit(0x807E0130, 0x3C80DEAD, 0x6084BEEF, 0x7C032000)
    unequal()
    emit(0x807E0140)
    call("DisposeMenu")
    call("LMGetKeyMap")
    check(0x174)
    # Directory IDs are full-width longs, including IDs above 65535.
    call("LMGetCurDirStore")
    emit(0x907E0170)
    emit(0x3C601234, 0x60635678)
    call("LMSetCurDirStore")
    call("LMGetCurDirStore")
    emit(0x3C801234, 0x60845678, 0x7C032000)
    unequal()
    emit(0x807E0170)
    call("LMSetCurDirStore")
    emit(0x8060103C, 0x388003BE, 0x38A0007C, 0x38C00000)
    call("CallUniversalProc")
    check(0)
    for identifier, expected in [
        (-20481, 0),
        (-20480, 1),
        (-16385, 1),
        (-16384, 0),
        (0, 0),
    ]:
        emit(0x38600000 | (identifier & 0xFFFF), 0x389E0130)
        call("IsSystemMenu")
        check(0)
        emit(0x887E0130)
        check(expected)
    emit(0x38600000, 0x38800000)
    call("IsSystemMenu")
    check(-50)
    emit(0x38600010)
    call("NewHandleClear")
    emit(0x907E0100)
    call("HLockHi")
    emit(0x807E0100)
    call("HGetState")
    emit(0x70630080)
    check(128)
    emit(0x807E0100)
    call("DisposeHandle")
    call("CurResFile")
    emit(0x907E0100)
    call("GetMap")
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x80630000, 0xA8630014, 0x809E0100, 0x7C032000)
    unequal()
    emit(0x38608000)
    call("GetMap")
    check(0)
    call("GetZone")
    emit(0x80830000, 0x7C832050, 0x909E0104)
    call("FreeMem")
    emit(0x907E0108, 0x387E0130, 0x389E0134)
    call("PurgeSpace")
    # Guest-endian output must fit the real zone, cover already free bytes
    # (apart from the private header), and bound the contiguous free extent.
    emit(0x807E0130, 0x809E0104, 0x7C032040)
    failures.append(len(code))
    emit(0x41810000)
    emit(0x3863000C, 0x809E0108, 0x7C032040)
    failures.append(len(code))
    emit(0x41800000)
    emit(0x807E0134, 0x809E0130, 0x7C032040)
    failures.append(len(code))
    emit(0x41810000)
    emit(0x387E0130, 0x38800001, 0x38A00000)
    call("AbsoluteToNanoseconds")
    emit(0x807E0130)
    check(40)
    emit(0x807E0134)
    check(0)
    emit(0x387E0130, 0x38800000, 0x38A0FFFF)
    call("AbsoluteToNanoseconds")
    emit(0x807E0130)
    check(39)
    emit(0x807E0134, 0x3880FFD8, 0x7C032000)
    unequal()
    emit(0x387E0120)
    call("UpTime")
    emit(0x387E0128)
    call("UpTime")
    # Uptime is relative to emulator startup, not the host clock epoch.
    # This probe has a 15-second deadline, well below a 25 MHz high-word tick.
    emit(0x807E0120)
    check(0)
    # Lexicographic unsigned comparison handles a low-word rollover.
    emit(0x807E0128, 0x809E0120, 0x7C032040)
    failures.append(len(code))
    emit(0x41800000)
    emit(0x41810014, 0x807E012C, 0x809E0124, 0x7C032040)
    failures.append(len(code))
    emit(0x41800000)
    emit(0x387E0130)
    call("Microseconds")
    emit(0x807E0130)
    check(0)
    # The advertised byte extent must match the actual allocation. Extenders
    # copy this many bytes, including the Process Manager and AppleEvent fields.
    emit(0x806002B6, 0x907E0100)
    call("GetPtrSize")
    emit(0x907E0104, 0x809E0100, 0x80840002, 0x7C032000)
    unequal()
    emit(0x38630040)
    call("NewPtrSysClear")
    emit(0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x907E0108, 0x7C641B78, 0x807E0100, 0x80BE0104)
    call("BlockMoveData")
    emit(0x807E0108, 0xA0630128)
    check(1)
    for offset in [340, 380]:
        emit(0x807E0108, 0x80630000 + offset, 0x809E0100, 0x80840000 + offset, 0x7C032000)
        unequal()
    emit(0x807E0108, 0x906002B6)
    emit(0x387E0110)
    call("GetCurrentProcess")
    check(0)
    emit(0x387E0110)
    call("WakeUpProcess")
    check(0)
    emit(0x38600000, 0x389E0160, 0x3CA07FFF, 0x60A5FFFF, 0x38C00000)
    call("WaitNextEvent")
    check(0)
    emit(0x387E0118)
    call("GetFrontProcess")
    check(0)
    for offset in [0, 4]:
        emit(0x807E0118 + offset, 0x809E0110 + offset, 0x7C032000)
        unequal()
    client()
    for _ in range(2):
        call("BeginSystemMode")
        check(0)
    client(True)
    call("EndSystemMode")
    check(0)
    client(True)
    call("EndSystemMode")
    check(0)
    client()
    call("EndSystemMode")
    check(0)
    client()
    emit(0x38600000)
    call("GetSystemClientProcess")
    check(-50)
    emit(0x807E0100, 0x906002B6, 0x807E0108)
    call("DisposePtr")
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    data = bytearray(1536)
    data[511] = data[768] = 0xAA
    data[512:768] = bytes(range(256))
    struct.pack_into(">H", data, 1024 + 258, 1)
    data[1024 + 260] = ord("a")
    data[1024 + 262] = ord("x")
    struct.pack_into(">H", data, 1024 + 388, 1)
    data[1024 + 390 : 1024 + 398] = bytes([0, 1, 0, 1, 97, 0x87, 0, 39])
    data[336:342] = b"\x05Probe"
    app = pef(
        code,
        data,
        imports=imports,
        main=0,
        reloc=[0x4600, 0x4A00 | (len(imports) - 1)],
    )
    write_fragment(destination, "PPC Process Probe", app, 1)


if __name__ == "__main__":
    make_process_probe(Path(sys.argv[1]))
