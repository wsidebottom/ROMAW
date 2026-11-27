# +===================================================================
#   File:      rez_system_types.py
#
#   Summary:   Encode structured System keyboard, locale, pattern and dispatch resources.
#
#   Classes:   None.
#
#   Functions: compile_system_resource, pattern_bitmap, indexed_pixels.
#
#   Origin:    ROMAW Intl0Rec/Intl1Rec and keyboard structures, classic PixMap
#              layouts, and the local Super ResEdit 2.1.3 itlb/ppat templates.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Checked layouts for the remaining literal System resources, without a Rez SDK."""

import io
import struct

NATIVE_OFFSETS = {
    "P_cdef0": 0,
    "P_cdef16": 4,
    "P_wdef0": 8,
    "P_wdef16": 12,
    "P_mdef0": 16,
    "P_ldef0": 20,
    "P_ldef19": 72,
    "P_mbdf0": 24,
    "P_snth5": 28,
    "P_cdef1008": 36,
    "P_dcmp_template": 40,
}
NATIVE_TYPES = {
    "CDEF": {0, 4, 36},
    "WDEF": {8, 12},
    "MDEF": {16},
    "LDEF": {20, 72},
    "MBDF": {24},
    "snth": {28},
    "dcmp": {40},
}
SYSTEM_TYPES = set(NATIVE_TYPES) | {"INTL", "KCHR", "KMAP", "fwst", "itlb", "movp", "ppat", "PAT ", "PAT#"}


def pattern_bitmap(data):
    """Read an editable eight-by-eight black/white PNG into Macintosh pattern rows."""
    from PIL import Image

    with Image.open(io.BytesIO(data)) as image:
        if image.format != "PNG" or image.size != (8, 8) or image.n_frames != 1:
            raise ValueError("Pattern artwork must be one 8x8 PNG")
        if any(alpha != 255 for alpha in image.convert("RGBA").getchannel("A").tobytes()):
            raise ValueError("Monochrome pattern pixels must be opaque")
        pixels = list(struct.iter_unpack("3B", image.convert("RGB").tobytes()))
    if any(pixel not in ((0, 0, 0), (255, 255, 255)) for pixel in pixels):
        raise ValueError("Pattern artwork must contain only opaque black and white")
    return bytes(sum((pixels[y * 8 + x] == (0, 0, 0)) << (7 - x) for x in range(8)) for y in range(8))


def indexed_pixels(data, width, height, depth, stride, color_count):
    """Preserve PNG palette indexes; never quantize or reorder the pattern palette."""
    from PIL import Image

    if (
        not 1 <= color_count <= 256
        or depth not in (1, 2, 4, 8)
        or stride != (width * depth + 7) // 8
        or width <= 0
        or height <= 0
    ):
        raise ValueError("Unsupported pixel-pattern dimensions, depth or row stride")
    with Image.open(io.BytesIO(data)) as image:
        if image.format != "PNG" or image.mode != "P" or image.size != (width, height) or image.n_frames != 1:
            raise ValueError("Pixel-pattern artwork must be an indexed PNG with matching dimensions")
        if "transparency" in image.info:
            raise ValueError("Pixel patterns cannot contain transparent palette entries")
        palette = image.getpalette()
        pixels = image.tobytes()
        if not palette or len(palette) < color_count * 3 or any(p >= color_count or p >= 1 << depth for p in pixels):
            raise ValueError("Pixel-pattern index exceeds its declared color table")
        colors = [tuple(c * 257 for c in palette[3 * i : 3 * i + 3]) for i in range(color_count)]
    rows = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            bit = x * depth
            rows[y * stride + bit // 8] |= pixels[y * width + x] << (8 - depth - bit % 8)
    return bytes(rows), colors


def compile_system_resource(kind, fields, read_artwork=None):
    """Serialize resource-specific fields; reject unsupported extents and trailing data."""
    if kind not in SYSTEM_TYPES:
        return None

    def scalar(fmt):
        bits = struct.calcsize(fmt) * 8
        signed = fmt.islower()
        return struct.pack(
            ">" + fmt, fields.number(-(1 << (bits - 1)) if signed else 0, (1 << (bits - int(signed))) - 1)
        )

    def separator():
        fields.expect(",")

    def fixed_string(size):
        value = bytearray()
        while fields.peek() is not None and fields.peek().startswith('"'):
            value.extend(fields.decode_string(fields.take()))
        if len(value) != size:
            raise ValueError(f"Expected exactly {size} bytes in fixed System string")
        return bytes(value)

    def array(fmt, count=None):
        fields.expect("{")
        result = bytearray()
        while fields.peek() != "}":
            result.extend(scalar(fmt))
            fields.expect(";")
        fields.expect("}")
        if count is not None and len(result) != count * struct.calcsize(fmt):
            raise ValueError("Incorrect System array length")
        return bytes(result)

    def artwork(function):
        fields.expect(function)
        fields.expect("(")
        name = fields.decode_string(fields.take()).decode("mac_roman")
        fields.expect(")")
        if read_artwork is None:
            raise ValueError("System bitmap requires an artwork reader")
        return read_artwork(name)

    if kind in NATIVE_TYPES:
        offset = fields.number(0, 65532)
        if offset not in NATIVE_TYPES[kind]:
            raise ValueError("Native definition does not match its resource type")
        result = struct.pack(">5H", 8312, 88, 8296, offset, 20176)
    elif kind == "PAT ":
        result = pattern_bitmap(artwork("$$pattern"))
    elif kind == "PAT#":
        fields.expect("{")
        patterns = []
        while fields.peek() != "}":
            patterns.append(pattern_bitmap(artwork("$$pattern")))
            fields.expect(";")
        fields.expect("}")
        if len(patterns) > 65535:
            raise ValueError("Too many monochrome patterns")
        result = struct.pack(">H", len(patterns)) + b"".join(patterns)
    elif kind == "INTL":
        variant = fields.choice({"NumberFormat": 0, "DateFormat": 1})
        fields.expect("{")
        result = bytearray()
        if variant == 0:
            # Intl0Rec: decimal/group/list separators, currency, date/time flags.
            for size in (1, 1, 1, 3):
                result.extend(fixed_string(size))
                separator()
            for index in range(6):
                result.extend(scalar("B"))
                separator()
            for size in (4, 4, 9):
                result.extend(fixed_string(size))
                separator()
            result.extend(scalar("B"))
            separator()
            result.extend(scalar("h"))
        else:
            # Intl1Rec keeps the entire 16-byte Pascal slot, including its padding.
            for count in (7, 12):
                fields.expect("{")
                for _ in range(count):
                    slot = fields.string()
                    if len(slot) > 16:
                        raise ValueError("International name exceeds Str15")
                    result.extend(slot.ljust(16, b"\0"))
                    fields.expect(";")
                fields.expect("}")
                separator()
            for _ in range(4):
                result.extend(scalar("B"))
                separator()
            for _ in range(5):
                result.extend(fixed_string(4))
                separator()
            result.extend(scalar("h"))
            separator()
            result.extend(scalar("H"))
        fields.expect("}")
    elif kind == "KCHR":
        result = bytearray(scalar("H"))
        separator()
        modifiers = array("B", 256)
        separator()
        fields.expect("{")
        tables = []
        while fields.peek() != "}":
            tables.append(fixed_string(128))
            fields.expect(";")
        fields.expect("}")
        separator()
        if not tables or len(tables) > 256:
            raise ValueError("Invalid keyboard translation-table count")
        # Preserve the inherited 136/137 layouts: some unused modifier states
        # contain selector 8 despite an eight-table payload. Conversion is not a repair.
        fields.expect("{")
        dead_keys = []
        while fields.peek() != "}":
            table = fields.number(0, len(tables) - 1)
            separator()
            key = fields.number(0, 127)
            separator()
            fields.expect("{")
            pairs = []
            while fields.peek() != "}":
                character = scalar("B")
                separator()
                pairs.append(character + scalar("B"))
                fields.expect(";")
            fields.expect("}")
            separator()
            filler = scalar("B")
            separator()
            no_match = scalar("B")
            fields.expect(";")
            if len(pairs) > 65535:
                raise ValueError("Too many dead-key pairs")
            dead_keys.append(bytes((table, key)) + struct.pack(">H", len(pairs)) + b"".join(pairs) + filler + no_match)
        fields.expect("}")
        if len(dead_keys) > 65535:
            raise ValueError("Too many dead keys")
        result.extend(modifiers + struct.pack(">H", len(tables)) + b"".join(tables))
        result.extend(struct.pack(">H", len(dead_keys)) + b"".join(dead_keys))
    elif kind == "KMAP":
        result = bytearray(scalar("H"))
        separator()
        result.extend(scalar("H"))
        separator()
        result.extend(array("B", 128))
        separator()
        fields.expect("{")
        exceptions = []
        while fields.peek() != "}":
            key = scalar("B")
            separator()
            operation = scalar("B")
            separator()
            exceptions.append(key + operation + fields.string())
            fields.expect(";")
        fields.expect("}")
        if len(exceptions) > 65535:
            raise ValueError("Too many key-map exceptions")
        result.extend(struct.pack(">H", len(exceptions)) + b"".join(exceptions))
    elif kind == "itlb":
        result = bytearray()
        for fmt in ("h", "h", "h", "H", "h", "H", "h", "B", "B", "h", "h"):
            result.extend(scalar(fmt))
            if fields.peek() == ",":
                separator()
        # This is the historical 20-byte bundle; no expanded Script Manager tail.
    elif kind == "fwst":
        result = bytearray(scalar("H"))
        separator()
        result.extend(fields.rect())
        separator()
        result.extend(array("h", 6))
    elif kind == "movp":
        # The inherited preference record has no authoritative semantic schema.
        # Keep its historical signed word boundaries explicit rather than invent fields.
        result = array("h", 17)
    elif kind == "ppat":
        pat_type = scalar("h")
        separator()
        if pat_type != b"\0\1":
            raise ValueError("Only indexed type-one pixel patterns are supported")
        expanded = scalar("I")
        separator()
        valid = scalar("h")
        separator()
        expanded_map = scalar("I")
        separator()
        fallback = pattern_bitmap(artwork("$$pattern"))
        separator()
        fields.expect("{")
        base = scalar("I")
        separator()
        row_bytes = scalar("H")
        separator()
        bounds = fields.rect()
        separator()
        metadata = bytearray()
        formats = ("H", "H", "I", "I", "I", "H", "H", "H", "H", "I", "I")
        values = []
        for i, fmt in enumerate(formats):
            value = scalar(fmt)
            values.append(int.from_bytes(value, "big"))
            metadata.extend(value)
            if i != len(formats) - 1:
                separator()
        fields.expect("}")
        separator()
        if values[0:3] != [0, 0, 0] or values[5] != 0 or values[7] != 1 or values[8] != values[6] or values[9] != 0:
            raise ValueError("Unsupported packed or component pixel-pattern layout")
        picture = artwork("$$pixels")
        separator()
        seed = scalar("I")
        separator()
        flags = scalar("H")
        separator()
        color_indexes = array("H")
        count = len(color_indexes) // 2
        top, left, bottom, right = struct.unpack(">4h", bounds)
        pixels, colors = indexed_pixels(
            picture, right - left, bottom - top, values[6], int.from_bytes(row_bytes, "big") & 16383, count
        )
        if not 1 <= count <= 256:
            raise ValueError("Invalid pixel-pattern color table length")
        # PixMap's color-table offset is derived from the image; reserved stays last.
        pixmap = base + row_bytes + bounds + metadata[:-4] + struct.pack(">I", 78 + len(pixels)) + metadata[-4:]
        table = seed + flags + struct.pack(">H", count - 1)
        table += b"".join(color_indexes[2 * i : 2 * i + 2] + struct.pack(">3H", *rgb) for i, rgb in enumerate(colors))
        result = (
            pat_type + struct.pack(">II", 28, 78) + expanded + valid + expanded_map + fallback + pixmap + pixels + table
        )
    fields.finish()
    return bytes(result)
