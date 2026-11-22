#!/usr/bin/env python3
# +===================================================================
#   File:      build.py
#
#   Summary:   Build an SDK-free original 68K application for viewing built-in DSAT dialogs.
#
#   Classes:   None.
#
#   Functions: guest_code, build, main.
#
#   Origin:    Original fixture using the repository's classic CODE/resource-fork
#              layout and Toolbox trap declarations; contains no extracted code.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Build the DSAT dialog chooser, or a single-dialog app with --show CODE."""

import argparse
import importlib.util
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from resource_catalog import read_rez

CHOICES = [
    ("Default / Quit", 1, False),
    ("Unknown error type", 7, False),
    ("Low memory (25)", 25, False),
    ("Force Quit / Cancel", 20002, False),
    ("RAM parity (101)", 101, False),
    ("Code Fragment (1010)", 1010, False),
    ("Mixed Mode (1011)", 1011, False),
    ("Stack collision (28)", 28, False),
    ("Insert disk (30)", 30, True),
    ("Remove module (20003)", 20003, True),
    ("Disk in use (20004)", 20004, True),
    ("Icon alert -10", -10, True),
    ("Icon alert -11", -11, True),
    ("Icon alert -13", -13, True),
]


def guest_code(single=None):
    """Emit original trap glue with checked PC-relative branch/table fixups."""
    code, labels, fixups = bytearray(), {}, []

    def words(*values):
        code.extend(struct.pack(">" + "H" * len(values), *(v & 65535 for v in values)))

    def label(name):
        labels[name] = len(code)

    def relative(opcode, destination):
        fixups.append((len(code) + 2, destination))
        words(opcode, 0)

    # NewPtrClear(1024), with event/item storage and QuickDraw globals in A3.
    words(0x203C, 0, 1024, 0xA31E, 0x2648, 0x2008)
    relative(0x6700, "quit")
    words(0x486B, 508, 0xA86E, 0xA8FE, 0xA912, 0xA930, 0xA9CC, 0xA850, 0x42A7, 0xA97B)
    # InitGraf, InitFonts, InitWindows, InitMenus, TEInit, InitCursor, InitDialogs.
    if single is None:
        # GetNewDialog(128, NULL, (WindowPtr)-1), save returned pointer in A4.
        words(0x42A7, 0x3F3C, 128, 0x42A7, 0x2F3C, 0xFFFF, 0xFFFF, 0xA97C, 0x285F, 0x200C)
        relative(0x6700, "quit")
        label("chooser")
        words(0x2F0C, 0xA873, 0x2F0C, 0xA915, 0x2F0C, 0xA981)  # SetPort, ShowWindow, DrawDialog.
        words(0x42A7, 0x2F0B, 0xA991, 0x3013)  # ModalDialog(NULL, &item).
        words(0x0C40, len(CHOICES) + 2)
        relative(0x6700, "quit")
        words(0x5540)  # Convert first button (item 2) to table index zero.
        relative(0x6B00, "chooser")
        words(0x0C40, len(CHOICES))
        relative(0x6400, "chooser")
        words(0xD040, 0xD040)  # Four-byte {code, pause} table entry.
        relative(0x41FA, "errors")
        words(0x3430, 0, 0x5488, 0x3230, 0, 0x3741, 4)
        words(0x2F0C, 0xA916, 0x3002)  # HideWindow, put selected error in D0.
    else:
        words(0x377C, 1, 4, 0x303C, single)  # Pause any returning single preview.
    words(0xA9C9)  # SysError uses D0, not a Pascal stack argument.
    words(0x4A6B, 4)
    relative(0x6700, "return")
    label("wait")
    # Wait for a click/key after the buttonless icon/module previews.
    words(0x4267, 0x3F3C, 0x000A, 0x486B, 8, 0x2F3C, 0, 1, 0x42A7, 0xA860, 0x301F, 0x4A00)
    relative(0x6700, "wait")
    label("return")
    relative(0x6000, "chooser" if single is None else "quit")
    label("quit")
    words(0xA9F4)  # ExitToShell.
    label("errors")
    for _, error, pause in CHOICES:
        words(error, int(pause))
    for at, destination in fixups:
        displacement = labels[destination] - at
        if not -32768 <= displacement <= 32767:
            raise ValueError("Guest branch exceeds 16-bit displacement")
        struct.pack_into(">h", code, at, displacement)
    return bytes(code)


def build(output, single=None):
    """Publish a classic APPL and its AppleDouble companion in the output tree."""
    output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("system_resource_builder", ROOT / "tools/romaw/system_resources.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    name = "DSAT Dialogs" if single is None else f"DSAT Dialog {single}"
    items = [
        '{ 12, 14, 66, 566 }, StaticText { disabled, "Choose an alert. Quit, Restart and Shut Down end this test process. For icon/disk previews, click or press a key to return." };'
    ]
    for index, (title, _, _) in enumerate(CHOICES):
        row, column = divmod(index, 2)
        y, x = 80 + row * 34, 14 + column * 282
        items.append(f'{{ {y}, {x}, {y+24}, {x+268} }}, Button {{ enabled, "{title}" }};')
    items.append('{ 330, 414, 354, 564 }, Button { enabled, "Close chooser" };')
    rez = "resource 'DLOG' (128) { {40, 40, 408, 620}, movableDBoxProc, visible, noGoAway, 0, 128, \"DSAT Dialogs\", centerMainScreen };\n"
    rez += "resource 'DITL' (128) { {\n" + "\n".join(items) + "\n} };\n"
    resource_source = output / "dialogs.r"
    resource_source.write_text(rez, encoding="utf-8", newline="\n")
    records = [
        (b"CODE", 0, 0, None, struct.pack(">4I4H", 4096, 4096, 8, 32, 0, 0x3F3C, 1, 0xA9F0)),
        (b"CODE", 1, 0, None, struct.pack(">HH", 0, 1) + guest_code(single)),
        (b"SIZE", -1, 0, None, struct.pack(">HII", 0x80, 4 * 1024 * 1024, 4 * 1024 * 1024)),
    ]
    records += [
        (bytes.fromhex(e["type_hex"]), e["id"], e["attributes"], None, e["payload"]) for e in read_rez(resource_source)
    ]
    fork = builder.build_fork(records)
    sidecar = bytearray(builder.apple_double(fork))
    sidecar[50:58] = b"APPLDSAT"
    app = output / name
    app.write_bytes(b"")
    app.with_name("._" + name).write_bytes(sidecar)
    # Verify the packaged CODE and dialog resources through the bounded reader.
    from mac_resources import AppleDouble, ResourceFork

    packaged = ResourceFork(AppleDouble(bytes(sidecar)).get(2))
    assert packaged.get(b"CODE", 1)[4:] == guest_code(single)
    assert packaged.get(b"DITL", 128) == records[-1][-1]
    executable = ROOT / "build/syserr-port/stage/ROMAW-optimized.exe"
    if not executable.is_file():
        executable = ROOT / "windows-binaries/bin/ROMAW.exe"
    launcher = output / (name + ".cmd")
    launcher.write_text(f'@echo off\nstart "" "{executable}" "{app}"\n', encoding="utf-8", newline="\r\n")
    print(app)
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/dsat-dialogs")
    parser.add_argument("--show", type=int, help="Build a single-dialog preview instead of the chooser")
    args = parser.parse_args()
    if args.show is not None and not -32768 <= args.show <= 32767:
        parser.error("SysError code must fit a signed 16-bit word")
    build(args.output.resolve(), args.show)


if __name__ == "__main__":
    main()
