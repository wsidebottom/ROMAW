#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_callback_probe.py
#
#   Summary:   Original PPC Mixed Mode and native Toolbox callback regression fixture.
#
#   Classes:   None declared.
#
#   Functions: make_callback_probe
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Original PPC Mixed Mode and native Toolbox callback regression fixture."""
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from make_ppc_probe import write_fragment
from make_ppc_shared_probe import pef


def make_callback_probe(destination, mode="valid"):
    if mode not in ("valid", "invalid-stack", "recursive", "intl", "grow", "queue", "linebreak", "crm", "ot"):
        raise ValueError(mode)
    code = []
    failures = []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0300 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def check(value):
        emit(0x2C030000 | value)
        failures.append(len(code))
        emit(0x40820000)

    emit(0x9421FF80, 0x7C0802A6, 0x90010084, 0x93C10078, 0x7C5E1378)
    emit(0x387E00F0, 0x388000C0, 0x38A00001)
    call(0)
    emit(0x907E02C0, 0x907E005C)  # save descriptor and NMRec.nmResp
    # Native Toolbox callbacks must retain the application's stack chain;
    # REALbasic checks it against the application stack's lower bound.
    emit(0x903E00E8, 0x3C801234, 0x60845678, 0x90810004)
    emit(0x387E0040)
    call(2)
    check(0)  # native NMInstall invokes PPC callback
    emit(0x807E0060)
    check(42)  # NMRec.nmRefCon changed through guest pointer
    emit(0x38600000, 0x907E0060, 0x807E02C0, 0x388000C0, 0x38BE0040)
    if mode == "invalid-stack":
        emit(0x38200000)
    call(3)
    emit(0x807E0060)
    check(42)  # guest CallUniversalProc invokes same descriptor
    # Resplendence wraps an existing PPC UPP in a kM68kISA descriptor.
    # CallUniversalProc must follow the wrapper rather than execute it as 68K.
    emit(0x807E02C0, 0x388000C5, 0x38A00000)
    call(0)
    emit(0x907E02C4, 0x388000C5, 0x38BE0040, 0x38C00000, 0x90DE0060)
    call(3)
    emit(0x807E0060)
    check(42)
    emit(0x807E02C4)
    call(1)
    emit(0x807E02C0, 0x907E008C, 0x387E0080)
    call(4)
    check(0xFFCD)
    emit(0x807E00A0)
    check(42)  # File Manager completion also invokes PPC
    emit(0x387E0100, 0x38800000)
    call(9)
    check(1)
    emit(0x887E0100)
    check(0)  # Roman parse-table bytes
    emit(0x387E0200, 0x38800003)
    call(10)
    emit(0x887E0200)
    check(65)
    emit(0x887E0202)
    check(90)
    emit(0x387E0200, 0x38800003)
    call(11)
    emit(0x887E0200)
    check(97)
    emit(0x887E0202)
    check(122)
    emit(0x387E0200, 0x38800000, 0x38A00000)
    call(12)
    check(1)  # Roman lowercase character classification
    if mode == "intl":
        emit(0x387E0200, 0x389E0200, 0x38A00003, 0x38C00003)
        call(13)
        check(0)  # equal Roman text
        emit(0x387E0200, 0x389E0201, 0x38A00001, 0x38C00001)
        call(13)
        check(0xFFFF)  # a sorts before b
    else:
        emit(0x387E0208, 0x38800001)
        call(13)
        emit(0x887E0208)
        check(3)
        emit(0x887E0209)
        check(65)
        emit(0x887E020B)
        check(90)
    # Direction accessors share the classic signed word at 0x0BAC. Preserve
    # its adjacent word and restore the caller's original direction.
    call(16)
    emit(0x907E0240, 0xA0600BAE, 0x907E0244)
    for direction in (-1, 0, 0x1234):
        emit(0x38600000 | (direction & 65535))
        call(17)
        emit(0xA8600BAC)
        check(direction & 65535)
        call(16)
        check(direction & 65535)
        emit(0xA0600BAE, 0x809E0244, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
    emit(0x807E0240)
    call(17)
    call(16)
    emit(0x809E0240, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    if mode == "crm":
        # CRM initialization is idempotent and returns the real live queue.
        call(28)
        check(0)
        call(29)
        check(1)
        call(30)
        emit(0x907E0294, 0x80830002, 0x909E0298, 0x80830006, 0x909E029C)
        call(28)
        check(0)
        call(30)
        emit(0x809E0294, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        for expected in (1, 2):
            emit(0x387E0400)
            call(32)
            emit(0x80630012, 0x907E0412)
            check(expected)
        emit(0x387E0400)
        call(32)
        check(0)
        for offset in (1064, 1104):
            emit(0x387E0000 | offset)
            call(31)
        # Search skips other device types and compares IDs without overflow.
        for kind, minimum, record in ((1, 2, 1064), (2, 0, 1104)):
            emit(0x38600000 | kind, 0x907E040E, 0x38600000 | minimum, 0x907E0412, 0x387E0400)
            call(32)
            emit(0x389E0000 | record, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)
        emit(0x3C607FFF, 0x6063FFFF, 0x907E0412, 0x387E0400)
        call(32)
        check(0)
        for offset in (1064, 1104):
            emit(0x387E0000 | offset)
            call(33)
            check(0)
        call(30)
        for offset, saved in ((2, 664), (6, 668)):
            emit(0x907E0294, 0x80630000 | offset, 0x809E0000 | saved, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)
            emit(0x807E0294)
        emit(0x38600000)
        call(31)
        emit(0x38600000)
        call(32)
        check(0)
        emit(0x38600000)
        call(33)
        check(-50 & 65535)
    if mode == "ot":
        # An unloaded Open Transport stack reports no configured interface.
        # Failed queries preserve the entire classic 552-byte result record.
        for index in (-1, 0, 1, 0x7FFFFFFF):
            emit(0x387E0400, 0x3C800000 | ((index >> 16) & 65535), 0x60840000 | (index & 65535))
            call(34)
            check(-3201 & 65535)
        emit(0x387E0400, 0x3880FFFE)
        call(34)
        check(-3221 & 65535)
        emit(0x38600000, 0x3880FFFF)
        call(34)
        check(-50 & 65535)
        for offset in range(1024, 1576, 4):
            emit(0x807E0000 | offset, 0x3C80A5A5, 0x6084A5A5, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)
    if mode == "linebreak":
        # DrawJustified adds scaled slop while preserving existing spExtra.
        emit(0x387E02B0)
        call(26)
        emit(0x807E02B0, 0x8083004C, 0x909E02B4, 0x3C600002)
        call(27)
        for scale in (1, 2):
            for slop in (0, 4, -2):
                emit(0x3860000A, 0x38800028)
                call(24)
                emit(
                    0x387E0260,
                    0x38800002,
                    0x3CA00000 | (slop & 65535),
                    0x38C00000,
                    0x3CE00001,
                    0x60E70000 | scale,
                    0x3D000001,
                    0x61080001,
                )
                call(23)
                emit(0x387E02A8)
                call(25)
                emit(0xA87E02AA)
                if slop == 0:
                    emit(0x907E02A0)
                else:
                    emit(0x809E02A0, 0x7C641850)
                    check(slop & 65535)
                emit(0x807E02B0, 0x8063004C, 0x3C800002, 0x7C032000)
                failures.append(len(code))
                emit(0x40820000)
        # Invalid and empty runs leave the pen untouched.
        for length, pointer, denominator in ((0, 608, 1), (-1, 608, 1), (2, 0, 1), (2, 608, 0)):
            emit(0x3860000A, 0x38800028)
            call(24)
            emit(
                0x387E0000 | pointer if pointer else 0x38600000,
                0x38800000 | (length & 65535),
                0x38A00000,
                0x38C00000,
                0x3CE00001,
                0x60E70001,
                0x3D000001,
                0x61080000 | denominator,
            )
            call(23)
            emit(0x387E02A8)
            call(25)
            emit(0xA87E02AA)
            check(10)
        emit(0x807E02B4)
        call(27)
        # VisibleLength trims trailing Roman spaces without changing the text.
        for pointer, length, expected in ((640, 7, 4), (640, 5, 4), (645, 2, 0), (640, 0, 0), (0, 7, 0), (640, -1, 0)):
            emit(0x387E0000 | pointer if pointer else 0x38600000, 0x38800000 | (length & 65535))
            call(20)
            check(expected)
        emit(0x387E0280, 0x3C807FFF, 0x6084FFFF)
        call(20)
        check(0)  # reject a guest span extending beyond emulated memory
        emit(0x887E0285)
        check(32)
        # Older Script Manager wrappers must supply the required remaining-width
        # output internally, and preserve negative signed pixel arguments.
        emit(0x387E0250, 0x38800002, 0x38A00000, 0x38C00000, 0x38FE0268)
        call(21)
        check(0)
        emit(0x887E0268)
        check(1)
        emit(0x387E0250, 0x38800002, 0x38A00000, 0x38C0FFFF, 0x38FE0268)
        call(21)
        check(0)
        emit(0x887E0268)
        check(0)
        emit(0x387E0250, 0x38800002, 0x38A00000, 0x38C00001, 0x38E00000)
        call(22)
        emit(0x907E0278, 0x3860004D)
        call(15)
        emit(0x809E0278, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        # Exercise the seven-argument Script Manager bridge and big-endian
        # in/out fields. A fitting run consumes its width and all characters.
        emit(0x3C600100, 0x907E0210, 0x38600000, 0x907E0214)
        emit(0x387E0220, 0x38800003, 0x38A00000, 0x38C00003, 0x38E00000, 0x391E0210, 0x393E0214)
        call(8)
        check(2)
        emit(0x807E0214)
        check(3)
        emit(0x807E0210, 0x3C800100, 0x7C032000)
        failures.append(len(code))
        emit(0x40800000)  # remaining width must decrease
        # A hard return terminates the line at the byte after the return.
        emit(0x387E0224, 0x38800003, 0x38A00000, 0x38C00003, 0x38E00000, 0x391E0210, 0x393E0214)
        call(8)
        check(0)
        emit(0x807E0214)
        check(2)
        # Roman weighting is proportional to the current font's space width,
        # space count and horizontal scaling; packed Points must stay in order.
        emit(0x38600020)
        call(15)
        emit(0x907E0230)
        for length, scale, denom, multiplier in ((0, 1, 1, 0), (3, 1, 1, 1), (5, 1, 1, 2), (5, 3, 1, 6), (5, 3, 2, 3)):
            for position in range(4):
                emit(
                    0x387E0238,
                    0x38800000 | length,
                    0x38A00000 | position,
                    0x3CC00001,
                    0x60C60000 | scale,
                    0x3CE00001,
                    0x60E70000 | denom,
                )
                call(14)
                emit(0x809E0230, 0x1C840000 | multiplier, 0x5484801E, 0x7C032000)
                failures.append(len(code))
                emit(0x40820000)
        for pointer, length, numerator, denominator in (
            (0x220, 3, 1, 1),
            (0, 5, 1, 1),
            (0x238, -1, 1, 1),
            (0x238, 5, 0, 1),
            (0x238, 5, 1, 0),
        ):
            emit(
                0x38600000 if not pointer else 0x387E0000 | pointer,
                0x38800000 | (length & 65535),
                0x38A00000,
                0x3CC00001,
                0x60C60000 | numerator,
                0x3CE00001,
                0x60E70000 | denominator,
            )
            call(14)
            check(0)
        emit(0x387E0238, 0x38800005, 0x38A00000, 0x3CC00001, 0x60C67FFF, 0x3CE00001, 0x60E70001)
        call(14)
        emit(0x3C807FFF, 0x6084FFFF, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        # A claimed guest span beyond emulated memory must not be scanned.
        emit(0x387E0238, 0x3C807FFF, 0x6084FFFF, 0x38A00000, 0x3CC00001, 0x60C60001, 0x3CE00001, 0x60E70001)
        call(14)
        check(0)
        emit(0x3860004D)
        call(15)
        emit(0x907E0258)
        for offset, factor in ((0, 0), (1, 1), (2, 2), (3, 2)):
            emit(
                0x387E0250,
                0x38800002,
                0x38A00000,
                0x38C00000 | offset,
                0x38E00000,
                0x39000000,
                0x3D200001,
                0x61290001,
                0x3D400001,
                0x614A0001,
            )
            call(19)
            emit(0x809E0258, 0x1C840000 | factor, 0x7C032000)
            failures.append(len(code))
            emit(0x40820000)
        # Slop belongs to spaces, including a leading space, in Roman runs.
        emit(
            0x387E0260,
            0x38800002,
            0x3CA00006,
            0x38C00001,
            0x38E00000,
            0x39000000,
            0x3D200001,
            0x61290001,
            0x3D400001,
            0x614A0001,
        )
        call(19)
        emit(0x809E0230, 0x38840006, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        for length, pixel, result, edge, remaining in (
            (0, "one", 0, 1, 1),
            (2, "leading", 0, 1, -1),
            (2, "trailing", 1, 0, -1),
            (2, "past", 2, 1, 1),
            (2, "negative", 0, 0, -1),
            (-1, "one", 0, 0, 1),
        ):
            emit(0x3860A55A, 0xB07E0268, 0x3C600001, 0x60630002, 0x90610038)
            emit(0x387E0250, 0x38800000 | (length & 65535), 0x38A00000)
            if pixel == "leading":
                emit(0x38C01000)
            elif pixel == "trailing":
                emit(0x80DE0258, 0x54C6801E, 0x38C6F000)
            elif pixel == "past":
                emit(0x80DE0258, 0x54C6881C, 0x3CC60001)
            elif pixel == "negative":
                emit(0x3CC0FFFF)
            else:
                emit(0x3CC00001)
            emit(0x38FE0268, 0x391E026C, 0x39200000, 0x3D400001, 0x614A0002)
            call(18)
            check(result)
            emit(0x887E0268)
            check(edge)
            emit(0x887E0269)
            check(0x5A)
            emit(0x807E026C)
            if remaining == -1:
                check(65535)
            else:
                emit(0x3C800001, 0x7C032000)
                failures.append(len(code))
                emit(0x40820000)
        emit(
            0x387E0400,
            0x38807FFF,
            0x38A00000,
            0x38C07FFF,
            0x38E00000,
            0x39000000,
            0x3D200001,
            0x61290001,
            0x3D400001,
            0x614A0001,
        )
        call(19)
        check(32767)  # Wide runs saturate instead of wrapping the caret coordinate.
    elif mode == "queue":
        call(8)
        check(0x14A)  # canonical live EventQueue, not a copied queue
    elif mode == "grow":
        emit(0x807E02C0)
        call(8)
        emit(0x80600118, 0x80630010, 0x809E02C0, 0x7C032000)
        failures.append(len(code))
        emit(0x40820000)
        emit(0x38600000)
        call(8)
        emit(0x80600118, 0x80630010)
        check(0)  # grow-zone callback installed and cleared in the current zone
    else:
        call(8)
        check(0)  # FontScript resolves to the existing Roman Script Manager
    emit(0x387E00C8, 0x38800031)
    call(3)
    check(42)  # 68K C function returns in D0
    # A PPC-to-PPC UniversalProc call must use the caller's stack even when
    # the application heap has too little room for another 256 KiB stack.
    emit(0x387E00E0)
    call(5)
    emit(0x3863FC00)
    call(6)
    emit(0x907E00E4, 0x2C030000)
    failures.append(len(code))
    emit(0x41820000)
    emit(0x903E00E8, 0x3C801234, 0x60845678, 0x90810004)
    emit(0x38600000, 0x907E0060, 0x807E02C0, 0x388000C0, 0x38BE0040)
    call(3)
    emit(0x807E0060)
    check(42)
    # A native File Manager completion must also work under heap pressure,
    # preserve the caller's zone and the suspended PPC stack chain.
    emit(0x80600118, 0x907E00EC, 0x38600000, 0x907E00A0, 0x387E0080)
    call(4)
    check(0xFFCD)
    emit(0x807E00A0)
    check(42)
    emit(0x80600118, 0x809E00EC, 0x7C032000)
    failures.append(len(code))
    emit(0x40820000)
    emit(0x807E00E4)
    call(7)
    emit(0x807E02C0)
    call(1)
    emit(0x83C10078, 0x80010084, 0x7C0803A6, 0x38210080, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)
    for branch in failures:
        code[branch] |= (bad - branch) * 4
    callback = len(code) * 4
    if mode == "recursive":
        # This callback recurses through an ordinary PPC UniversalProc call.
        # A controlled limit must unwind it before the native stack overflows.
        emit(0x9421FFC0, 0x7C5E1378, 0x807E02C0, 0x388000C0, 0x38BE0040)
        call(3)
        emit(0x4E800020)
    emit(0x38800001, 0x80A200E8, 0x2C050000)
    skip_stack = len(code)
    emit(0x41820000)
    stack_failures = []
    for words in [
        (0x80C10000, 0x7C062800),
        (0x80C50004, 0x3CE01234, 0x60E75678, 0x7C063800),
        (0x7026000F,),
    ]:
        emit(*words)
        stack_failures.append(len(code))
        emit(0x40820000)
    code[skip_stack] |= (len(code) - skip_stack) * 4
    emit(0x3880002A)
    for branch in stack_failures:
        code[branch] |= (len(code) - branch) * 4
    emit(0x90830020, 0x4E800020)
    data = bytearray(33792 if mode == "linebreak" else 1664)
    if mode == "linebreak":
        data[1024:33791] = b"M" * 32767
    data[256:512] = bytes([0xA5]) * 256
    if mode == "ot":
        data[1024:1576] = bytes([0xA5]) * 552
    data[512:515] = b"aBz"
    data[520:524] = b"\x03aBz"
    data[544:547] = b"abc"
    data[548:551] = b"a\rb"
    data[568:573] = b"a b c"
    data[592:594] = b"MM"
    data[608:610] = b" M"
    data[640:647] = b" a b   "
    if mode == "crm":
        struct.pack_into(">I", data, 1038, 1)
        for offset, kind in ((1064, 1), (1104, 2)):
            struct.pack_into(">2h", data, offset + 4, 9, 1)
            struct.pack_into(">I", data, offset + 14, kind)
    struct.pack_into(">II", data, 240, callback, 0)
    data[200:204] = bytes.fromhex("702a4e75")  # moveq #42,d0; rts
    app = pef(
        code,
        data,
        imports=[
            ("OTInetClientLib" if name == "OTInetGetInterfaceInfo" else "InterfaceLib", name, 2)
            for name in [
                "NewRoutineDescriptor",
                "DisposeRoutineDescriptor",
                "NMInstall",
                "CallUniversalProc",
                "PBGetEOFAsync",
                "MaxMem",
                "NewPtr",
                "DisposePtr",
                (
                    "GetEvQHdr"
                    if mode == "queue"
                    else "SetGrowZone" if mode == "grow" else "StyledLineBreak" if mode == "linebreak" else "FontScript"
                ),
                "FillParseTable",
                "UpperText",
                "LowerText",
                "CharacterType",
                "IUMagString" if mode == "intl" else "UpperString",
                "PortionLine",
                "CharWidth",
                "GetSysDirection",
                "SetSysDirection",
                "PixelToChar",
                "CharToPixel",
                "VisibleLength",
                "Pixel2Char",
                "Char2Pixel",
                "DrawJustified",
                "MoveTo",
                "GetPen",
                "GetPort",
                "SpaceExtra",
                "InitCRM",
                "CRMGetCRMVersion",
                "CRMGetHeader",
                "CRMInstall",
                "CRMSearch",
                "CRMRemove",
                "OTInetGetInterfaceInfo",
            ]
        ],
        main=0,
        reloc=[0x4600, 0xA000, 768, 0x4A22, 0xA000, 240, 0x4600],
    )
    write_fragment(destination, "PPC Callback Probe", app, 1)


if __name__ == "__main__":
    make_callback_probe(Path(sys.argv[1]), sys.argv[2] if len(sys.argv) > 2 else "valid")
