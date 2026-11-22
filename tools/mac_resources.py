# +===================================================================
#   File:      mac_resources.py
#
#   Summary:   Bounded AppleDouble/resource-fork access, preserving unrelated metadata.
#
#   Classes:   AppleDouble, ResourceFork
#
#   Functions: extent, unpack
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Bounded AppleDouble/resource-fork access, preserving unrelated metadata."""

import struct


def extent(data, offset, length):
    if offset < 0 or length < 0 or offset + length > len(data):
        raise ValueError("Truncated file extent")
    return data[offset : offset + length]


def unpack(fmt, data, offset=0):
    return struct.unpack(fmt, extent(data, offset, struct.calcsize(fmt)))


class AppleDouble:
    def __init__(self, data):
        self.data = data
        magic, version = unpack(">II", data)
        if (magic, version) != (0x00051607, 0x00020000):
            raise ValueError("Expected AppleDouble version 2")
        count = unpack(">H", data, 24)[0]
        extent(data, 26, count * 12)
        self.entries = {}
        for i in range(count):
            kind, offset, size = unpack(">III", data, 26 + i * 12)
            if kind in self.entries or offset < 26 + count * 12:
                raise ValueError("Invalid AppleDouble descriptor")
            if size:
                extent(data, offset, size)
            self.entries[kind] = (offset, size, i)

    def get(self, kind):
        offset, size, _ = self.entries[kind]
        return extent(self.data, offset, size) if size else b""

    def replace(self, kind, payload):
        # Append the replacement so every other descriptor/payload stays intact.
        _, _, index = self.entries[kind]
        result = bytearray(self.data)
        struct.pack_into(">II", result, 26 + index * 12 + 4, len(result), len(payload))
        result.extend(payload)
        return bytes(result)


class ResourceFork:
    def __init__(self, data):
        self.data = data
        self.data_offset, self.map_offset, data_size, map_size = unpack(">IIII", data)
        extent(data, self.data_offset, data_size)
        self.map = extent(data, self.map_offset, map_size)
        self.type_offset = unpack(">H", self.map, 24)[0]
        count = unpack(">H", self.map, self.type_offset)[0]
        self.records = {}
        for i in range(0 if count == 0xFFFF else count + 1):
            kind, count_minus_one, refs = unpack(">4sHH", self.map, self.type_offset + 2 + i * 8)
            for j in range(count_minus_one + 1):
                reference = self.type_offset + refs + j * 12
                rid, _, attributes_offset = unpack(">hHI", self.map, reference)
                offset = attributes_offset & 0xFFFFFF
                length = unpack(">I", data, self.data_offset + offset)[0]
                if offset + 4 + length > data_size:
                    raise ValueError("Resource exceeds data area")
                payload = extent(data, self.data_offset + offset + 4, length)
                key = (kind, rid)
                if key in self.records:
                    raise ValueError("Duplicate resource")
                self.records[key] = (payload, attributes_offset >> 24, reference)

    def get(self, kind, rid):
        return self.records[(kind, rid)][0]

    def rebuild(self, replacements, attributes_override=None):
        attributes_override = attributes_override or {}
        unknown = set(replacements) - self.records.keys()
        if unknown:
            raise ValueError(f"Cannot replace missing resources: {unknown}")
        resource_data = bytearray()
        resource_map = bytearray(self.map)
        for key, (payload, attributes, reference) in self.records.items():
            payload = replacements.get(key, payload)
            attributes = attributes_override.get(key, attributes)
            if len(resource_data) > 0xFFFFFF:
                raise ValueError("Resource data offset exceeds 24 bits")
            struct.pack_into(">I", resource_map, reference + 4, attributes << 24 | len(resource_data))
            resource_data.extend(struct.pack(">I", len(payload)))
            resource_data.extend(payload)
        map_offset = self.data_offset + len(resource_data)
        header = struct.pack(">IIII", self.data_offset, map_offset, len(resource_data), len(resource_map))
        resource_map[:16] = header
        prefix = bytearray(self.data[: self.data_offset])
        prefix[:16] = header
        return bytes(prefix + resource_data + resource_map)
