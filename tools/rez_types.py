# +===================================================================
#   File:      rez_types.py
#
#   Summary:   Encode typed classic resources and native Finder menu configuration.
#
#   Classes:   RezFields
#
#   Functions: compile_alert_table, compile_resource
#
#   Origin:    Classic resource layouts and conventions used by Executor's existing
#              Rez sources and references/finder7/tools/readable_rez.py.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Encode the typed classic and native layouts used by the resource sources.

This is a deliberately small field parser, not a general Rez template compiler.
Unknown types remain available to the native Finder's layout readers.
"""

import re
import struct

from rez_system_types import NATIVE_OFFSETS, SYSTEM_TYPES, compile_system_resource

QUOTED = r'"(?:\\.|[^"\\])*"'
SYMBOLS = {
    "documentProc": 0,
    "dBoxProc": 1,
    "plainDBox": 2,
    "altDBoxProc": 3,
    "noGrowDocProc": 4,
    "movableDBoxProc": 5,
    "zoomDocProc": 8,
    "noAutoCenter": 0,
    "centerMainScreen": 0x280A,
    "alertPositionMainScreen": 0x300A,
    "staggerMainScreen": 0x380A,
    "centerParentWindow": 0xA80A,
    "alertPositionParentWindow": 0xB00A,
    "staggerParentWindow": 0xB80A,
    "centerParentWindowScreen": 0x680A,
    "alertPositionParentWindowScreen": 0x700A,
    "staggerParentWindowScreen": 0x780A,
    "development": 0x20,
    "alpha": 0x40,
    "beta": 0x60,
    "final": 0x80,
    "verUS": 0,
    "verBritain": 2,
}
SYMBOLS.update(NATIVE_OFFSETS)

ITEM_TYPES = {
    "HelpItem": 1,
    "UserItem": 0,
    "Button": 4,
    "CheckBox": 5,
    "RadioButton": 6,
    "Control": 7,
    "StaticText": 8,
    "EditText": 16,
    "Icon": 32,
    "Picture": 64,
}
LABEL_MENU_FIELDS = (
    "frame_width",
    "frame_height",
    "canvas_width",
    "canvas_height",
    "header_height",
    "item_height",
    "separator_y",
    "separator_gray",
    "text_x",
    "none_text_x",
    "sprite_x",
    "sprite_y",
    "check_x",
    "check_y",
    "icon_resource",
    "icon_color_index",
)
SUPPORTED_TYPES = SYSTEM_TYPES | {
    "STR ",
    "STR#",
    "MACS",
    "vers",
    "ALRT",
    "DLOG",
    "DITL",
    "lMnu",
    "clut",
    "wctb",
    "DSAT",
}


class RezFields:
    """Consume fields strictly, with checked integer widths and byte strings."""

    def __init__(self, source, decode_string):
        pattern = re.compile(
            QUOTED + r"|\$\$(?:icon|pattern|pixels)|-?(?:0[xX][0-9a-fA-F]+|[0-9]+)|[A-Za-z_][A-Za-z_0-9]*|[{}(),;]"
        )
        self.tokens = []
        position = 0
        for match in pattern.finditer(source):
            if source[position : match.start()].strip():
                raise ValueError("Unsupported typed Rez expression")
            self.tokens.append(match[0])
            position = match.end()
        if source[position:].strip():
            raise ValueError("Invalid typed Rez field")
        self.position = 0
        self.decode_string = decode_string

    def peek(self):
        return self.tokens[self.position] if self.position < len(self.tokens) else None

    def take(self):
        result = self.peek()
        if result is None:
            raise ValueError("Truncated typed Rez resource")
        self.position += 1
        return result

    def expect(self, value):
        if self.take() != value:
            raise ValueError(f"Expected {value!r} in typed Rez resource")

    def choice(self, values):
        token = self.take()
        if token not in values:
            raise ValueError(f"Invalid typed Rez choice: {token}")
        return values[token]

    def number(self, low, high):
        token = self.take()
        if token in SYMBOLS:
            value = SYMBOLS[token]
        else:
            try:
                value = int(token, 16 if "x" in token.lower() else 10)
            except ValueError as error:
                raise ValueError(f"Invalid typed Rez number: {token}") from error
        if not low <= value <= high:
            raise ValueError("Typed Rez number out of range")
        return value

    def string(self):
        if self.peek() is None or not self.peek().startswith('"'):
            raise ValueError("Expected typed Rez string")
        value = bytearray()
        while self.peek() is not None and self.peek().startswith('"'):
            value.extend(self.decode_string(self.take()))
        if len(value) > 255:
            raise ValueError("Typed Rez string exceeds Str255")
        return bytes([len(value)]) + value

    def rect(self):
        self.expect("{")
        values = []
        for index in range(4):
            if index:
                self.expect(",")
            values.append(self.number(-32768, 32767))
        self.expect("}")
        return struct.pack(">4h", *values)

    def finish(self):
        if self.peek() is not None:
            raise ValueError("Unexpected trailing typed Rez fields")


def compile_alert_table(fields, read_icon=None):
    """Encode DSAlertTab records, checking their explicit legacy payload lengths."""
    fields.expect("{")
    records = []
    identities = set()
    while fields.peek() != "}":
        identity = fields.number(-32768, 32767)
        if identity in identities:
            raise ValueError("Duplicate system-error alert record ID")
        identities.add(identity)
        fields.expect(",")
        length = fields.number(0, 65535)
        fields.expect(",")
        kind = fields.choice(
            {
                key: key
                for key in ("Alert", "AlertExtended", "Text", "Icon", "Buttons", "ButtonText", "Procedure", "Rectangle")
            }
        )
        fields.expect("{")
        payload = bytearray()
        if kind in {"Alert", "AlertExtended"}:
            for index in range(6 if kind == "AlertExtended" else 5):
                if index:
                    fields.expect(",")
                payload.extend(struct.pack(">h", fields.number(-32768, 32767)))
        elif kind in {"Text", "ButtonText"}:
            if kind == "Text":
                fields.expect("{")
                vertical = fields.number(-32768, 32767)
                fields.expect(",")
                horizontal = fields.number(-32768, 32767)
                fields.expect("}")
                fields.expect(",")
                payload.extend(struct.pack(">hh", vertical, horizontal))
            payload.extend(fields.decode_string(fields.take()))
            if not payload or payload[-1] != 0:
                raise ValueError("System-error text must include its terminating NUL")
            if len(payload) % 2:
                raise ValueError("System-error text must include word-alignment padding")
        elif kind == "Icon":
            payload.extend(fields.rect())
            fields.expect(",")
            fields.expect("$$icon")
            fields.expect("(")
            filename = fields.decode_string(fields.take()).decode("mac_roman")
            fields.expect(")")
            if read_icon is None:
                raise ValueError("System-error icons require an artwork reader")
            bitmap = read_icon(filename)
            if len(bitmap) != 128:
                raise ValueError("System-error icon must contain 128 monochrome bytes")
            payload.extend(bitmap)
        elif kind == "Buttons":
            fields.expect("{")
            buttons = []
            while fields.peek() != "}":
                button = struct.pack(">h", fields.number(-32768, 32767))
                fields.expect(",")
                button += fields.rect()
                fields.expect(",")
                button += struct.pack(">h", fields.number(-32768, 32767))
                buttons.append(button)
                if fields.peek() != "}":
                    fields.expect(";")
            fields.expect("}")
            payload.extend(struct.pack(">h", len(buttons)))
            payload.extend(b"".join(buttons))
        elif kind == "Rectangle":
            payload.extend(fields.rect())
        else:
            payload.extend(struct.pack(">i", fields.number(-2147483648, 2147483647)))
        fields.expect("}")
        if len(payload) != length:
            raise ValueError("System-error alert record length does not match its payload")
        records.append(struct.pack(">hH", identity, length) + payload)
        if fields.peek() != "}":
            fields.expect(";")
    fields.expect("}")
    return struct.pack(">h", len(records)) + b"".join(records)


def compile_resource(kind, body, decode_string, read_icon=None, read_artwork=None):
    """Return encoded bytes for a supported layout, or None for an unknown type."""
    if kind not in SUPPORTED_TYPES:
        return None
    fields = RezFields(body, decode_string)
    if kind in SYSTEM_TYPES:
        return compile_system_resource(kind, fields, read_artwork)
    result = bytearray()
    if kind == "DSAT":
        result.extend(compile_alert_table(fields, read_icon))
    elif kind in {"STR ", "MACS"}:
        result.extend(fields.string())
    elif kind == "STR#":
        fields.expect("{")
        strings = []
        while fields.peek() != "}":
            strings.append(fields.string())
            if fields.peek() == "}":
                break
            fields.expect(";")
        fields.expect("}")
        if len(strings) > 32767:
            raise ValueError("Typed Rez string list exceeds signed count")
        result.extend(struct.pack(">H", len(strings)))
        result.extend(b"".join(strings))
    elif kind == "vers":
        numbers = []
        for high in (255, 255, 255, 255, 65535):
            numbers.append(fields.number(0, high))
            fields.expect(",")
        major, minor, stage, revision, region = numbers
        result.extend(struct.pack(">4BH", major, minor, stage, revision, region))
        result.extend(fields.string())
        fields.expect(",")
        result.extend(fields.string())
    elif kind == "ALRT":
        result.extend(fields.rect())
        fields.expect(",")
        result.extend(struct.pack(">h", fields.number(-32768, 32767)))
        fields.expect(",")
        fields.expect("{")
        stages = 0
        for _ in range(4):
            button = fields.choice({"OK": 0, "Cancel": 8})
            fields.expect(",")
            visible = fields.choice({"invisible": 0, "visible": 4})
            fields.expect(",")
            sound = fields.choice({"silent": 0, "sound1": 1, "sound2": 2, "sound3": 3})
            fields.expect(";")
            stages = stages << 4 | button | visible | sound
        fields.expect("}")
        result.extend(struct.pack(">H", stages))
        if fields.peek() == ",":
            fields.expect(",")
            result.extend(struct.pack(">H", fields.number(0, 65535)))
    elif kind == "DLOG":
        result.extend(fields.rect())
        fields.expect(",")
        proc = fields.number(-32768, 32767)
        fields.expect(",")
        visible = fields.choice({"invisible": 0, "visible": 1})
        fields.expect(",")
        away = fields.choice({"noGoAway": 0, "goAway": 1})
        fields.expect(",")
        ref = fields.number(-2147483648, 4294967295)
        fields.expect(",")
        items = fields.number(-32768, 32767)
        fields.expect(",")
        result.extend(struct.pack(">hBBBBIh", proc, visible, 0, away, 0, ref & 0xFFFFFFFF, items))
        result.extend(fields.string())
        # Older DLOG resources end immediately after the title. Only the newer
        # optional positioning field requires the intervening word alignment.
        if fields.peek() == ",":
            fields.expect(",")
            result.extend(b"\0" * (len(result) & 1))
            result.extend(struct.pack(">H", fields.number(0, 65535)))
    elif kind == "DITL":
        fields.expect("{")
        items = []
        while fields.peek() != "}":
            rect = fields.rect()
            fields.expect(",")
            item_type = fields.choice(ITEM_TYPES)
            fields.expect("{")
            flags = item_type | fields.choice({"enabled": 0, "disabled": 128})
            if item_type in {4, 5, 6, 8, 16}:
                fields.expect(",")
                data = fields.string()
            elif item_type == 1:
                fields.expect(",")
                subtype = fields.number(-32768, 32767)
                fields.expect(",")
                data = b"\x04" + struct.pack(">hh", subtype, fields.number(-32768, 32767))
            elif item_type in {7, 32, 64}:
                fields.expect(",")
                data = b"\x02" + struct.pack(">h", fields.number(-32768, 32767))
            else:
                data = b"\0"
            padding = None
            if fields.peek() == ",":
                fields.expect(",")
                padding = fields.number(0, 255)
            fields.expect("}")
            fields.expect(";")
            item = b"\0" * 4 + rect + bytes([flags]) + data
            if padding is not None and not len(item) & 1:
                raise ValueError("Dialog alignment byte supplied for an already aligned item")
            items.append(item + bytes((0 if padding is None else padding,)) * (len(item) & 1))
        fields.expect("}")
        if len(items) > 32768:
            raise ValueError("Typed Rez item list exceeds signed count")
        result.extend(struct.pack(">h", len(items) - 1))
        result.extend(b"".join(items))
    elif kind in {"clut", "wctb"}:
        seed = fields.number(-2147483648, 4294967295)
        fields.expect(",")
        flags = fields.number(-32768, 65535)
        fields.expect(",")
        fields.expect("{")
        colours = []
        while fields.peek() != "}":
            index = fields.number(-32768, 65535)
            rgb = []
            for _ in range(3):
                fields.expect(",")
                rgb.append(fields.number(0, 65535))
            fields.expect(";")
            colours.append(struct.pack(">4H", index & 65535, *rgb))
            if len(colours) > 32768:
                raise ValueError("Typed Rez colour table exceeds signed count")
        fields.expect("}")
        result.extend(struct.pack(">IHh", seed & 0xFFFFFFFF, flags & 65535, len(colours) - 1))
        result.extend(b"".join(colours))
    elif kind == "lMnu":
        for index, name in enumerate(LABEL_MENU_FIELDS):
            low, high = (1, 32767) if index < 6 else (-32768, 32767)
            if name == "separator_gray":
                low, high = 0, 255
            if name == "icon_color_index":
                low, high = 0, 255
            result.extend(struct.pack(">h", fields.number(low, high)))
            fields.expect(",")
        fields.expect("{")
        for _ in range(8):
            result.extend(fields.string())
            for _ in range(3):
                fields.expect(",")
                result.append(fields.number(0, 255))
            fields.expect(";")
        fields.expect("}")
        fields.expect(",")
        fields.expect("{")
        for _ in range(8):
            row = fields.string()
            if row[0] != 9 or set(row[1:]) - {ord("."), ord("#")}:
                raise ValueError("The checkmark must be an 8 by 9 bitmap of dots and hashes")
            result.extend(row)
            fields.expect(";")
        fields.expect("}")
    fields.finish()
    return bytes(result)
