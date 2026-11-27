# +===================================================================
#   File:      patch-finder-618.py
#
#   Summary:   Disable Finder 6.1.8's disk System version comparison with a guarded local patch.
#
#   Classes:   None.
#
#   Functions: patch, main (script entry point).
#
#   Origin:    Local disassembly of the user's Finder 6.1.8 CODE 1 resource.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
#              User-supplied application binaries are not distributed here.
# ===================================================================+

"""Patch a local Finder resource fork, preserving a hash-named original backup."""

import argparse
import hashlib
from pathlib import Path

from mac_resources import AppleDouble, ResourceFork, unpack


def patch(application, backup_directory):
    companion = application.with_name("._" + application.name)
    original = companion.read_bytes()
    double = AppleDouble(original)
    fork = ResourceFork(double.get(2))
    code = fork.get(b"CODE", 1)
    if fork.get(b"vers", 1)[:2] != bytes.fromhex("0618") or len(code) != 20932:
        raise ValueError("Expected the inspected Finder 6.1.8 application")
    # The function at 0x431c opens the selected file's resource fork, reads
    # vers 1, restores resource state, then returns whether that version is
    # older than SysVersion. Retain all resource ownership/cleanup operations.
    # D0 is already zero: replace CMP/SLT/NEG with NOPs to return false.
    offset = 0x436C
    before = bytes.fromhex("b878015a5dc04400")
    after = bytes.fromhex("4e714e714e714e71")
    if code[0x431C:0x4324] != bytes.fromhex("4e56fff848e73800") or code[offset - 2 : offset] != b"\x70\x00":
        raise ValueError("Version-check function does not match the inspected code")
    if code[offset : offset + len(after)] == after:
        print(f"Already patched: {application}")
        return
    if code[offset : offset + len(before)] != before:
        raise ValueError("Version comparison does not match; file was not changed")
    _, _, reference = fork.records[(b"CODE", 1)]
    resource_offset = unpack(">I", fork.map, reference + 4)[0] & 0xFFFFFF
    position = double.entries[2][0] + fork.data_offset + resource_offset + 4 + offset
    updated = original[:position] + after + original[position + len(before) :]
    check = ResourceFork(AppleDouble(updated).get(2))
    for key, (payload, attributes, _) in fork.records.items():
        expected = code[:offset] + after + code[offset + len(before) :] if key == (b"CODE", 1) else payload
        assert check.records[key][:2] == (expected, attributes), key
    assert updated[:position] == original[:position]
    assert updated[position + len(after) :] == original[position + len(before) :]
    backup_directory.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(original).hexdigest()
    backup = backup_directory / f"Finder-6.1.8-{digest}.appledouble"
    if backup.exists():
        if backup.read_bytes() != original:
            raise ValueError("Backup contents do not match")
    else:
        with backup.open("xb") as destination:
            destination.write(original)
    companion.write_bytes(updated)
    assert companion.read_bytes() == updated
    print(f"Patched CODE 1 +0x{offset:x}: {application}")
    print(f"Original resource fork: {backup}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path)
    parser.add_argument("--backup-directory", required=True, type=Path)
    args = parser.parse_args()
    patch(args.application, args.backup_directory)


if __name__ == "__main__":
    main()
