#!/usr/bin/env python3
# +===================================================================
#   File:      make_ppc_probe.py
#
#   Summary:   Create an original PPC PEF application exercising ROMAW's Toolbox ABI.
#
#   Classes:   None declared.
#
#   Functions: make_probe, write_fragment
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Create an original PPC PEF application exercising ROMAW's Toolbox ABI."""
import argparse
import struct
from pathlib import Path


def make_probe(destination, cached=False, l2=False):
    assert not l2 or cached
    name = "PPC L2 Cached Toolbox Probe" if l2 else "PPC Cached Toolbox Probe" if cached else "PPC Toolbox Probe"
    imports = [
        "SetRect",
        "OffsetRect",
        "NewPtr",
        "DisposePtr",
        "GetDateTime",
        "PtInRect",
    ]
    if cached:
        imports.append("MakeDataExecutable")
    if l2:
        imports.extend(("DebugStr", "BlockMove"))
    interface_count = len(imports)
    if l2:
        imports.extend(
            (
                "SetPt",
                "AddPt",
                "SubPt",
                "EqualPt",
                "DeltaPoint",
                "Pt2Rect",
                "InsetRect",
                "EmptyRect",
                "EqualRect",
                "SectRect",
                "UnionRect",
                "MapPt",
                "MapRect",
                "ScalePt",
                "BlockMoveData",
            )
        )
    carbon_count = len(imports)
    if l2:
        imports.extend(("memset", "memcpy", "memmove", "memcmp", "strlen", "strcmp"))
    rect = 0x100 if l2 else 64 if cached else 32
    message_offset = rect + 24
    code = []
    branches = []

    def emit(*words):
        code.extend(words)

    def call(index):
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800421)

    def spr(number, register, write=False):
        return (0x7C0003A6 if write else 0x7C0002A6) | register << 21 | (number & 31) << 16 | (number & 0x3E0) << 6

    def mapped_call(index, pointer_register, writable, expected_top=None, other_pointers=(), identity_pointer=None):
        # Call a registered Toolbox stub through a distinct instruction BAT alias.
        # Access the pointer argument through a separate data BAT alias.
        def high_nibble(dst, src):
            return 21 << 26 | src << 21 | dst << 16 | 3 << 1

        emit(0x9421FFA0, 0x7D0000A6, 0x91010030)  # Temporary frame, save MSR.
        for i in range(4):
            emit(spr(528 + i, 8), 0x91010034 + 4 * i)
        emit(0x819E0008 + index * 4, 0x800C0000, 0x804C0004)  # r0 = physical stub.
        emit(0x48000005, 0x7D2802A6, high_nibble(9, 9))  # Current code region.
        emit(0x61280002, spr(529, 8, True), 0x61281FFE, spr(528, 8, True))
        emit(high_nibble(10, 0), 0x61480002, spr(531, 8, True))
        emit(0x6D298000, 0x61281FFE, spr(530, 8, True))  # Alias region opposite PC.
        emit(0x5009013E, 0x7D2903A6)  # rlwimi r9,r0,0,4,31; mtctr r9
        emit(0x93410050, 0x93210054, 0x83410030)  # Preserve r26/MSR and r25/rectangle.
        for i in range(2):
            emit(spr(536 + i, 8), 0x91010044 + 4 * i)
        if identity_pointer is not None:
            for i in range(2):
                emit(spr(538 + i, 8), 0x91010020 + 4 * i)
            emit(high_nibble(10, identity_pointer), 0x61480002, spr(539, 8, True))
            emit(0x61481FFE, spr(538, 8, True))
        emit(high_nibble(10, pointer_register), 0x61480000 | (2 if writable else 1), spr(537, 8, True))
        emit(0x6C008000 | pointer_register << 21 | pointer_register << 16, high_nibble(10, pointer_register))
        emit(0x61481FFE, spr(536, 8, True))
        for reg in other_pointers:
            emit(0x6C008000 | reg << 21 | reg << 16)
        emit(0x7C190378 | pointer_register << 21 | pointer_register << 11)  # mr r25,pointer
        emit(0x63480030, 0x7D000124, 0x4C00012C, 0x4E800421)
        if expected_top is not None:
            emit(0xA9190000, 0x2C080000 | expected_top)
            branches.append(len(code))
            emit(0x40820000)
        emit(0x7F400124, 0x4C00012C, 0x83210054, 0x83410050)
        for i in range(2):
            emit(0x81010044 + 4 * i, spr(536 + i, 8, True))
        if identity_pointer is not None:
            for i in range(2):
                emit(0x81010020 + 4 * i, spr(538 + i, 8, True))
        for i in range(4):
            emit(0x81010034 + 4 * i, spr(528 + i, 8, True))
        emit(0x38210060)

    def check(value, equal=False):
        emit(0x2C030000 | (value & 0xFFFF))
        branches.append(len(code))
        emit(0x41820000 if equal else 0x40820000)

    def constant(register, value):
        emit(0x3C000000 | register << 21 | (value >> 16))
        emit(0x60000000 | register << 21 | register << 16 | (value & 0xFFFF))

    def packed_point(register, vertical, horizontal):
        constant(register, (vertical & 0xFFFF) << 16 | (horizontal & 0xFFFF))

    def check_halfwords(offset, *values):
        for index, value in enumerate(values):
            emit(0xA87E0000 | (offset + 2 * index))
            check(value)

    def check_aliased_pointer(offset):
        emit(0x389E0000 | offset, 0x6C848000, 0x7C032000)
        branches.append(len(code))
        emit(0x40820000)

    emit(0x9421FFC0, 0x7C0802A6, 0x90010044, 0x93C10038, 0x7C5E1378)
    if cached:
        # Preserve r29/HID0; enable and flash both L1 banks before native Toolbox calls.
        emit(0x93810030, 0x93A10034, 0x7FB0FAA6)
        if l2:

            def l2cr(register, write=False):
                return (0x7C0003A6 if write else 0x7C0002A6) | register << 21 | (1017 & 31) << 16 | (1017 & 0x3E0) << 6

            emit(0x9361002C, l2cr(27), 0x3860FFFF, 0x38800000)
            call(6)  # Publish any pre-existing dirty cache state before disabling L2.
            emit(0x7C0004AC, 0x3C603900, l2cr(3, True), 0x7C0004AC, 0x64630020, l2cr(3, True), 0x3B807FFF)
            waiting = len(code)
            emit(l2cr(3), 0x70630001)
            completed = len(code)
            emit(0x41820000, 0x379CFFFF)
            emit(0x40820000 | (((waiting - len(code)) * 4) & 0xFFFF))
            branches.append(len(code))
            emit(0x48000000)  # Bounded invalidation timeout.
            code[completed] |= (len(code) - completed) * 4
            emit(0x3C60F900, l2cr(3, True))
        emit(0x38600000, 0x6063CC00, 0x7C70FBA6, 0x4C00012C)
    emit(0x387E0000 | rect, 0x3880000A, 0x38A00014, 0x38C0006E, 0x38E00078)
    if l2:
        mapped_call(0, 3, True, expected_top=20)
    else:
        call(0)
    if cached:
        emit(0x38600015, 0xB07E0000 | rect)  # Dirty top=21 must reach host RAM before OffsetRect reads it.
    emit(0x387E0000 | rect, 0x38800005, 0x38A00007)
    if l2:
        mapped_call(1, 3, True, expected_top=28)
    else:
        call(1)
    emit(0xA87E0000 | rect)
    check(28 if cached else 27)
    emit(0xA87E0000 | (rect + 6))
    check(115)
    emit(0x3C60001C, 0x60630010, 0x389E0000 | rect)
    if l2:
        mapped_call(5, 4, False)
    else:
        call(5)
    check(1)
    if l2:
        emit(0x3C60001C, 0x60630073, 0x389E0000 | rect)  # x == right is outside.
        mapped_call(5, 4, False)
        check(0)
        emit(0x387E0000 | message_offset)
        mapped_call(7, 3, False)
    emit(0x38600040)
    call(2)
    check(0, equal=True)
    call(3)
    emit(0x387E0000 | (rect + 8))
    call(4)
    emit(0x807E0000 | (rect + 8))
    check(0, equal=True)
    if l2:
        emit(0x7C7C1B78, 0x387E0000 | (rect + 12))  # Preserve the reference time in r28.
        mapped_call(4, 3, True)
        emit(0x807E0000 | (rect + 12), 0x7C7C1850, 0x2803000A)  # Unsigned elapsed seconds <= 10.
        branches.append(len(code))
        emit(0x41810000)
        # Record calls share a data BAT alias. Read-only inputs and an aliased
        # SectRect destination exercise the same geometry handlers.
        emit(0x387E00A0, 0x3880FFFA, 0x38A00003)
        mapped_call(imports.index("SetPt"), 3, True)
        check_halfwords(0xA0, 3, -6)
        packed_point(3, 2, 4)
        emit(0x389E00A0)
        mapped_call(imports.index("AddPt"), 4, True)
        check_halfwords(0xA0, 5, -2)
        packed_point(3, 1, -3)
        emit(0x389E00A0)
        mapped_call(imports.index("SubPt"), 4, True)
        check_halfwords(0xA0, 4, 1)
        packed_point(3, 5, -2)
        packed_point(4, 5, -2)
        emit(0x38DE00A0)
        mapped_call(imports.index("EqualPt"), 6, False)
        check(1)
        packed_point(3, 5, 8)
        packed_point(4, 5, 4)
        emit(0x38DE00A0)
        mapped_call(imports.index("DeltaPoint"), 6, False)
        check(4)

        packed_point(3, 9, 2)
        packed_point(4, 3, -6)
        emit(0x38BE00B8)
        mapped_call(imports.index("Pt2Rect"), 5, True)
        check_halfwords(0xB8, 3, -6, 9, 2)
        emit(0x387E00B8, 0x38800001, 0x38A00002)
        mapped_call(imports.index("InsetRect"), 3, True)
        check_halfwords(0xB8, 5, -5, 7, 1)
        emit(0x387E00B8)
        mapped_call(imports.index("EmptyRect"), 3, False)
        check(0)

        emit(0x387E00A8, 0x389E00B0, 0x38BE00B8)
        mapped_call(imports.index("UnionRect"), 3, True, other_pointers=(4, 5))
        check_halfwords(0xB8, 10, -4, 30, 14)
        emit(0x387E00B8, 0x389E00C0)
        mapped_call(imports.index("EqualRect"), 3, False, other_pointers=(4,))
        check(1)
        emit(0x387E00A8, 0x389E00B0, 0x38BE00A8)
        mapped_call(imports.index("SectRect"), 3, True, other_pointers=(4, 5))
        check(1)
        check_halfwords(0xA8, 15, 0, 22, 8)

        for symbol, offset, expected in (
            ("MapPt", 0xA0, (108, 202)),
            ("MapRect", 0xD8, (102, 204, 108, 212)),
            ("ScalePt", 0xE0, (6, 8)),
        ):
            emit(0x387E0000 | offset, 0x389E00C8, 0x38BE00D0)
            mapped_call(imports.index(symbol), 3, True, other_pointers=(4, 5))
            check_halfwords(offset, *expected)

        # Unaligned writes, untouched guards and a returned effective pointer.
        emit(0x387E0181, 0x388001A5, 0x38A0000D)
        mapped_call(imports.index("memset"), 3, True)
        check_aliased_pointer(0x181)
        check_halfwords(0x180, 0x00A5)
        emit(0x887E018D)
        check(0xA5)
        emit(0x887E018E)
        check(0)
        emit(0x387E0184, 0x389E0140, 0x38A00011)
        mapped_call(imports.index("memcpy"), 3, True, other_pointers=(4,))
        check_aliased_pointer(0x184)
        check_halfwords(0x184, 0x3031, 0x3233, 0x3435)
        emit(0x387E0184, 0x389E0140, 0x38A00011)
        mapped_call(imports.index("memcmp"), 3, False, other_pointers=(4,))
        check(0)

        # Destination and source use disjoint effective ranges that overlap in RAM.
        emit(0x387E0185, 0x389E0184, 0x38A00010)
        mapped_call(imports.index("memmove"), 3, True, identity_pointer=4)
        check_aliased_pointer(0x185)
        check_halfwords(0x184, 0x3030, 0x3132, 0x3334)
        emit(0x887E0194)
        check(ord("f"))
        emit(0x387E0140)
        mapped_call(imports.index("strlen"), 3, False)
        check(17)
        for symbol, size, result in (("memcmp", 13, 0), ("memcmp", 14, 12), ("strcmp", 0, 12)):
            emit(0x387E0140, 0x389E0160, 0x38A00000 | size)
            mapped_call(imports.index(symbol), 3, False, other_pointers=(4,))
            check(result)
        for symbol, result in (("memcpy", -1), ("memmove", -1), ("memset", -1), ("memcmp", 0)):
            emit(0x3860FFFF, 0x3880FFFF, 0x38A00000, 0x38DE0140)
            mapped_call(imports.index(symbol), 6, False)
            check(result)

        # Cross a page boundary and exceed the stack-backed copy snapshot.
        emit(0x38602000)
        call(2)
        check(0, equal=True)
        emit(0x7C7C1B78)  # Save the allocation for DisposePtr.

        def before_page(register, delta=0):
            emit(21 << 26 | 28 << 21 | register << 16 | 19 << 1)
            emit(14 << 26 | register << 21 | register << 16 | (4091 + delta))

        before_page(3)
        emit(0x388000A5, 0x38A00180)
        mapped_call(imports.index("memset"), 3, True)
        before_page(4)
        emit(0x38600015, 0x98640000, 0x38600026, 0x98640008)
        before_page(3, 1)
        emit(0x38A0017F)
        mapped_call(imports.index("memmove"), 3, True, other_pointers=(4,))
        before_page(4)
        for offset, value in ((0, 0x15), (1, 0x15), (5, 0xA5), (9, 0x26), (0x17F, 0xA5)):
            emit(0x88640000 | offset)
            check(value)
        emit(0x7F83E378)  # mr r3,r28
        call(3)

        # Repair a protection fault from the read-only OffsetRect destination
        # and retry at the aliased stub. The rectangle must be offset once.
        def publish_vector():
            emit(0x39000300, 0x7C0040AC, 0x7C0004AC, 0x7C0047AC, 0x4C00012C)

        def check_store_fault(offset):
            emit(0x7D635B78)  # mr r3,r11
            check(1)
            emit(spr(19, 3))
            check_aliased_pointer(offset)
            emit(spr(18, 3))
            constant(4, 0x0A000000)
            emit(0x7C032000)
            branches.append(len(code))
            emit(0x40820000)

        handler = (0x396B0001, spr(537, 12), 0x698C0003, spr(537, 12, True), 0x4C000064)
        for index, word in enumerate(handler):
            vector = 0x300 + 4 * index
            emit(0x81000000 | vector, 0x91010008 + 4 * index)
            constant(9, word)
            emit(0x91200000 | vector)
        publish_vector()
        emit(0x39600000, 0x387E0000 | rect, 0x38800000, 0x38A00001)
        mapped_call(1, 3, False)
        check_halfwords(rect, 29, 15, 128, 115)
        check_store_fault(rect)

        # The same handler repairs a protected memmove through two aliases.
        emit(0x39600000, 0x387E0141, 0x389E0140, 0x38A00010)
        mapped_call(imports.index("memmove"), 3, False, identity_pointer=4)
        for index in range(len(handler)):
            emit(0x81010008 + 4 * index, 0x91000300 + 4 * index)
        publish_vector()
        check_aliased_pointer(0x141)
        check_halfwords(0x140, 0x3030, 0x3132, 0x3334)
        check_store_fault(0x141)

        # Toolbox copies take source first and interpret their count as a signed Size.
        for symbol in ("BlockMove", "BlockMoveData"):
            index = imports.index(symbol)
            constant(8, 0x41424344)
            emit(0x911E0180, 0x387E0180, 0x389E0181, 0x38A00003)
            mapped_call(index, 4, True, identity_pointer=3)
            check(0)
            check_halfwords(0x180, 0x4141, 0x4243)
            emit(0x387E0180, 0x38800220, 0x38A00002)
            mapped_call(index, 3, False, identity_pointer=4)
            check(0)
            emit(0xA8600220)
            check(0)
            for count in (0, -1):
                emit(0x3900FF94, 0xB1000220, 0x3860FFFF, 0x3880FFFF, 0x38A00000 | (count & 0xFFFF), 0x38DE0180)
                mapped_call(index, 6, False)
                check(0)
                emit(0xA8600220)
                check(0)
    if cached:
        for value in (17, 29):
            emit(0x3C603860, 0x60630000 | value, 0x907E0080)
            if l2 and value == 29:
                emit(0x399E0080, 0x7D8903A6, 0x4E800421)
                check(17)  # The old instructions remain visible until publication.
            emit(0x387E0080, 0x38800014)
            if l2:
                mapped_call(6, 3, False)
            else:
                call(6)
            emit(0x3B800020)
            loop = len(code)
            emit(0x399E0080, 0x7D8903A6, 0x4E800421)
            check(value)
            emit(0x379CFFFF)
            emit(0x40820000 | (((loop - len(code)) * 4) & 0xFFFF))
        emit(0x3860FFFF, 0x38800000)  # An empty publication range does not dereference its pointer.
        if l2:
            emit(0x38BE0080)
            mapped_call(6, 5, False)
            emit(l2cr(27, True), 0x8361002C)  # Final empty publication fenced both cache levels.
        else:
            call(6)
        emit(0x7FB0FBA6, 0x4C00012C, 0x83A10034, 0x83810030)  # Restore HID0, r29 and r28.
    emit(0x83C10038, 0x80010044, 0x7C0803A6, 0x38210040, 0x38600000, 0x4E800020)
    bad = len(code)
    emit(0)  # Any failed assertion is a controlled illegal instruction.
    for index in branches:
        code[index] |= (bad - index) * 4
    code = b"".join(struct.pack(">I", w) for w in code)
    data = bytearray(0x1A0 if l2 else 160 if cached else 64)
    if cached:
        data[128:148] = struct.pack(">5I", 0x38600000, 0x38630000, 0x38630000, 0x38630000, 0x4E800020)
    if l2:
        message = b"750 translated Pascal string"
        data[message_offset : message_offset + 1 + len(message)] = bytes([len(message)]) + message
        struct.pack_into(">20h", data, 0xA8, 10, -4, 22, 8, 15, 0, 30, 14, 0, 0, 0, 0, 10, -4, 30, 14, 0, 0, 10, 20)
        struct.pack_into(">10h", data, 0xD0, 100, 200, 120, 240, 1, 2, 4, 6, 3, 4)
        data[0x140:0x152] = b"0123456789abcdefg\0"
        data[0x160:0x172] = b"0123456789abcXefg\0"
    libraries = [("InterfaceLib", imports[:interface_count])]
    if l2:
        libraries.extend((("CarbonLib", imports[interface_count:carbon_count]), ("StdCLib", imports[carbon_count:])))
    strings = bytearray()
    library_records = bytearray()
    symbols = []
    for library, names in libraries:
        library_records += struct.pack(">6I", len(strings), 0, 0, len(names), len(symbols), 0)
        strings += library.encode("ascii") + b"\0"
        for symbol in names:
            symbols.append(0x02000000 | len(strings))
            strings += symbol.encode("ascii") + b"\0"
    relocation_offset = 56 + len(library_records) + 4 * len(symbols) + 12
    loader = struct.pack(
        ">14I",
        1,
        0,
        0xFFFFFFFF,
        0,
        0xFFFFFFFF,
        0,
        len(libraries),
        len(imports),
        1,
        relocation_offset,
        relocation_offset + 4,
        relocation_offset + 4 + len(strings),
        0,
        0,
    )
    loader += library_records
    loader += b"".join(struct.pack(">I", s) for s in symbols)
    loader += struct.pack(">HHII", 1, 0, 2, 0)
    loader += struct.pack(">HH", 0x4600, 0x4A00 | (len(imports) - 1)) + strings
    code_offset = (128 + len(loader) + 15) & ~15
    data_offset = (code_offset + len(code) + 15) & ~15
    header = b"Joy!peffpwpc" + struct.pack(">5I", 1, 0, 0, 0, 0)
    header += struct.pack(">HHI", 3, 2, 0)
    sections = b""
    for payload, offset, kind in [
        (code, code_offset, 0),
        (data, data_offset, 1),
        (loader, 128, 4),
    ]:
        sections += struct.pack(
            ">6I4B",
            0xFFFFFFFF,
            0,
            len(payload),
            len(payload),
            len(payload),
            offset,
            kind,
            0,
            4,
            0,
        )
    pef = bytearray(data_offset + len(data))
    pef[:40] = header
    pef[40:124] = sections
    pef[128 : 128 + len(loader)] = loader
    pef[code_offset : code_offset + len(code)] = code
    pef[data_offset:] = data

    write_fragment(destination, name, pef, 1)


def write_fragment(
    destination,
    name,
    pef,
    kind,
    extra_resources=(),
    arch=b"pwpc",
    location=1,
    include_cfrg=True,
    file_name=None,
):
    fragment = arch + bytes(12) + struct.pack(">IhBBII", 0x40000, 0, kind, location, 0, len(pef))
    fragment += bytes(8)
    record_length = (43 + len(name) + 3) & ~3
    fragment += struct.pack(">H", record_length) + bytes([len(name)]) + name.encode()
    fragment += bytes(record_length - len(fragment))
    # Apple's cfrg resource format version is 1 (not the PEF format version).
    cfrg = bytes(8) + struct.pack(">I", 1) + bytes(16)
    cfrg += struct.pack(">I", 1) + fragment
    size = struct.pack(">HII", 0x80, 4 * 1024 * 1024, 4 * 1024 * 1024)
    resources = [(b"cfrg", 0, cfrg)] if include_cfrg else []
    resources += [(b"SIZE", -1, size)] + list(extra_resources)
    resource_data = b""
    types = b""
    refs = b""
    groups = {}
    for resource_type, identifier, payload in resources:
        groups.setdefault(resource_type, []).append((identifier, payload))
    for resource_type, entries in groups.items():
        types += resource_type + struct.pack(">HH", len(entries) - 1, 2 + 8 * len(groups) + len(refs))
        for identifier, payload in entries:
            refs += struct.pack(">hH", identifier, 0xFFFF) + len(resource_data).to_bytes(4, "big") + bytes(4)
            resource_data += struct.pack(">I", len(payload)) + payload
    type_list = struct.pack(">H", len(groups) - 1) + types + refs
    resource_header = struct.pack(">4I", 256, 256 + len(resource_data), len(resource_data), 28 + len(type_list))
    resource_map = resource_header + bytes(8) + struct.pack(">HH", 28, 28 + len(type_list)) + type_list
    fork = resource_header + bytes(240) + resource_data + resource_map
    finder = (b"APPL" if kind else b"shlb") + b"PPRB" + bytes(24)
    sidecar = struct.pack(">II16sH", 0x00051607, 0x00020000, bytes(16), 2)
    sidecar += struct.pack(">III", 2, 50, len(fork))
    sidecar += struct.pack(">III", 9, 50 + len(fork), len(finder)) + fork + finder
    destination.mkdir(parents=True, exist_ok=True)
    (destination / ".AppleDouble").mkdir(exist_ok=True)
    file_name = name if file_name is None else file_name
    (destination / file_name).write_bytes(pef)
    (destination / ".AppleDouble" / file_name).write_bytes(sidecar)
    print(destination / file_name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--l2", action="store_true", help="Also generate the strict 750 L2 variant")
    arguments = parser.parse_args()
    make_probe(arguments.destination)
    make_probe(arguments.destination, cached=True)
    if arguments.l2:
        make_probe(arguments.destination, cached=True, l2=True)
