# +===================================================================
#   File:      patch-kidpix-studio.py
#
#   Summary:   Apply guarded local Quit and large-volume fixes to Kid Pix Studio apps.
#
#   Classes:   None.
#
#   Functions: records, replace_resource, patch, main (script entry point).
#
#   Origin:    Local analysis of the user's installed 68K Studio applications;
#              arithmetic helper uses the documented 68020 unsigned multiply.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
#              Application binaries are user-supplied and are not distributed here.
# ===================================================================+

"""Patch local AppleDouble companions, retaining original bytes in hashed backups."""

import argparse
import hashlib
from pathlib import Path
import struct

from mac_resources import AppleDouble, extent, unpack


APPS = {
    "Digital Puppets": (1, 0x2912),
    "Moopies": (1, 0x31A8),
    "Stampimator": (1, 0x2B04),
    "SlideShow": (2, 0x2CA4),
}
# Multiply the real free-block count by the real allocation-block size in
# D1:D0, then saturate to the signed byte count expected by SlideShow.
FREE_SPACE_HELPER = bytes.fromhex("4c2e0401ffb6 4a81 6604 4a80 6a06 203c7fffffff 4e75")


def records(fork):
    """Retain duplicate localized resources by identifying their map references."""
    data_offset, map_offset, data_size, map_size = unpack(">IIII", fork)
    extent(fork, data_offset, data_size)
    resource_map = extent(fork, map_offset, map_size)
    type_offset = unpack(">H", resource_map, 24)[0]
    count = unpack(">H", resource_map, type_offset)[0]
    result = {}
    for i in range(0 if count == 0xFFFF else count + 1):
        kind, count, refs = unpack(">4sHH", resource_map, type_offset + 2 + i * 8)
        for j in range(count + 1):
            reference = type_offset + refs + j * 12
            rid, _, attributes_offset = unpack(">hHI", resource_map, reference)
            relative = attributes_offset & 0xFFFFFF
            length = unpack(">I", fork, data_offset + relative)[0]
            if relative + 4 + length > data_size:
                raise ValueError("Resource exceeds its data area")
            payload_offset = data_offset + relative + 4
            result[kind, rid, reference] = (payload_offset, extent(fork, payload_offset, length))
    return result


def replace_resource(fork, key, payload):
    """Append one changed resource, preserving every other resource and map record."""
    data_offset, map_offset, _, map_size = unpack(">IIII", fork)
    relative = map_offset - data_offset
    if relative > 0xFFFFFF:
        raise ValueError("Resource offset exceeds 24 bits")
    addition = struct.pack(">I", len(payload)) + payload
    result = bytearray(fork[:map_offset] + addition + fork[map_offset:])
    updated_map = map_offset + len(addition)
    reference = updated_map + key[2] + 4
    attributes_offset = unpack(">I", result, reference)[0]
    struct.pack_into(">I", result, reference, (attributes_offset & 0xFF000000) | relative)
    header = struct.pack(">IIII", data_offset, updated_map, relative + len(addition), map_size)
    result[:16] = result[updated_map : updated_map + 16] = header
    return bytes(result)


def patch(name, original):
    double = AppleDouble(original)
    old_fork = double.get(2)
    fork = bytearray(old_fork)
    original_records = records(old_fork)
    expected = {}
    code_id, launch = APPS[name]
    code_keys = [key for key in original_records if key[:2] == (b"CODE", code_id)]
    if len(code_keys) != 1:
        raise ValueError(f"{name}: expected one launch segment")
    key = code_keys[0]
    offset, code = original_records[key]
    prefix = bytes.fromhex("41eeff8e" if name == "SlideShow" else "41eeffd4")
    if code[launch - 4 : launch] != prefix or code[launch : launch + 2] not in (b"\xa9\xf2", b"\x70\x00"):
        raise ValueError(f"{name}: launch signature differs")
    updated = code[:launch] + b"\x70\x00" + code[launch + 2 :]
    fork[offset : offset + len(code)] = updated
    expected[key] = updated
    menus = 0
    for key, (offset, data) in original_records.items():
        if key[0] != b"MENU":
            continue
        old = b"\x10Return to Picker1Q\x00\x00\x00"
        new = b"\x04Quit1Q\x00\x00\x00" + bytes(12)
        if data.count(old) == 1:
            data = data.replace(old, new)
        elif new not in data:
            continue
        menus += 1
        expected[key] = data
        fork[offset : offset + len(data)] = data
    if menus != 1:
        raise ValueError(f"{name}: expected one File menu")
    if name == "SlideShow":
        keys = [key for key in original_records if key[:2] == (b"CODE", 3)]
        if len(keys) != 1:
            raise ValueError("SlideShow: expected one free-space segment")
        key = keys[0]
        _, data = original_records[key]
        if data[0x1806:0x180C] == bytes.fromhex("4c2e0000ffb6"):
            if data[:4] != bytes.fromhex("04c0006c") or len(data) != 30938:
                raise ValueError("SlideShow: segment layout differs")
            code = bytearray(data + FREE_SPACE_HELPER)
            code[0x1806:0x180C] = b"\x4e\xba" + struct.pack(">h", len(data) - 0x1808) + b"\x4e\x71"
            expected[key] = bytes(code)
            fork = replace_resource(bytes(fork), key, bytes(code))
        elif not data.endswith(FREE_SPACE_HELPER) or data[0x1806:0x1808] != b"\x4e\xba":
            raise ValueError("SlideShow: free-space signature differs")
    check = records(fork)
    if check.keys() != original_records.keys():
        raise ValueError("Resource inventory changed")
    for key, (_, before) in original_records.items():
        if check[key][1] != expected.get(key, before):
            raise ValueError(f"Unexpected resource change: {key}")
    if bytes(fork) == old_fork:
        return original
    updated = double.replace(2, bytes(fork))
    check_double = AppleDouble(updated)
    if any(double.get(kind) != check_double.get(kind) for kind in double.entries if kind != 2):
        raise ValueError("Unrelated AppleDouble metadata changed")
    return updated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Installed Studio Stuff directory")
    parser.add_argument("--apply", action="store_true", help="Write the verified changes")
    parser.add_argument(
        "--backups", type=Path, default=Path(__file__).resolve().parents[1] / "build/kidpix-quit/backups"
    )
    args = parser.parse_args()
    plans = []
    for name in APPS:
        path = args.directory / ("._" + name)
        original = path.read_bytes()
        updated = patch(name, original)
        if updated != original:
            plans.append((path, original, updated))
        print(f"{name}: {'ready to patch' if updated != original else 'already patched'}")
    if args.apply:
        args.backups.mkdir(parents=True, exist_ok=True)
        for path, original, updated in plans:
            digest = hashlib.sha256(original).hexdigest()
            backup = args.backups / f"{path.name[2:]}.{digest}.appledouble"
            if backup.exists() and backup.read_bytes() != original:
                raise ValueError(f"Conflicting backup: {backup}")
            backup.write_bytes(original)
            if path.read_bytes() != original:
                raise ValueError(f"Application changed while preparing patch: {path}")
            path.write_bytes(updated)


if __name__ == "__main__":
    main()
