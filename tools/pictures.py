# +===================================================================
#   File:      pictures.py
#
#   Summary:   Compile editable PNG and RGB BMP artwork into classic Macintosh PICT resources.
#
#   Classes:   None.
#
#   Functions: read_bmp, read_png, read_image, write_bmp, resource.
#
#   Origin:    Executor BMP-backed picture resource build support.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Build PICT v2 DirectBitsRect resources from lossless PNG or RGB BMP sources."""

import io
import struct


def read_bmp(data):
    """Read bounded Windows 24/32-bit BI_RGB pixels into top-down RGB bytes."""
    if not 54 <= len(data) <= 32 * 1024 * 1024 or data[:2] != b"BM":
        raise ValueError("Invalid RGB BMP extent or signature")
    length, reserved, offset = struct.unpack_from("<III", data, 2)
    header, width, height, planes, depth, compression = struct.unpack_from("<IiiHHI", data, 14)
    if (
        length != len(data)
        or reserved
        or header != 40
        or not 1 <= width <= 4095
        or not 1 <= abs(height) <= 4095
        or planes != 1
        or depth not in (24, 32)
        or compression
        or not 54 <= offset <= len(data)
    ):
        raise ValueError("Expected an uncompressed 24/32-bit RGB Windows BMP")
    stride = ((width * depth + 31) // 32) * 4
    if stride * abs(height) > len(data) - offset:
        raise ValueError("Truncated RGB BMP pixel rows")
    pixels = bytearray()
    for y in range(abs(height)):
        start = offset + (y if height < 0 else height - y - 1) * stride
        for x in range(width):
            blue, green, red = data[start + x * (depth // 8) : start + x * (depth // 8) + 3]
            pixels.extend((red, green, blue))
    return width, abs(height), bytes(pixels)


def read_png(data):
    """Decode bounded PNG artwork to RGB without changing its pixel colors."""
    if not 33 <= len(data) <= 32 * 1024 * 1024 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG extent or signature")
    from PIL import Image

    try:
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.size
            if image.format != "PNG" or not 1 <= width <= 4095 or not 1 <= height <= 4095 or image.n_frames != 1:
                raise ValueError("Expected a single PNG image with valid Macintosh picture dimensions")
            pixels = image.convert("RGB").tobytes()
    except (OSError, SyntaxError, Image.DecompressionBombError) as error:
        raise ValueError("Invalid PNG picture") from error
    return width, height, pixels


def read_image(data):
    """Read PNG artwork or a compatible legacy RGB BMP source."""
    return read_png(data) if data[:8] == b"\x89PNG\r\n\x1a\n" else read_bmp(data)


def write_bmp(width, height, pixels):
    """Write lossless 24-bit BMP artwork with zero file-row padding."""
    if not 1 <= width <= 4095 or not 1 <= height <= 4095 or len(pixels) != width * height * 3:
        raise ValueError("Invalid RGB artwork dimensions or byte extent")
    stride = ((width * 3 + 3) // 4) * 4
    rows = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            offset = (y * width + x) * 3
            rows.extend(pixels[offset : offset + 3][::-1])
        rows.extend(bytes(stride - width * 3))
    return (
        struct.pack("<2sIHHI", b"BM", 54 + len(rows), 0, 0, 54)
        + struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(rows), 2835, 2835, 0, 0)
        + rows
    )


def resource(data, top=0, left=0):
    """Preserve the declared frame origin while replacing drawing commands with RGB pixels."""
    width, height, pixels = read_image(data)
    if not -32768 <= top <= 32767 - height or not -32768 <= left <= 32767 - width:
        raise ValueError("PICT frame exceeds Macintosh coordinates")
    bounds = struct.pack(">4h", top, left, top + height, left + width)
    picture = bytearray(b"\0\0" + bounds + b"\0\x11\x02\xff")
    # Extended v2 header: 72 dpi and a source rectangle in integer coordinates.
    picture += struct.pack(">HhHII", 0x0C00, -2, 0, 72 << 16, 72 << 16) + bounds + bytes(4)
    picture += struct.pack(">HH", 0x0001, 10) + bounds
    # DirectBitsRect, dummy baseAddr and PixMap. Pack type 2 omits pad bytes.
    picture += struct.pack(">HIH", 0x009A, 0xFF, 0x8000 | (width * 4)) + bounds
    picture += struct.pack(">HHIIIHHHHIII", 0, 2, 0, 72 << 16, 72 << 16, 16, 32, 3, 8, 0, 0, 0)
    picture += bounds + bounds + struct.pack(">H", 0) + pixels
    if len(picture) & 1:
        picture.append(0)
    picture += b"\0\xff"
    # A zero size is the classic convention for pictures larger than 64 KiB.
    struct.pack_into(">H", picture, 0, len(picture) if len(picture) <= 65535 else 0)
    return bytes(picture)
