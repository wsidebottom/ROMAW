# +===================================================================
#   File:      test_disassemble.py
#
#   Summary:   Synthetic PEF fixtures; no Apple executable bytes are required.
#
#   Classes:   DisassemblyTests
#
#   Functions: pef_fixture
#
#   Origin:    Existing repository source in tools/classic-disasm; original attribution and provenance comments are
#              preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Synthetic PEF fixtures; no Apple executable bytes are required."""

import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("classic_disassemble", Path(__file__).with_name("disassemble.py"))
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
sys.path.insert(0, str(analysis.ROOT / "disasm/system9/python-packages"))


def pef_fixture():
    # A CFM import glue, a relative call back to it, and a return instruction.
    code = bytes.fromhex("8182000090410014800c0000804c00047c0903a64e8004204bffffe94e800020")
    data = struct.pack(">III", 24, 8, 0)
    names = b"ThreadsLib\0NewThread\0"
    loader = bytearray(56)
    struct.pack_into(">iIiIiI", loader, 0, 1, 0, -1, 0, -1, 0)
    struct.pack_into(">IIIII", loader, 24, 1, 1, 1, 96, 100)
    loader.extend(struct.pack(">IIIIII", 0, 0, 0, 1, 0, 0))
    loader.extend(struct.pack(">I", (2 << 24) | names.index(b"NewThread")))
    loader.extend(struct.pack(">HHII", 1, 0, 2, 0))
    # RelocTVector8 followed by RelocImportRun.
    loader.extend(struct.pack(">HH", 0x4600, 0x4A00))
    loader.extend(names)
    header = bytearray(40)
    header[:12] = b"Joy!peffpwpc"
    struct.pack_into(">I", header, 12, 1)
    struct.pack_into(">HH", header, 32, 4, 2)
    payloads = [(0, code), (1, data), (7, b"dbg!"), (4, bytes(loader))]
    offset = 40 + 4 * 28
    for kind, payload in payloads:
        size = len(payload) if kind in (0, 1) else 0
        header.extend(struct.pack(">iIIIIIBBBB", -1, 0, size, size, len(payload), offset, kind, 1, 0, 0))
        offset += len(payload)
    return bytes(header) + b"".join(payload for _, payload in payloads)


class DisassemblyTests(unittest.TestCase):
    def test_pattern_operations(self):
        self.assertEqual(analysis.unpack_pattern(b"\x03", 3), bytes(3))
        self.assertEqual(analysis.unpack_pattern(b"\x23abc", 3), b"abc")
        self.assertEqual(analysis.unpack_pattern(b"\x42\x01ab", 4), b"abab")
        self.assertEqual(analysis.unpack_pattern(b"\x61\x01\x02ABC", 5), b"ABACA")
        self.assertEqual(analysis.unpack_pattern(b"\x81\x01\x02BC", 5), b"\0B\0C\0")
        for payload, size in [(b"\x23a", 3), (b"\x03", 2), (b"\x42\xff", 4)]:
            with self.assertRaises(ValueError):
                analysis.unpack_pattern(payload, size)

    def test_relocation_repeat_and_bounds(self):
        result = analysis.relocations(struct.pack(">HH", 0x4A00, 0x9000), 8, 2)
        self.assertEqual(result, {0: ("import", 0), 4: ("import", 1)})
        with self.assertRaises(ValueError):
            analysis.relocations(struct.pack(">H", 0x4A01), 4, 2)

    def test_fragment_entries_imports_and_noninstantiated_debug_section(self):
        sections, imports, _, relocs, entries = analysis.parse_pef(pef_fixture())
        self.assertEqual(sections[2]["payload"], b"dbg!")
        self.assertEqual(imports[0]["name"], "NewThread")
        self.assertEqual(relocs[1][8], ("import", 0))
        self.assertEqual(entries[0]["section"], 1)
        for length in (0, 12, 40, 100, len(pef_fixture()) - 10):
            with self.assertRaises((ValueError, StopIteration)):
                analysis.parse_pef(pef_fixture()[:length])

    def test_import_call_annotation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            analysis.pef_listing(pef_fixture(), path)
            metadata = json.loads((path / "pef.json").read_text())
            self.assertEqual(metadata["entries"][0]["code_offset"], 24)
            self.assertEqual(len(metadata["import_stubs"]), 1)
            self.assertEqual(metadata["imported_calls"][0]["offset"], 24)
            self.assertIn("ThreadsLib:NewThread", (path / "section-0.ppc.s").read_text())


if __name__ == "__main__":
    unittest.main()
