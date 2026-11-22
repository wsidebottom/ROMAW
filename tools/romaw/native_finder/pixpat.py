# +===================================================================
#   File:      pixpat.py
#
#   Summary:   Decode indexed color PixPat resources into native 32-bit bitmap pixels.
#
#   Classes:   None declared.
#
#   Functions: decode_pixpat
#
#   Origin:    Existing repository source in executor/src/tools/native_finder; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Decode indexed color PixPat resources into native 32-bit bitmap pixels."""

import struct


def decode_pixpat(data):
    """Return (width, height, row-major 0xRRGGBB pixels) from indexed PixPat bytes.

    Only unpacked 1/2/4/8-bit indexed maps are supported. Offsets, row extents,
    color-table identities and every referenced palette index are validated
    before native bitmap output is produced.
    """

    def read(fmt, offset):
        """Read a complete big-endian field within the resource byte extent."""
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("Truncated PixPat resource")
        return struct.unpack_from(fmt, data, offset)

    if read(">H", 0)[0] != 1:
        raise ValueError("Desktop requires a color PixPat resource")
    pixmap, pixels = read(">II", 2)
    if pixmap < 28 or pixels < pixmap + 50:
        raise ValueError("Invalid PixPat map/data offsets")
    # The high rowBytes bits identify PixMap flags rather than row storage.
    row_bytes = read(">H", pixmap + 4)[0] & 0x3FFF
    top, left, bottom, right = read(">hhhh", pixmap + 6)
    width, height = right - left, bottom - top
    pixel_type, depth, components, component_size = read(">HHHH", pixmap + 30)
    table = read(">I", pixmap + 42)[0]
    if (
        not 0 < width <= 1024
        or not 0 < height <= 1024
        or depth not in (1, 2, 4, 8)
        or pixel_type != 0
        or components != 1
        or component_size != depth
        or read(">H", pixmap + 16)[0] != 0
        or row_bytes < (width * depth + 7) // 8
        or pixels + row_bytes * height > table
    ):
        raise ValueError("Invalid or unsupported indexed PixPat bitmap")
    flags, last = read(">HH", table + 4)
    if last >= 256:
        raise ValueError("Invalid PixPat color table size")
    palette = {}
    for ordinal in range(last + 1):
        index, red, green, blue = read(">HHHH", table + 8 + ordinal * 8)
        # Device tables use entry order; other tables carry explicit indexes.
        if flags & 0x8000:
            index = ordinal
        if index >= 1 << depth or index in palette:
            raise ValueError("Invalid PixPat color index")
        palette[index] = (red >> 8) << 16 | (green >> 8) << 8 | (blue >> 8)
    result = []
    mask = (1 << depth) - 1
    # Packed indexes are most-significant-bit first; row padding is skipped.
    for y in range(height):
        for x in range(width):
            byte = data[pixels + y * row_bytes + x * depth // 8]
            index = (byte >> (8 - depth - x * depth % 8)) & mask
            if index not in palette:
                raise ValueError("PixPat pixel references a missing color")
            result.append(palette[index])
    return width, height, result
