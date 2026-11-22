#!/usr/bin/env python3
# +===================================================================
#   File:      create-image-launch-probes.py
#
#   Summary:   Build original 68K launchContinue and replacing launch applications in HFS.
#
#   Classes:   None.
#
#   Functions: resource_fork, launcher, main.
#
#   Origin:    Executor mounted-image launch regression fixture; contains no Apple code.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
import struct
import subprocess
import sys
from pathlib import Path


def resource_fork(resources):
    groups = {}
    for kind, identifier, payload in resources:
        groups.setdefault(kind, []).append((identifier, payload))
    data, types, references = bytearray(), bytearray(), bytearray()
    for kind, entries in groups.items():
        types += kind + struct.pack(">HH", len(entries) - 1, 2 + 8 * len(groups) + len(references))
        for identifier, payload in entries:
            references += struct.pack(">hH", identifier, 65535) + len(data).to_bytes(4, "big") + bytes(4)
            data += struct.pack(">I", len(payload)) + payload
    table = struct.pack(">H", len(groups) - 1) + types + references
    header = struct.pack(">IIII", 256, 256 + len(data), len(data), 28 + len(table))
    return header + bytes(240) + data + header + bytes(8) + struct.pack(">HH", 28, 28 + len(table)) + table


def launcher(target, continuing):
    code, branches, addresses = bytearray(), [], []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *values))

    def address(opcode):
        addresses.append(len(code))
        words(opcode, 0)

    def failure():
        branches.append(len(code))
        words(0x6600, 0)

    words(0x203C, 0, 1024, 0xA31E, 0x2848)  # NewPtrClear; keep state in A4.
    loop = len(code)
    # Wait for the harness to post app4Evt, so every stage can be inspected.
    words(0x4267, 0x3F3C, 0xFFFF, 0x4854, 0x2F3C, 0, 1, 0x42A7, 0xA860, 0x101F, 0x0C54, 15)
    at = len(code)
    words(0x6600, (loop - at - 2) & 65535)
    if continuing:
        words(0x4267, 0x4267, 0x42A7)  # OSErr result, vref=0, directory=0.
        address(0x487A)
        words(0x486C, 64, 0x7001, 0xAA52, 0x301F, 0x4A40)  # FSMakeFSSpec selector 1.
        failure()
        words(0x397C, 0x4C43, 166, 0x297C, 0, 32, 168, 0x397C, 0x4000, 174)
        words(0x41EC, 64, 0x2948, 176)  # launchAppSpec.
    else:
        address(0x41FA)
        words(0x2948, 160)  # Legacy block points at a volume-qualified name.
    words(0x41EC, 160, 0xA9F2, 0x4A40)  # Execute the real guest Launch trap.
    failure()
    # launchContinue must keep the caller alive while its child runs.
    words(0x6000, (loop - len(code) - 2) & 65535)
    bad = len(code)
    words(0x4AFC)
    label = len(code)
    encoded = target.encode("mac_roman")
    code += bytes([len(encoded)]) + encoded
    for at in addresses:
        struct.pack_into(">h", code, at + 2, label - at - 2)
    for at in branches:
        struct.pack_into(">h", code, at + 2, bad - at - 2)
    code0 = struct.pack(">4I4H", 448, 0, 8, 32, 0, 0x3F3C, 1, 0xA9F0)
    return resource_fork(
        [
            (b"CODE", 0, code0),
            (b"CODE", 1, struct.pack(">HH", 0, 1) + code),
            (b"SIZE", -1, struct.pack(">HII", 0x80, 4 * 1024 * 1024, 4 * 1024 * 1024)),
        ]
    )


def main():
    root = Path(__file__).resolve().parents[2]
    work = Path(sys.argv[1]).resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / "ContinueLauncher.rsrc").write_bytes(launcher("Mount Handoff:ReplaceLauncher", True))
    (work / "ReplaceLauncher.rsrc").write_bytes(launcher("Disk Tools:Disk First Aid", False))
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            str(root / "system/tests/image_launch_fixture.cpp"),
            str(root / "build/images-direct-linux/libromaw-images.a"),
            "-o",
            str(work / "make-fixture"),
        ],
        check=True,
    )
    subprocess.run([str(work / "make-fixture"), str(work)], check=True)
    print(work / "Launchers.hfv")


if __name__ == "__main__":
    main()
