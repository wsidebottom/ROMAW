#!/usr/bin/env python3
# +===================================================================
#   File:      check_fileplay_capture.py
#
#   Summary:   Check real SDL sink samples against PCM and independently hashed MACE PCM.
#
#   Classes:   None declared.
#
#   Functions: checksum, check_capture
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Check real SDL sink samples against PCM and independently hashed MACE PCM."""
import re
import sys
from pathlib import Path


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def check_capture(directory):
    log = (directory / "stderr.txt").read_text(errors="replace")
    match = re.search(r"SDL test audio: no host device, (\d+) Hz", log)
    assert match, "The test-only output driver was not selected"
    assert "PowerPC fragment completed" in log
    captured = (directory / "output.raw").read_bytes()
    step = (22050 << 32) // int(match[1])
    pcm = bytes(136 if index & 16 else 120 for index in range(4410))
    mace3 = (directory / "reference-mac3.pcm").read_bytes()
    mace6 = (directory / "reference-mac6.pcm").read_bytes()
    assert len(mace3) == 6144 and checksum(mace3) == 0x5AF088AB
    assert len(mace6) == 12288 and checksum(mace6) == 0x13EBA7F1
    position = 0
    for name, source in [
        ("synchronous PCM", pcm),
        ("asynchronous PCM", pcm),
        ("sound resource", bytes([136]) * 4410),
        ("MACE 3:1", mace3),
        ("MACE 6:1", mace6),
    ]:
        length = (len(source) * (1 << 32) + step - 1) // step
        expected = bytes(source[(index * step) >> 32] for index in range(length))
        start = captured.find(expected, position)
        assert start >= 0, f"Captured {name} differs from reference PCM"
        assert all(byte == 128 for byte in captured[position:start]), f"Unexpected non-silent samples before {name}"
        position = start + len(expected)
    assert all(byte == 128 for byte in captured[position:]), "Non-silent tail"
    print("Captured PCM, resource and both MACE outputs match exactly; gaps are silent")


if __name__ == "__main__":
    check_capture(Path(sys.argv[1]))
