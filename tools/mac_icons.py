# +===================================================================
#   File:      mac_icons.py
#
#   Summary:   Macintosh icon families and Windows resource generation.
#
#   Classes:   None declared.
#
#   Functions: icns_chunk, vertical_scroll_thumb, scroll_arrow, icns_family, icns_members, windows_cursor, decode_ico, decode,
#              encode, decode_png, read_classic_palette, classic_palette, best_frame, resized_rows, image, classic_frame,
#              encode_ico, complete_depths, complete_huge, without_huge, color_icon, _color_icon_layout,
#              tint_color_icon, color_icon_frame, resource, rgba, png
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Macintosh icon families and Windows resource generation."""

import struct
import zlib
import io
from functools import lru_cache
from pathlib import Path

from resource_catalog import read_rez

KINDS = {
    b"ICN#": (32, 1, True),
    b"ics#": (16, 1, True),
    b"icl4": (32, 4, False),
    b"icl8": (32, 8, False),
    b"ics4": (16, 4, False),
    b"ics8": (16, 8, False),
    b"ich#": (48, 1, True),
    b"ich4": (48, 4, False),
    b"ich8": (48, 8, False),
    b"ICON": (32, 1, False),
    b"SICN": (16, 1, False),
}

PNG_TYPES = {
    16: b"icp4",
    32: b"icp5",
    64: b"icp6",
    128: b"ic07",
    256: b"ic08",
    512: b"ic09",
    1024: b"ic10",
}
PNG_SIZES = {kind: size for size, kind in PNG_TYPES.items()} | {
    b"ic11": 32,
    b"ic12": 64,
    b"ic13": 256,
    b"ic14": 512,
}


def icns_chunk(kind, payload):
    return struct.pack(">4sI", kind, len(payload) + 8) + payload


def vertical_scroll_thumb(rows):
    """Flip vertically, then rotate left, preserving every pixel."""
    return [list(column) for column in zip(*rows[::-1])][::-1]


def scroll_arrow(rows, direction):
    if direction == "right":
        return rows
    if direction == "left":
        return [row[::-1] for row in rows]
    if direction == "up":
        return [list(column) for column in zip(*rows)][::-1]
    if direction == "down":
        return [list(column) for column in zip(*rows[::-1])]
    raise ValueError(f"Invalid scrollbar arrow direction: {direction}")


def icns_family(chunks):
    return icns_chunk(b"icns", b"".join(chunks))


def icns_members(data):
    if len(data) < 8 or struct.unpack_from(">4sI", data) != (b"icns", len(data)):
        raise ValueError("Invalid ICNS header")
    offset = 8
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("Truncated ICNS member")
        kind, length = struct.unpack_from(">4sI", data, offset)
        if not 8 <= length <= len(data) - offset:
            raise ValueError("Invalid ICNS member extent")
        yield kind, data[offset + 8 : offset + length]
        offset += length


def windows_cursor(data):
    """Convert a 16x16 monochrome Windows CUR, including inverse pixels."""
    if len(data) < 22 or struct.unpack_from("<HHH", data) != (0, 2, 1):
        raise ValueError("Expected a single-image Windows cursor")
    width, height, colors, reserved, x, y, length, offset = struct.unpack_from("<BBBBHHII", data, 6)
    if (
        (width, height, reserved) != (16, 16, 0)
        or colors not in (0, 2)
        or x >= 16
        or y >= 16
        or offset < 22
        or length < 176
        or offset + length > len(data)
    ):
        raise ValueError("Invalid 16x16 Windows cursor entry")
    bitmap = data[offset : offset + length]
    header, w, h, planes, depth, compression = struct.unpack_from("<IiiHHI", bitmap)
    if (header, w, h, planes, depth, compression) != (40, 16, 32, 1, 1, 0):
        raise ValueError("Expected a monochrome Windows cursor DIB")
    palette = [tuple(bitmap[i : i + 3]) for i in (40, 44)]
    if set(palette) != {(0, 0, 0), (255, 255, 255)}:
        raise ValueError("Expected a black and white cursor palette")
    image = bytearray()
    mask = bytearray()
    for row in range(15, -1, -1):
        for column in range(2):
            xor = bitmap[48 + row * 4 + column]
            if palette[0] == (255, 255, 255):
                xor ^= 255
            opaque = bitmap[112 + row * 4 + column] ^ 255
            image.append(xor ^ opaque)
            mask.append(opaque)
    return bytes(image + mask) + struct.pack(">hh", y, x)


def decode_ico(data):
    """Decode every bounded ICO frame, preserving indexed palettes and transparency."""
    if len(data) < 6 or len(data) > 32 * 1024 * 1024:
        raise ValueError("Invalid ICO extent")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if reserved or kind != 1 or not 1 <= count <= 1024 or 6 + count * 16 > len(data):
        raise ValueError("Invalid ICO directory")
    frames = []
    for index in range(count):
        width, height, _, reserved, planes, _, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + index * 16)
        width, height = width or 256, height or 256
        if reserved or planes not in (0, 1) or offset < 6 + count * 16 or not length or length > len(data) - offset:
            raise ValueError("Invalid ICO image extent")
        payload = data[offset : offset + length]
        if payload.startswith(b"\x89PNG\r\n\x1a\n"):
            frames.append(decode_png(payload, width, height))
            continue
        if len(payload) < 40:
            raise ValueError("Truncated ICO bitmap")
        header, w, h, dib_planes, depth, compression = struct.unpack_from("<IiiHHI", payload)
        if not 40 <= header <= len(payload) or w != width or abs(h) != height * 2 or dib_planes != 1:
            raise ValueError("Invalid ICO bitmap dimensions")
        if (
            depth not in (1, 4, 8, 16, 24, 32)
            or compression not in (0, 3)
            or (compression == 3 and depth not in (16, 32))
        ):
            raise ValueError("Unsupported ICO bitmap encoding")
        colors = struct.unpack_from("<I", payload, 32)[0] if depth <= 8 else 0
        colors = colors or (1 << depth if depth <= 8 else 0)
        if depth <= 8 and colors > 1 << depth:
            raise ValueError("Invalid ICO palette")
        base = header + colors * 4 + (12 if compression == 3 and header == 40 else 0)
        stride, mask_stride = ((width * depth + 31) // 32) * 4, ((width + 31) // 32) * 4
        mask_base = base + stride * height
        if mask_base > len(payload) or (depth != 32 and mask_base + mask_stride * height > len(payload)):
            raise ValueError("Truncated ICO pixels or mask")
        if depth == 16 or compression == 3:
            # Pillow supplies Windows bitfield handling; indexed frames above
            # stay unconverted so resource palette roles are never renumbered.
            from PIL import Image

            single = struct.pack("<HHHBBBBHHII", 0, 1, 1, width % 256, height % 256, 0, 0, 1, depth, length, 22)
            try:
                with Image.open(io.BytesIO(single + payload)) as bitmap:
                    rgba_rows = bitmap.convert("RGBA")
                    if rgba_rows.size != (width, height):
                        raise ValueError("ICO bitfield dimensions changed")
                    frames.append(
                        decode_png(
                            png([list(rgba_rows.crop((0, y, width, y + 1)).getdata()) for y in range(height)]),
                            width,
                            height,
                        )
                    )
            except OSError as error:
                raise ValueError("Invalid ICO bitfield bitmap") from error
            continue
        palette = [tuple(reversed(payload[header + i * 4 : header + i * 4 + 3])) for i in range(colors)]
        packed_stride, opacity_stride = (width * depth + 7) // 8, (width + 7) // 8
        raster, opacity = bytearray(), bytearray()
        has_mask = mask_base + mask_stride * height <= len(payload)
        for y in range(height):
            row = height - 1 - y if h > 0 else y
            raster += payload[base + row * stride : base + row * stride + packed_stride]
            opacity += bytes(
                (payload[mask_base + row * mask_stride + x] ^ 255) if has_mask else 255 for x in range(opacity_stride)
            )
        frame = dict(
            size=width, height=height, depth=depth, palette=palette, image=bytes(raster), opacity=bytes(opacity)
        )
        rgba(frame)  # Reject palette indices outside a shortened colour table.
        frames.append(frame)
    return frames


def decode(data):
    """Read PNG, ICO or editable ICNS artwork by its file signature."""
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        if len(data) < 24 or data[12:16] != b"IHDR":
            raise ValueError("Truncated PNG icon header")
        width, height = struct.unpack_from(">2I", data, 16)
        if not 1 <= width <= 1024 or not 1 <= height <= 1024:
            raise ValueError("Invalid PNG icon dimensions")
        return [decode_png(data, width, height)]
    if data[:4] == b"\0\0\1\0":
        return decode_ico(data)
    exact = []
    standard = {}
    for kind, payload in icns_members(data):
        if kind == b"exfr":
            if len(payload) < 10:
                raise ValueError("Truncated ICNS source frame")
            width, height, depth, colors, version = struct.unpack_from(">5H", payload)
            pixel_end = 10 + colors * 3 + ((width * depth + 7) // 8) * height
            if (
                not 1 <= width <= 1024
                or not 1 <= height <= 1024
                or depth not in (1, 4, 8, 24, 32)
                or version != 1
                or not (1 <= colors <= 1 << depth if depth <= 8 else colors == 0)
                or len(payload) != pixel_end + ((width + 7) // 8) * height
            ):
                raise ValueError("Invalid ICNS source frame")
            exact.append(
                dict(
                    size=width,
                    height=height,
                    depth=depth,
                    palette=list(struct.iter_unpack("3B", payload[10 : 10 + colors * 3])),
                    image=payload[10 + colors * 3 : pixel_end],
                    opacity=payload[pixel_end:],
                )
            )
        else:
            standard[kind] = payload
    if exact:
        return exact
    frames = []
    for kind, (size, depth, paired) in KINDS.items():
        if kind not in standard:
            continue
        pixels = size * size * depth // 8
        mask_size = size * size // 8
        payload = standard[kind]
        if len(payload) != pixels + (mask_size if paired else 0):
            raise ValueError("Invalid indexed ICNS member size")
        mask_kind = {16: b"ics#", 32: b"ICN#", 48: b"ich#"}[size]
        mask = standard.get(mask_kind, b"")[mask_size:]
        frames.append(
            dict(
                size=size,
                height=size,
                depth=depth,
                palette=classic_palette(depth),
                image=payload[:pixels],
                opacity=mask if len(mask) == mask_size else b"\xff" * mask_size,
            )
        )
    for kind, payload in standard.items():
        if kind in PNG_SIZES and payload.startswith(b"\x89PNG\r\n\x1a\n"):
            size = PNG_SIZES[kind]
            frames.append(decode_png(payload, size, size))
    if not frames:
        raise ValueError("ICNS contains no supported images")
    return frames


def encode(frames):
    """Write standard previews and lossless source frames for classic artwork."""
    chunks = []
    previews = {}
    for frame in frames:
        width, height, depth = (frame[k] for k in ("size", "height", "depth"))
        palette = frame["palette"]
        payload = struct.pack(">5H", width, height, depth, len(palette), 1)
        payload += b"".join(map(bytes, palette)) + frame["image"] + frame["opacity"]
        chunks.append(icns_chunk(b"exfr", payload))
        size = next(n for n in PNG_TYPES if n >= max(width, height))
        old = previews.get(size)
        if old is None or (width * height, depth) > (
            old["size"] * old["height"],
            old["depth"],
        ):
            previews[size] = frame
    for size, frame in sorted(previews.items()):
        rows = [[(0, 0, 0, 0)] * size for _ in range(size)]
        x, y = (size - frame["size"]) // 2, (size - frame["height"]) // 2
        for i, row in enumerate(rgba(frame), y):
            rows[i][x : x + len(row)] = row
        chunks.append(icns_chunk(PNG_TYPES[size], png(rows)))
    for kind, (size, depth, paired) in KINDS.items():
        if kind in (b"ICON", b"SICN"):
            continue
        for frame in frames:
            if (frame["size"], frame["height"], frame["depth"]) == (size, size, depth):
                native = classic_frame(frame, size, depth)
                chunks.append(icns_chunk(kind, native["image"] + (native["opacity"] if paired else b"")))
                break
    return icns_family(chunks)


def decode_png(payload, width, height):
    try:
        from PIL import Image
    except ImportError as error:
        raise ValueError("PNG icon images require Pillow: install python3-pil") from error
    try:
        with Image.open(io.BytesIO(payload)) as image:
            if image.format != "PNG" or image.size != (width, height):
                raise ValueError("PNG dimensions disagree with ICNS member")
            rows = image.convert("RGBA")
            pixels = list(rows.getdata())
    except (OSError, Image.DecompressionBombError) as error:
        raise ValueError("Invalid icon PNG image") from error
    mask = bytearray(((width + 7) // 8) * height)
    raster = bytearray()
    for i, (r, g, b, a) in enumerate(pixels):
        raster.extend((b, g, r, a))
        if a:
            y, x = divmod(i, width)
            mask[y * ((width + 7) // 8) + x // 8] |= 128 >> (x % 8)
    return dict(
        size=width,
        height=height,
        depth=32,
        palette=[],
        image=bytes(raster),
        opacity=bytes(mask),
    )


def read_classic_palette(path, depth):
    """Read an indexed CLUT as 8-bit RGB, respecting explicit or device indices."""
    if depth not in {1, 4, 8}:
        raise ValueError("Classic artwork palettes require depth 1, 4 or 8")
    entries = read_rez(path)
    entry = next((entry for entry in entries if entry["type"] == "clut" and entry["id"] == depth), None)
    if entry is None:
        raise ValueError(f"Missing classic palette for depth {depth}")
    payload = entry.get("payload", b"")
    if len(payload) < 8:
        raise ValueError("Truncated classic colour table")
    _, flags, last = struct.unpack_from(">IHh", payload)
    count = last + 1
    if count != 1 << depth or len(payload) != 8 + count * 8:
        raise ValueError("Classic colour table size differs from its pixel depth")
    if flags & ~0x8000:
        raise ValueError("Unsupported classic colour table flags")
    palette = [None] * count
    for ordinal in range(count):
        index, red, green, blue = struct.unpack_from(">4H", payload, 8 + ordinal * 8)
        if flags & 0x8000:
            index = ordinal
        if index >= count or palette[index] is not None:
            raise ValueError("Invalid or duplicate classic palette index")
        # Icon pixels expose eight bits per component. Replicated 8-bit source
        # values (v * 257) return exactly v, without changing palette indices.
        palette[index] = (red >> 8, green >> 8, blue >> 8)
    return palette


@lru_cache(maxsize=3)
def classic_palette(depth):
    return read_classic_palette(Path(__file__).resolve().parents[1] / "bitmaps/mac-palettes.r", depth)


def best_frame(frames, size, depth=None):
    return min(
        frames,
        key=lambda f: (
            abs(f["size"] - size) + abs(f["height"] - size),
            f["size"] < size,
            depth is not None and f["depth"] != depth,
            -f["depth"],
        ),
    )


def resized_rows(frame, size):
    rows = rgba(frame)
    # Nearest-neighbour keeps the edges of classic pixel artwork sharp.
    return [[rows[y * frame["height"] // size][x * frame["size"] // size] for x in range(size)] for y in range(size)]


def image(data, size):
    """Render an icon at the requested size, even if an editor removed it."""
    return resized_rows(best_frame(decode(data), size), size)


def classic_frame(frame, size, depth):
    palette = classic_palette(depth)
    if (frame["size"], frame["height"], frame["depth"], frame["palette"]) == (
        size,
        size,
        depth,
        palette,
    ):
        return frame
    rows = resized_rows(frame, size)
    row_bytes = (size * depth + 7) // 8
    mask_bytes = (size + 7) // 8
    pixels = bytearray(row_bytes * size)
    mask = bytearray(mask_bytes * size)
    cache = {}
    for y, row in enumerate(rows):
        for x, (r, g, b, a) in enumerate(row):
            rgb = (r, g, b)
            if a < 128:
                index = 0
            elif depth == 1:
                index = int(299 * r + 587 * g + 114 * b < 128000)
            else:
                if rgb not in cache:
                    cache[rgb] = min(
                        range(len(palette)),
                        key=lambda i: sum((c - p) ** 2 for c, p in zip(rgb, palette[i])),
                    )
                index = cache[rgb]
            bit = y * row_bytes * 8 + x * depth
            pixels[bit // 8] |= index << (8 - depth - bit % 8)
            if a >= 128:
                mask[y * mask_bytes + x // 8] |= 128 >> (x % 8)
    return dict(
        size=size,
        height=size,
        depth=depth,
        palette=palette,
        image=bytes(pixels),
        opacity=bytes(mask),
    )


def encode_ico(frames):
    # Windows icon directories can describe images up to 256 pixels.
    supported = [f for f in frames if max(f["size"], f["height"]) <= 256]
    if not supported:
        payload = png(resized_rows(best_frame(frames, 256), 256))
        return struct.pack("<HHHBBBBHHII", 0, 1, 1, 0, 0, 0, 0, 1, 32, len(payload), 22) + payload
    frames = supported
    directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    images = []
    offset = 6 + len(frames) * 16
    for frame in frames:
        size, height, depth = frame["size"], frame["height"], frame["depth"]
        tight = (size * depth + 7) // 8
        stride = (tight + 3) & ~3
        mask_tight = (size + 7) // 8
        mask_stride = (mask_tight + 3) & ~3
        raster = b"".join(
            frame["image"][y * tight : (y + 1) * tight] + bytes(stride - tight) for y in reversed(range(height))
        )
        mask = b"".join(
            bytes(x ^ 255 for x in frame["opacity"][y * mask_tight : (y + 1) * mask_tight])
            + bytes(mask_stride - mask_tight)
            for y in reversed(range(height))
        )
        palette = b"".join(bytes((b, g, r, 0)) for r, g, b in frame["palette"])
        image = struct.pack(
            "<IiiHHIIiiII",
            40,
            size,
            height * 2,
            1,
            depth,
            0,
            len(raster) + len(mask),
            0,
            0,
            len(frame["palette"]),
            0,
        )
        image += palette + raster + mask
        directory += struct.pack(
            "<BBBBHHII",
            size % 256,
            height % 256,
            len(frame["palette"]) % 256,
            0,
            1,
            depth,
            len(image),
            offset,
        )
        images.append(image)
        offset += len(image)
    return bytes(directory) + b"".join(images)


def complete_depths(data):
    """Retain edited artwork and add missing mono, 16- and 256-colour frames."""
    frames = decode(data)
    original_count = len(frames)
    if any(f["size"] != f["height"] for f in frames):
        raise ValueError("Expected square icon frames")
    for size in sorted({frame["size"] for frame in frames}):
        source = best_frame(frames, size)
        for depth in (1, 4, 8):
            if not any(f["size"] == size and f["depth"] == depth for f in frames):
                if source["depth"] < depth and source["size"] == size:
                    # UI palettes use indices as recolouring roles. Expanding
                    # an indexed image must not renumber those existing roles.
                    row_bytes = (size * depth + 7) // 8
                    source_bytes = (size * source["depth"] + 7) // 8
                    pixels = bytearray(row_bytes * size)
                    for y in range(size):
                        for x in range(size):
                            bit = x * source["depth"]
                            value = source["image"][y * source_bytes + bit // 8] >> (8 - source["depth"] - bit % 8)
                            value &= (1 << source["depth"]) - 1
                            bit = x * depth
                            pixels[y * row_bytes + bit // 8] |= value << (8 - depth - bit % 8)
                    palette = list(source["palette"])
                    palette += classic_palette(depth)[len(palette) :]
                    frames.append(dict(source, depth=depth, palette=palette, image=bytes(pixels)))
                else:
                    frames.append(classic_frame(source, size, depth))
    return data if len(frames) == original_count else encode(frames)


def complete_huge(data):
    """Preserve existing members and supply the complete 48-pixel classic family."""
    if data[:4] != b"icns":
        data = encode(decode(data))
    members = list(icns_members(data))
    present = {kind for kind, _ in members}
    missing = [kind for kind in (b"ich#", b"ich4", b"ich8") if kind not in present]
    if not missing:
        return data
    chunks = [icns_chunk(kind, payload) for kind, payload in members]
    chunks.extend(icns_chunk(kind, resource(data, kind)) for kind in missing)
    return icns_family(chunks)


def without_huge(data):
    """Remove 48-pixel members while preserving all other ICNS payloads exactly."""
    members = list(icns_members(data))
    retained = [
        (kind, payload)
        for kind, payload in members
        if kind not in (b"ich#", b"ich4", b"ich8", b"ih32", b"h8mk")
        and not (kind == b"exfr" and len(payload) >= 4 and struct.unpack_from(">2H", payload) == (48, 48))
    ]
    return data if retained == members else icns_family([icns_chunk(kind, payload) for kind, payload in retained])


def color_icon(data):
    """Build a classic cicn resource with a separate transparency mask."""
    source = best_frame(decode(data), 32)
    color = classic_frame(source, 32, 8)
    mono = classic_frame(source, 32, 1)
    header = bytearray(82)
    bounds = (0, 0, 32, 32)
    struct.pack_into(">H4h", header, 4, 0x8020, *bounds)
    struct.pack_into(">II", header, 22, 72 << 16, 72 << 16)
    struct.pack_into(">4H", header, 30, 0, 8, 1, 8)
    struct.pack_into(">H4h", header, 54, 4, *bounds)
    struct.pack_into(">H4h", header, 68, 4, *bounds)
    palette = color["palette"]
    table = struct.pack(">IHH", 0, 0, len(palette) - 1)
    table += b"".join(struct.pack(">4H", index, *(c * 257 for c in rgb)) for index, rgb in enumerate(palette))
    return bytes(header) + color["opacity"] + mono["image"] + table + color["image"]


def _color_icon_layout(data):
    """Validate the indexed CIcon representation emitted by color_icon."""
    if not 82 <= len(data) <= 32 * 1024 * 1024:
        raise ValueError("Invalid color icon extent")
    row, top, left, bottom, right = struct.unpack_from(">H4h", data, 4)
    width, height = right - left, bottom - top
    row &= 0x3FFF
    mask_row = struct.unpack_from(">H", data, 54)[0]
    mono_row = struct.unpack_from(">H", data, 68)[0]
    if (
        not 1 <= width <= 4096
        or not 1 <= height <= 4096
        or row < width
        or mask_row < (width + 7) // 8
        or mono_row < (width + 7) // 8
        or struct.unpack_from(">4H", data, 30) != (0, 8, 1, 8)
    ):
        raise ValueError("Unsupported color icon geometry or pixel format")
    table = 82 + (mask_row + mono_row) * height
    if table + 8 > len(data):
        raise ValueError("Truncated color icon table")
    flags, last = struct.unpack_from(">HH", data, table + 4)
    count = last + 1
    pixels = table + 8 + count * 8
    if flags not in (0, 0x8000) or not 1 <= count <= 256 or pixels + row * height != len(data):
        raise ValueError("Invalid color icon palette or pixel extent")
    slots = {}
    for ordinal in range(count):
        offset = table + 8 + ordinal * 8
        index = ordinal if flags & 0x8000 else struct.unpack_from(">H", data, offset)[0]
        if index >= count or index in slots:
            raise ValueError("Invalid color icon palette index")
        slots[index] = offset + 2
    return width, height, row, mask_row, slots, pixels


def tint_color_icon(data, index, rgb):
    """Change one ColorSpec's RGB words, as Finder 7 does for label-menu icons."""
    _, _, _, _, slots, _ = _color_icon_layout(data)
    if index not in slots or len(rgb) != 3 or any(not 0 <= channel <= 255 for channel in rgb):
        raise ValueError("Invalid label icon tint index or RGB color")
    result = bytearray(data)
    struct.pack_into(">3H", result, slots[index], *(channel * 257 for channel in rgb))
    return bytes(result)


def color_icon_frame(data):
    """Decode a generated CIcon, retaining its palette indices and mask."""
    width, height, row, mask_row, slots, pixels = _color_icon_layout(data)
    palette = [tuple(channel >> 8 for channel in struct.unpack_from(">3H", data, slots[i])) for i in range(len(slots))]
    image = b"".join(data[pixels + y * row : pixels + y * row + width] for y in range(height))
    if any(index >= len(palette) for index in image):
        raise ValueError("Color icon pixel exceeds palette")
    mask_width = (width + 7) // 8
    mask = b"".join(data[82 + y * mask_row : 82 + y * mask_row + mask_width] for y in range(height))
    return dict(size=width, height=height, depth=8, palette=palette, image=image, opacity=mask)


def resource(data, kind, length=None):
    if kind == b"cicn":
        return color_icon(data)
    size, depth, paired = KINDS[kind]
    frames = decode(data)
    candidates = [f for f in frames if (f["size"], f["height"], f["depth"]) == (size, size, depth)]
    if not candidates:
        candidates = [best_frame(frames, size, depth)]
    candidates = [classic_frame(frame, size, depth) for frame in candidates]
    if kind == b"SICN" and length is not None:
        if length % 32:
            raise ValueError("Invalid SICN resource length")
        candidates += [candidates[-1]] * max(0, length // 32 - len(candidates))
    result = b"".join(f["image"] for f in candidates) if kind == b"SICN" else candidates[0]["image"]
    # Windows ICOs may use black=0/white=1; Macintosh monochrome resources
    # always use white=0/black=1. The AND mask has independent polarity.
    if depth == 1:
        converted = []
        for frame in candidates if kind == b"SICN" else candidates[:1]:
            bits = frame["image"]
            if sum(frame["palette"][0]) < sum(frame["palette"][1]):
                bits = bytes(x ^ 255 for x in bits)
            if kind == b"SICN":
                # SICN has no mask plane: composite transparent source pixels
                # to white instead of exposing arbitrary ICNS image bits.
                bits = bytes(pixel & mask for pixel, mask in zip(bits, frame["opacity"]))
            converted.append(bits)
        result = b"".join(converted)
    if paired:
        result += candidates[0]["opacity"]
    return result if length is None else result[:length]


def rgba(frame):
    size, depth = frame["size"], frame["depth"]
    rows = []
    image, mask = frame["image"], frame["opacity"]
    alpha_used = depth == 32 and any(image[i] for i in range(3, len(image), 4))
    for y in range(frame["height"]):
        row = []
        for x in range(size):
            if depth <= 8:
                bit = y * ((size * depth + 7) // 8) * 8 + x * depth
                index = image[bit // 8] >> (8 - depth - bit % 8) & ((1 << depth) - 1)
                if index >= len(frame["palette"]):
                    raise ValueError("Icon pixel exceeds palette")
                r, g, b = frame["palette"][index]
                a = 255
            else:
                offset = (y * size + x) * (depth // 8)
                b, g, r = image[offset : offset + 3]
                a = image[offset + 3] if alpha_used else 255
            if not (mask[y * ((size + 7) // 8) + x // 8] & (128 >> (x % 8))):
                a = 0
            row.append((r, g, b, a))
        rows.append(row)
    return rows


def png(rows):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    raster = b"".join(b"\0" + bytes(c for pixel in row for c in pixel) for row in rows)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", len(rows[0]), len(rows), 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raster))
        + chunk(b"IEND", b"")
    )


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Generate a Windows icon from ICNS")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(encode_ico(decode(args.source.read_bytes())))
