# +===================================================================
#   File:      runtime_fixtures.py
#
#   Summary:   Original guest application and desk-accessory fixtures for ROMAW tests.
#
#   Classes:   None declared.
#
#   Functions: module, make_68k, drvr_id
#
#   Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Original guest application and desk-accessory fixtures for ROMAW tests."""

import importlib.util
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def module(name, path):
    """Load a maintained fixture generator without depending on its import name."""
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def make_68k(directory):
    """Write an original terminating 68K app and its AppleDouble resource fork."""
    resources = module("resources", ROOT / "tools/romaw/system_resources.py")
    # Original terminating 68K application. SIZE explicitly permits 32-bit mode.
    code0 = struct.pack(">4I", 4096, 4096, 8, 32) + bytes.fromhex("00003f3c0001a9f0")
    fork = resources.build_fork(
        [
            (b"CODE", 0, 0, None, code0),
            (b"CODE", 1, 0, None, bytes.fromhex("00000001a9f4")),
            (b"SIZE", -1, 0, None, struct.pack(">HII", 0x80, 1048576, 1048576)),
        ]
    )
    app = directory / "68K Launcher Probe"
    app.write_bytes(b"")
    app.with_name("._" + app.name).write_bytes(
        struct.pack(">II16sH6I", 0x51607, 0x20000, bytes(16), 2, 9, 50, 32, 2, 82, len(fork))
        + b"APPLF64T"
        + bytes(24)
        + fork
    )
    return app


def drvr_id(path):
    """Locate a DRVR ID in an installed test asset; malformed fixtures fail here."""
    b = path.with_name("._" + path.name).read_bytes()
    for i in range(struct.unpack_from(">H", b, 24)[0]):
        entry, start, size = struct.unpack_from(">III", b, 26 + 12 * i)
        if entry != 2:
            continue
        fork = b[start : start + size]
        map_offset = struct.unpack_from(">I", fork, 4)[0]
        types = map_offset + struct.unpack_from(">H", fork, map_offset + 24)[0]
        for j in range(struct.unpack_from(">H", fork, types)[0] + 1):
            position = types + 2 + 8 * j
            if fork[position : position + 4] == b"DRVR":
                refs = types + struct.unpack_from(">H", fork, position + 6)[0]
                return struct.unpack_from(">h", fork, refs)[0]
    raise RuntimeError("No DRVR found")
