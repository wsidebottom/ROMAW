#!/usr/bin/env python3
# +===================================================================
#   File:      native_code_image.py
#
#   Summary:   Verify and extract closed relocatable native code from PE and ELF link containers.
#
#   Classes:   None.
#
#   Functions: extract_elf, extract_image.
#
#   Origin:    Shared from ARDI Executor's native PowerPC memory kernel verifier.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

import struct


def extract_elf(raw, symbols, roots):
    """Retain native SysV ABI without converting ELF relocation encodings."""
    if len(raw) < 64 or raw[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", raw, 18)[0] != 62:
        raise ValueError("invalid native ELF kernel ABI")
    section_offset = struct.unpack_from("<Q", raw, 40)[0]
    section_size, count = struct.unpack_from("<HH", raw, 58)
    sections = []
    for index in range(count):
        values = struct.unpack_from("<IIQQQQIIQQ", raw, section_offset + index * section_size)
        _, kind, flags, address, offset, size, link, info, _, entry_size = values
        if kind != 8 and offset + size > len(raw):
            raise ValueError("truncated ELF kernel section")
        sections.append((kind, flags, address, size, raw[offset : offset + size], info, entry_size))
    selected = []
    for section in sections:
        kind, flags, address, size, data, _, _ = section
        if not flags & 2 or not size or kind == 7:
            continue
        if flags & 1 or kind not in (1,):
            raise ValueError("ELF kernel contains writable or unsupported allocated section")
        selected.append(section)
    length = max(address + size for _, _, address, size, _, _, _ in selected)
    if not 16 <= length <= 1024 * 1024:
        raise ValueError("invalid ELF kernel extent")
    code = bytearray(length)
    for _, _, address, size, data, _, _ in selected:
        code[address : address + size] = data

    def contains(at, size, executable=False):
        return any(
            address <= at and at + size <= address + length and (not executable or flags & 4)
            for _, flags, address, length, _, _, _ in selected
        )

    width = 8
    fixups, seen = [], set()
    for kind, _, _, size, data, target_index, entry_size in sections:
        if kind not in (4, 9) or not sections[target_index][1] & 2:
            continue
        expected_size = 24 if kind == 4 else 16
        if entry_size != expected_size or size % entry_size:
            raise ValueError("invalid ELF kernel relocation extent")
        for at in range(0, size, entry_size):
            site, info = struct.unpack_from("<QQ", data, at)
            relocation = info & 0xFFFFFFFF
            if relocation in (2, 4):
                # The final link has resolved PC-relative displacements, which
                # are unchanged when the entire kernel moves together.
                if not contains(site, 4):
                    raise ValueError("invalid ELF relative relocation")
                displacement = struct.unpack_from("<i", code, site)[0]
                if not contains(site + 4 + displacement, 1):
                    raise ValueError("ELF relative reference targets outside its code/data")
                continue
            if relocation != 1 or not contains(site, width) or site in seen:
                raise ValueError(f"unsupported ELF kernel relocation {relocation}")
            target = struct.unpack_from("<Q", code, site)[0]
            if not contains(target, 1):
                raise ValueError("ELF kernel pointer targets outside its code/data")
            seen.add(site)
            fixups.append((site, target))
    entries = [symbols[name] for name in roots]
    if any(not contains(entry, 4, executable=True) for entry in entries):
        raise ValueError("ELF kernel entry outside executable code")
    return code, fixups, entries


def extract_image(raw, symbols, roots):
    """Use the PE linker solely as an ABI-independent relocation container."""
    if raw[:2] != b"MZ":
        raise ValueError("native kernel container has no DOS signature")
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[pe : pe + 4] != b"PE\0\0":
        raise ValueError("native kernel container has no PE signature")
    machine, count = struct.unpack_from("<HH", raw, pe + 4)
    optional = pe + 24
    optional_size = struct.unpack_from("<H", raw, pe + 20)[0]
    if machine != 0x8664 or struct.unpack_from("<H", raw, optional)[0] != 0x20B:
        raise ValueError("native kernel ABI width mismatch")
    if struct.unpack_from("<Q", raw, optional + 24)[0]:
        raise ValueError("native kernel image base must be zero")
    directories = optional + 112
    sections = []
    for index in range(count):
        at = optional + optional_size + index * 40
        name = raw[at : at + 8].split(b"\0")[0].decode("ascii")
        virtual_size, rva, size, offset = struct.unpack_from("<IIII", raw, at + 8)
        flags = struct.unpack_from("<I", raw, at + 36)[0]
        if offset + size > len(raw):
            raise ValueError("truncated native kernel section")
        sections.append((name, rva, max(size, virtual_size), raw[offset : offset + size], flags))

    def section_bytes(rva, size):
        for _, start, _, data, _ in sections:
            if start <= rva and rva + size <= start + len(data):
                return data[rva - start : rva - start + size]
        raise ValueError("native kernel directory outside initialized sections")

    # GNU PE ld emits an empty import descriptor even with -nostdlib. An
    # actual descriptor, import address table or delay import is forbidden.
    for index in (1, 12, 13):
        rva, size = struct.unpack_from("<II", raw, directories + index * 8)
        if rva and any(section_bytes(rva, size)):
            raise ValueError("native kernel imports a runtime helper")
    selected = []
    for name, rva, length, data, flags in sections:
        if name in (".reloc", ".idata"):
            continue
        if flags & 0x80000000 or name in (".pdata", ".xdata", ".eh_fram"):
            raise ValueError(f"native kernel contains writable or unwind section {name}")
        if not flags & 0x40000000:
            raise ValueError(f"native kernel contains unreadable section {name}")
        selected.append((name, rva, length, data, flags))
    length = max(rva + size for _, rva, size, _, _ in selected)
    if not 16 <= length <= 1024 * 1024:
        raise ValueError(f"invalid native kernel size {length}")
    ordered = sorted(selected, key=lambda section: section[1])
    for previous, current in zip(ordered, ordered[1:]):
        if previous[1] + previous[2] > current[1]:
            raise ValueError("overlapping native kernel sections")
    code = bytearray(length)
    for _, rva, _, data, _ in selected:
        code[rva : rva + len(data)] = data

    def contains(address, size, executable=False):
        return any(
            start <= address and address + size <= start + length and (not executable or flags & 0x20000000)
            for _, start, length, _, flags in selected
        )

    rva, size = struct.unpack_from("<II", raw, directories + 5 * 8)
    relocations = section_bytes(rva, size) if size else b""
    offset, fixups, seen = 0, [], set()
    width = 8
    while offset < len(relocations):
        page, block_size = struct.unpack_from("<II", relocations, offset)
        if block_size < 8 or block_size % 2 or offset + block_size > len(relocations):
            raise ValueError("invalid native kernel relocation block")
        for at in range(offset + 8, offset + block_size, 2):
            record = struct.unpack_from("<H", relocations, at)[0]
            kind = record >> 12
            if not kind:
                continue
            site = page + (record & 4095)
            if kind != 10 or not contains(site, width) or site in seen:
                raise ValueError("invalid native kernel pointer relocation")
            target = struct.unpack_from("<Q", code, site)[0]
            if not contains(target, 1):
                raise ValueError("native kernel pointer targets outside its code/data")
            seen.add(site)
            fixups.append((site, target))
        offset += block_size
    entries = []
    for name in roots:
        entry = symbols[name]
        if not contains(entry, 4, executable=True):
            raise ValueError(f"native kernel root {name} is not executable")
        entries.append(entry)
    return code, fixups, entries
