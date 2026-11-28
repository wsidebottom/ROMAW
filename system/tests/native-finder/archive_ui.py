# +===================================================================
#   File:      archive_ui.py
#
#   Summary:   Exercise native archives, Windows/Mac transfers and metadata inspection in an isolated Finder.
#
#   Classes:   ListItem; uses the existing private desktop process harness.
#
#   Functions: main, cleanup_drives.
#
#   Origin:    ROMAW native Finder regression fixtures; no production test hooks.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Test the staged executable with private files, profile, desktop and clipboard."""
import argparse
import atexit
import binascii
import ctypes as c
from ctypes import wintypes as w
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import zipfile

from private_desktop import api, private_desktop

ROOT = Path(__file__).resolve().parents[3]
DRIVE_MAPPINGS = []


def cleanup_drives():
    for define, letter, target in reversed(DRIVE_MAPPINGS):
        assert define(1 | 2 | 4 | 8, letter, target), "Cannot remove the exact private drive mapping"


atexit.register(cleanup_drives)


class ListItem(c.Structure):
    _fields_ = [
        ("mask", w.UINT),
        ("item", c.c_int),
        ("subitem", c.c_int),
        ("state", w.UINT),
        ("stateMask", w.UINT),
        ("text", c.c_void_p),
        ("capacity", c.c_int),
        ("image", c.c_int),
        ("param", w.LPARAM),
        ("group", c.c_int),
        ("columns", w.UINT),
        ("indexes", c.c_void_p),
        ("formats", c.c_void_p),
        ("groupIndex", c.c_int),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=ROOT / "windows-binaries/bin/ROMAW.exe")
    parser.add_argument("--import-format", choices=("OSX", "Netatalk", "BasiliskII", "Executor"))
    parser.add_argument(
        "--image-reader", default="/mnt/d/Source/ROMAW/build/archive-image-reader/archive-image-readback"
    )
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    digest = hashlib.sha256(runtime.read_bytes()).hexdigest()
    base = Path(tempfile.mkdtemp(prefix="romaw-archive-ui-"))
    windows = Path(tempfile.mkdtemp(prefix="archive-ui-windows-", dir=ROOT / "build"))
    backing = windows
    drive_format = ""
    if args.import_format:
        # Map an unused letter to this disposable D: directory. Declaring the
        # fixture Mac-compatible must never register Desktop DBs on the user's D: root.
        kernel = c.WinDLL("kernel32", use_last_error=True)
        query = api(kernel, "QueryDosDeviceW", w.DWORD, w.LPCWSTR, w.LPWSTR, w.DWORD)
        define = api(kernel, "DefineDosDeviceW", w.BOOL, w.DWORD, w.LPCWSTR, w.LPCWSTR)
        for letter in reversed("EFGHIJKLMNOPQRSTUVWXYZ"):
            name = letter + ":"
            result = c.create_unicode_buffer(32768)
            if not query(name, result, len(result)):
                target = "\\??\\" + str(backing.resolve())
                assert define(1 | 8, name, target), c.get_last_error()
                DRIVE_MAPPINGS.append((define, name, target))
                windows = Path(name + "/Destination")
                windows.mkdir()
                drive_format = f"Format{letter}={args.import_format}\n"
                break
        else:
            raise AssertionError("No unused drive letter for the disposable Mac-format fixture")
    profile = base / "profile"
    apps = profile / ".romaw/Apps"
    desktop_path = apps / "Desktop Folder"
    desktop_path.mkdir(parents=True)
    (apps / "System Folder").mkdir()
    local = profile / "AppData/Local"
    (local / "Finder").mkdir(parents=True)
    (profile / "AppData/Roaming").mkdir()
    (apps.parent / "ROMAW.prefs").write_text(
        "[HostDrives]\nSidecarDrives=none\nFormatC=Windows\n"
        + ("" if args.import_format else "FormatD=Windows\n")
        + drive_format
        + '\n[APPL.CFGF]\nWindowName="Existing title"\nSize=640x480\nPreferM68K=0\n'
        + "Gestalt('keep')={0,1};\nGestalt('gone')={0,2};\nDisplayScale=125\n"
    )
    (apps.parent / "ROMAW.ftm").write_text(
        "ZIP,,APPL,BAD!,x,x,Attempted override\nSIT,,APPL,BAD!,x,x,Attempted override\n"
        "FOO,x,DATA,TEST,,x,Mapped Windows import\nTXT,,TEXT,ttxt,,x,Text document\n"
        "FOO,,BADD,BAD!,x,,Wrong direction must not override\n"
    )
    spec = importlib.util.spec_from_file_location("archive_fixtures", ROOT / "archives/tests/check.py")
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    fixtures.DATA = b"Native Finder archive data fork\n" * 31
    icon = struct.pack(">I", 256) + bytes([0x55]) * 128 + bytes([0xFF]) * 128
    header = struct.pack(">IIII", 256, 256 + len(icon), len(icon), 50)
    resource = (
        header
        + bytes(240)
        + icon
        + header
        + bytes(8)
        + struct.pack(">HHH4sHHhHI4x", 28, 50, 0, b"ICN#", 0, 10, -16455, 65535, 0)
    )
    fixtures.RESOURCE = resource
    fixtures.FINDER = b"TEXTttxt\x01\0" + struct.pack(">hhH", 40, 40, 0) + bytes(range(16))
    if args.import_format:
        fixtures.FINDER = fixtures.FINDER[:8] + b"\x05\0" + fixtures.FINDER[10:]
    ad = fixtures.appledouble()
    if args.import_format == "OSX":
        (windows / ".DS_Store").write_bytes(b"")
    elif args.import_format == "Netatalk":
        (windows / ".AppleDouble").mkdir()
    elif args.import_format == "BasiliskII":
        (windows / ".finf").mkdir()
    elif args.import_format == "Executor":
        (windows / "%FormatMarker").write_bytes(ad)
    verify = desktop_path / "Verify"
    verify.mkdir()
    from runtime_fixtures import module

    resources = module("app_config_resources", ROOT / "tools/romaw/system_resources.py")
    member = b"pwpc" + bytes(18) + b"\x01\x01" + bytes(16) + struct.pack(">H", 44) + bytes(2)
    for name, creator, fat in (("FAT Fixture", b"CFGF", True), ("68k Fixture", b"CFGM", False)):
        entries = [(b"CODE", 0, 0, None, bytes(16))]
        if fat:
            entries.append((b"cfrg", 0, 0, None, bytes(28) + struct.pack(">I", 1) + member))
        fork = resources.build_fork(entries)
        (verify / name).touch()
        (verify / ("._" + name)).write_bytes(
            struct.pack(">II16xHIIIIII", 0x51607, 0x20000, 2, 9, 50, 32, 2, 82, len(fork))
            + b"APPL" + creator + bytes(24) + fork
        )
    incompatible = (
        ("Keyboard Layout", b"kfil", "Keyboard files are not compatible with ROMAW."),
        ("Printer Driver", b"PRER", "Printer files are not compatible with ROMAW."),
        ("Chooser Driver", b"RDEV", "Chooser items don't work with ROMAW."),
    )
    for name, type_code, _ in incompatible:
        (verify / name).touch()
        (verify / ("._" + name)).write_bytes(
            struct.pack(">II16xHIII", 0x51607, 0x20000, 1, 9, 38, 32) + type_code + b"MACS" + bytes(24)
        )
    windows_copy = base / "Windows Copy"
    windows_copy.mkdir()
    (windows / "OrdinaryWindows").mkdir()
    (windows / "OrdinaryWindows/Data").write_bytes(b"ordinary Windows folder")

    def zipped(root_name):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr(root_name + "/", b"")
            archive.writestr(root_name + "/Test", fixtures.DATA)
            archive.writestr("__MACOSX/" + root_name + "/._Test", ad)
        return data.getvalue()

    scroll_zip = io.BytesIO()
    with zipfile.ZipFile(scroll_zip, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in ("Near One.txt", "Near Two.txt"):
            archive.writestr(name, fixtures.DATA)

    cases = {}
    for name, extension, raw in (
        ("Zip Here", "zip", zipped("ZipPayload")),
        ("Scrolled Zip", "zip", scroll_zip.getvalue()),
        ("By Type", "", zipped("TypePayload")),
        ("Zip Picker", "zip", zipped("PickerPayload")),
        ("BinHex", "hqx", fixtures.binhex()),
        ("MacBinary", "bin", fixtures.macbinary(130)),
        ("StuffIt", "sit", fixtures.stuffit_classic()),
        ("Image Archive", "zip", zipped("ImagePayload")),
        ("Image Source", "zip", zipped("ImageSourcePayload")),
    ):
        folder = desktop_path / name
        folder.mkdir()
        source = folder / ("Archive" + ("." + extension if extension else ""))
        source.write_bytes(raw)
        if not extension:
            info = b"ZIP BAD!" + bytes(24)
            source.with_name("._" + source.name).write_bytes(
                struct.pack(">II16xHIII", 0x51607, 0x20000, 1, 9, 38, 32) + info
            )
        cases[name] = (folder, source)
    for name in ("PackageZip", "PackageSit", "PackageMoveZip", "PackageFailedMove"):
        folder = desktop_path / name
        folder.mkdir()
        (folder / "Test").write_bytes(fixtures.DATA)
        (folder / "._Test").write_bytes(ad)
    (desktop_path / "PackageFailedMove/._Test").write_bytes(b"bad")
    binary_cases = []
    for storage in ("OSX", "Netatalk", "BasiliskII", "ROMAW"):
        for moving in (False, True):
            name = "MB" + storage + ("Move" if moving else "Copy")
            path = desktop_path / name
            path.write_bytes(fixtures.DATA)
            if storage == "OSX":
                companion = path.with_name("._" + name)
            elif storage == "Netatalk":
                companion = desktop_path / ".AppleDouble" / name
            elif storage == "ROMAW":
                companion = path.with_name("%" + name)
            else:
                companion = desktop_path / ".finf" / name
                (desktop_path / ".rsrc").mkdir(exist_ok=True)
                (desktop_path / ".rsrc" / name).write_bytes(resource)
            companion.parent.mkdir(exist_ok=True)
            companion.write_bytes(fixtures.FINDER if storage == "BasiliskII" else ad)
            binary_cases.append((name, moving, windows, companion))
    for name in ("MBSameVolume", "MBFailedMove", "MBBlockedMove"):
        path = desktop_path / name
        path.write_bytes(fixtures.DATA)
        companion = path.with_name("._" + name)
        companion.write_bytes(b"bad" if name == "MBFailedMove" else ad)
        binary_cases.append((name, True, windows_copy if name == "MBSameVolume" else windows, companion))
    (windows / "MBBlockedMove.bin").mkdir()
    (windows / "MBOSXCopy").write_bytes(b"unrelated unwrapped destination")
    (windows / "PlainWindows.txt").write_bytes(b"ordinary Windows file")
    incoming = []
    if args.import_format:

        def wrapped(name, data=fixtures.DATA, finder=fixtures.FINDER, fork=resource):
            header = bytearray(fixtures.macbinary(130)[:128])
            header[1:65] = bytes(64)
            encoded = name.encode("mac_roman")
            header[1] = len(encoded)
            header[2 : 2 + len(encoded)] = encoded
            header[65:73] = finder[:8]
            header[73] = finder[8]
            header[101] = finder[9]
            struct.pack_into(">II", header, 83, len(data), len(fork))
            secondary = struct.pack(">II16xHIIIIII", 0x51607, 0x20000, 2, 9, 50, 32, 2, 82, 0) + finder
            struct.pack_into(">H", header, 120, len(secondary))
            struct.pack_into(">H", header, 124, binascii.crc_hqx(header[:124], 0))
            return (
                bytes(header)
                + secondary
                + bytes(-len(secondary) % 128)
                + data
                + bytes(-len(data) % 128)
                + fork
                + bytes(-len(fork) % 128)
            )

        for outer, inner, moving in (("Copy.bin", "DecodedCopy", False), ("Move.bin", "DecodedMove", True)):
            (windows_copy / outer).write_bytes(wrapped(inner))
            incoming.append((outer, inner, moving, fixtures.DATA, fixtures.FINDER, resource))
        for version in (0, 129, 130):
            inner = "Legacy" + str(version)
            raw = bytearray(fixtures.macbinary(version))
            raw[1:65] = bytes(64)
            raw[1] = len(inner)
            raw[2 : 2 + len(inner)] = inner.encode()
            if version:
                struct.pack_into(">H", raw, 124, binascii.crc_hqx(raw[:124], 0))
            finder = bytearray(32)
            finder[:8], finder[8], finder[10:16] = raw[65:73], raw[73], raw[75:81]
            if version:
                finder[9] = raw[101]
            if version == 130:
                finder[24:26] = raw[106:108]
            outer = inner + ".bin"
            (windows_copy / outer).write_bytes(raw)
            incoming.append((outer, inner, version == 129, fixtures.DATA, bytes(finder), resource))
        (windows_copy / "Explicit.foo").write_bytes(fixtures.DATA)
        (windows_copy / "._Explicit.foo").write_bytes(ad)
        incoming.append(("Explicit.foo", "Explicit.foo", True, fixtures.DATA, fixtures.FINDER, resource))
        for outer, moving in (("Copy.foo", False), ("Move.foo", True), ("Keep.txt", False)):
            (windows_copy / outer).write_bytes(b"mapped Windows bytes")
            inner = Path(outer).stem if outer.endswith(".foo") else outer
            finder = (b"DATATEST" if outer.endswith(".foo") else b"TEXTttxt") + bytes(24)
            incoming.append((outer, inner, moving, b"mapped Windows bytes", finder, b""))
        (windows_copy / "SameVolume.bin").write_bytes(wrapped("SameVolume"))
        generic = fixtures.FINDER[:8] + b"\x01\0" + fixtures.FINDER[10:]
        (windows_copy / "Generic.bin").write_bytes(wrapped("Generic", finder=generic))

        def resource_fork(entries):
            payload, types, references = bytearray(), bytearray(struct.pack(">H", len(entries) - 1)), bytearray()
            for kind, identifier, body in entries:
                offset = len(payload)
                payload += struct.pack(">I", len(body)) + body
                types += kind + struct.pack(">HH", 0, 2 + len(entries) * 8 + len(references))
                references += struct.pack(">hHII", identifier, 65535, offset, 0)
            table = types + references
            header = struct.pack(">IIII", 256, 256 + len(payload), len(payload), 28 + len(table))
            return header + bytes(240) + payload + header + bytes(8) + struct.pack(">HH", 28, 28 + len(table)) + table

        bundle = (
            b"TEST"
            + struct.pack(">HH", 0, 1)
            + b"ICN#"
            + struct.pack(">Hhh", 0, 0, 128)
            + b"FREF"
            + struct.pack(">Hhh", 0, 0, 128)
        )
        app_info = b"APPLTEST\x01\0" + fixtures.FINDER[10:]
        app_fork = resource_fork(
            (
                (b"ICN#", 128, bytes([0xAA]) * 128 + bytes([0xFF]) * 128),
                (b"BNDL", 128, bundle),
                (b"FREF", 128, b"APPL" + bytes(3)),
            )
        )
        (windows_copy / "BundleApp.bin").write_bytes(wrapped("BundleApp", finder=app_info, fork=app_fork))
        (windows_copy / "GenericApp.bin").write_bytes(wrapped("GenericApp", finder=app_info, fork=b""))
        for name in ("FolderCopy", "FolderMove", "CollisionFolder", "VacatedFolder"):
            folder = windows_copy / name
            folder.mkdir()
            (folder / "Outer.bin").write_bytes(wrapped("Raw.foo" if name == "VacatedFolder" else "Nested"))
            (folder / "Raw.foo").write_bytes(b"nested mapped data")
            if name == "CollisionFolder":
                (folder / "Other.bin").write_bytes(wrapped("Nested"))
        (windows_copy / "Blocked.bin").write_bytes(wrapped("Blocked"))
        (windows / "Blocked").mkdir()
        corrupt = bytearray(wrapped("Corrupt"))
        corrupt[65] ^= 1
        (windows_copy / "Corrupt.bin").write_bytes(corrupt)
        if args.import_format == "OSX":
            for name in ("ImageCopy", "ImageMove"):
                (windows_copy / (name + ".bin")).write_bytes(wrapped(name))
            image_folder = windows_copy / "ImageFolderMove"
            image_folder.mkdir()
            (image_folder / "Outer.bin").write_bytes(wrapped("Nested"))
            (image_folder / "Raw.foo").write_bytes(b"nested mapped data")
            (windows_copy / "TooLarge.bin").write_bytes(wrapped("TooLarge", b"Z" * (4 * 1024 * 1024)))
            for name, prefix in (("ImageExplicit.foo", "._"), ("ImagePercent.foo", "%")):
                (windows_copy / name).write_bytes(fixtures.DATA)
                (windows_copy / (prefix + name)).write_bytes(ad)
    (windows / "Blocked.zip").write_bytes(zipped("ForbiddenPayload"))
    scrolled_folder, scrolled_archive = cases["Scrolled Zip"]
    distant_item = scrolled_folder / "Distant.txt"
    distant_item.touch()
    session = [folder for folder, _ in cases.values()] + [windows, verify, windows_copy]

    def path_key(path):
        value = 1469598103934665603
        for character in str(path):
            value = ((value ^ ord(character)) * 1099511628211) & ((1 << 64) - 1)
        return value

    (local / "Finder/settings.ini").write_text(
        "[Session]\nCount="
        + str(len(session))
        + "\n"
        + "".join(f"Window{i + 1}={path}\n" for i, path in enumerate(session))
        + f"[PositionsNative]\n{path_key(scrolled_archive)}=144,528\n{path_key(distant_item)}=144,840\n",
        encoding="utf-16",
    )
    image = base / "Archive Test.hfv"
    shutil.copyfile(ROOT / "build/image-desktop-fixture/DesktopMetadata.hfv", image)
    source = (ROOT / "finder/main.cpp").read_text(encoding="utf-8")
    enum = source.split("enum Command {", 1)[1].split("};", 1)[0]
    commands, number = {}, 0
    for entry in enum.split(","):
        entry = entry.strip()
        if not entry:
            continue
        if "=" in entry:
            entry, value = (part.strip() for part in entry.split("="))
            number = int(value)
        commands[entry] = number
        number += 1
    environment = {
        **os.environ,
        "USERPROFILE": str(profile),
        "LOCALAPPDATA": str(local),
        "APPDATA": str(profile / "AppData/Roaming"),
    }
    print(f"Archive UI profile: {base}; Windows destination: {windows}", flush=True)
    with private_desktop() as (station, launch):
        process = launch([str(runtime), "-mount-rw", str(image)], cwd=base, env=environment)
        user, kernel = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
        pointer, word = c.c_void_p, c.c_size_t
        callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        enum_roots = api(user, "EnumDesktopWindows", w.BOOL, w.HANDLE, callback, w.LPARAM)
        enum_children = api(user, "EnumChildWindows", w.BOOL, w.HWND, callback, w.LPARAM)
        pid_of = api(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
        get_class = api(user, "GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
        send_api = api(user, "SendMessageTimeoutW", word, w.HWND, w.UINT, word, word, w.UINT, w.UINT, c.POINTER(word))
        post = api(user, "PostMessageW", w.BOOL, w.HWND, w.UINT, word, word)
        get_item = api(user, "GetDlgItem", w.HWND, w.HWND, c.c_int)
        get_parent = api(user, "GetParent", w.HWND, w.HWND)
        get_client = api(user, "GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        get_bounds = api(user, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        get_style = api(user, "GetWindowLongW", w.LONG, w.HWND, c.c_int)
        enabled = api(user, "IsWindowEnabled", w.BOOL, w.HWND)
        is_window = api(user, "IsWindow", w.BOOL, w.HWND)
        allocate = api(kernel, "VirtualAllocEx", pointer, w.HANDLE, pointer, word, w.DWORD, w.DWORD)
        free = api(kernel, "VirtualFreeEx", w.BOOL, w.HANDLE, pointer, word, w.DWORD)
        write = api(kernel, "WriteProcessMemory", w.BOOL, w.HANDLE, pointer, pointer, word, pointer)
        read = api(kernel, "ReadProcessMemory", w.BOOL, w.HANDLE, pointer, pointer, word, pointer)
        handle = process._handle
        remote = allocate(handle, None, 4096, 0x3000, 4)
        assert remote

        def send(window, message, wp=0, lp=0):
            result = word()
            assert send_api(window, message, wp, lp, 2, 3000, c.byref(result)), f"Message timeout {message:x}"
            return result.value

        def caption(window):
            if not window:
                return ""
            text = c.create_unicode_buffer(1024)
            try:
                send(window, 0xD, len(text), c.addressof(text))
            except AssertionError:
                if is_window(window):
                    raise
            return text.value

        def windows_of(owner=None, kind=None):
            result = []

            @callback
            def visit(window, _):
                pid, name = w.DWORD(), c.create_unicode_buffer(128)
                pid_of(window, c.byref(pid))
                get_class(window, name, len(name))
                if (
                    pid.value == process.pid
                    and (not owner or get_parent(window) == owner)
                    and (kind is None or name.value == kind)
                ):
                    result.append(window)
                return True

            if owner:
                enum_children(owner, visit, 0)
            else:
                enum_roots(station, visit, 0)
            return result

        def named(owner, kind, text):
            return next((window for window in windows_of(owner, kind) if caption(window) == text), None)

        def wait(check, description):
            until = time.monotonic() + 30
            while time.monotonic() < until:
                value = check()
                if value:
                    return value
                if process.poll() is not None:
                    raise AssertionError(f"{description}: ROMAW exited {process.returncode}")
                time.sleep(0.06)
            raise AssertionError(
                f"Timeout: {description}; top windows: {[caption(x) for x in windows_of()]}; "
                f"notices: {[[caption(y) for y in windows_of(x)] for x in windows_of(None, 'FinderNotice')]}"
            )

        def rows(window):
            listing = windows_of(window, "SysListView32")[0]
            names = []
            for i in range(send(listing, 0x1004)):
                item = ListItem(text=remote + 512, capacity=512)
                assert write(handle, remote, c.byref(item), c.sizeof(item), None)
                send(listing, 0x1073, i, remote)
                text = c.create_unicode_buffer(512)
                assert read(handle, remote + 512, c.byref(text), c.sizeof(text), None)
                names.append(text.value)
            return listing, names

        def select(window, name):
            listing, _ = rows(window)
            # Exercise normal list activation so desktop menu dispatch uses
            # this folder rather than a previously active child window.
            assert post(listing, 0x201, 1, 0)
            assert post(listing, 0x202, 0, 0)
            time.sleep(0.25)
            # A completed move can refresh and reorder rows during activation.
            # Resolve the index afterwards, as a user would see the new listing.
            listing, names = rows(window)
            assert name in names, (name, names)
            item = ListItem(stateMask=3)
            assert write(handle, remote, c.byref(item), c.sizeof(item), None)
            send(listing, 0x102B, word(-1).value, remote)
            item.state = 3
            assert write(handle, remote, c.byref(item), c.sizeof(item), None)
            send(listing, 0x102B, names.index(name), remote)

        def command(window, name, action):
            select(window, name)
            assert post(window, 0x111, commands[action], 0)

        def point_message(listing, message, row=0):
            point = w.POINT()
            assert send(listing, message, row, remote)
            assert read(handle, remote, c.byref(point), c.sizeof(point), None)
            return point.x, point.y

        def check_nearby(window, archive, outputs):
            listing, names = rows(window)
            origin_x, origin_y = point_message(listing, 0x1029)  # LVM_GETORIGIN
            anchor_x, anchor_y = point_message(listing, 0x1010, names.index(archive))  # LVM_GETITEMPOSITION
            area = w.RECT()
            assert get_client(listing, c.byref(area))
            positions = set()
            for name in outputs:
                x, y = point_message(listing, 0x1010, names.index(name))
                assert (x, y) != (anchor_x, anchor_y)
                assert abs(x - anchor_x) + abs(y - anchor_y) <= 128, (name, (x, y), (anchor_x, anchor_y))
                assert 0 <= x - origin_x <= area.right - 48, (name, x, origin_x, area.right)
                assert 0 <= y - origin_y <= area.bottom - 52, (name, y, origin_y, area.bottom)
                positions.add((x, y))
            assert len(positions) == len(outputs), positions
            return origin_y

        expected_failures = set()

        def finished():
            operations = windows_of(desktop, "FinderOperation")
            for operation in operations:
                if operation in expected_failures:
                    continue
                status = caption(get_item(operation, 10))
                assert status not in ("Move failed", "Copy failed", "Extraction failed"), [
                    caption(x) for x in windows_of(operation)
                ]
                if status not in ("Complete", "Stopped"):
                    return False
            return True

        def verify_file(path, full=True):
            wait(path.is_file, f"extracted {path}")
            wait(finished, "completed archive operation")
            assert path.read_bytes() == fixtures.DATA
            metadata = path.with_name("._" + path.name).read_bytes()
            parts = {}
            for i in range(int.from_bytes(metadata[24:26], "big")):
                kind, offset, size = struct.unpack_from(">III", metadata, 26 + i * 12)
                parts[kind] = metadata[offset : offset + size]
            assert parts[2] == resource
            assert parts[9] == fixtures.FINDER if full else parts[9][:10] == fixtures.FINDER[:10]

        def image_readback(name):
            def unix(path):
                text = str(path.resolve()).replace("\\", "/")
                return "/mnt/" + text[0].lower() + text[2:]

            subprocess.run(
                ["wsl.exe", "--exec", args.image_reader, unix(image), name, unix(verify / name)], check=True, timeout=30
            )
            verify_file(verify / name / "Test")

        desktop = wait(lambda: named(None, "FinderFolder", "ROMAW"), "native Finder")
        folders = {name: wait(lambda name=name: named(desktop, "FinderFolder", name), name) for name in cases}
        win_folder = wait(lambda: named(desktop, "FinderFolder", windows.name), "Windows destination")
        check_folder = wait(lambda: named(desktop, "FinderFolder", "Verify"), "verification folder")
        win_copy_folder = wait(lambda: named(desktop, "FinderFolder", "Windows Copy"), "ordinary Windows destination")
        if args.import_format:

            def imported(path, data, finder, fork, storage=args.import_format):
                wait(path.is_file, f"published decoded/mapped file {path}")
                wait(finished, "completed Windows import")
                assert path.read_bytes() == data
                if storage == "BasiliskII":
                    actual_info = (path.parent / ".finf" / path.name).read_bytes()
                    resource_path = path.parent / ".rsrc" / path.name
                    actual_fork = resource_path.read_bytes() if resource_path.exists() else b""
                else:
                    metadata_path = (
                        path.parent / ".AppleDouble" / path.name
                        if storage == "Netatalk"
                        else path.with_name(("%" if storage == "Executor" else "._") + path.name)
                    )
                    metadata = metadata_path.read_bytes()
                    parts = {}
                    for i in range(int.from_bytes(metadata[24:26], "big")):
                        kind, offset, size = struct.unpack_from(">III", metadata, 26 + i * 12)
                        parts[kind] = metadata[offset : offset + size]
                    actual_info, actual_fork = parts[9], parts.get(2, b"")
                assert actual_info == finder and actual_fork == fork

            def paste(source, name, destination, moving=False):
                command(source, name, "cut_command" if moving else "copy_command")
                time.sleep(0.15)
                listing, _ = rows(destination)
                assert post(listing, 0x201, 1, 0)
                assert post(listing, 0x202, 0, 0)
                time.sleep(0.25)
                assert post(destination, 0x111, commands["paste_command"], 0)

            def expected_failure():
                notice = wait(lambda: next(iter(windows_of(None, "FinderNotice")), None), "expected import failure")
                for operation in windows_of(desktop, "FinderOperation"):
                    if caption(get_item(operation, 10)) in ("Move failed", "Copy failed"):
                        expected_failures.add(operation)
                        assert post(operation, 0x111, 3, 0)
                assert post(notice, 0x111, 1, 0)
                wait(lambda: not windows_of(None, "FinderNotice"), "dismiss expected failure")

            from get_info_ui import icon_clipboard_reader

            copied = icon_clipboard_reader(api, user, kernel)
            for name, pattern, type_code, creator in (
                ("Copy.bin", 0x55, "TEXT", "ttxt"),
                ("BundleApp.bin", 0xAA, "APPL", "TEST"),
            ):
                command(win_copy_folder, name, "info_command")
                info = wait(lambda: next(iter(windows_of(desktop, "FinderInfo")), None), "MacBinary Get Info")
                assert named(info, "Static", f"'{type_code}'"), (name, [caption(x) for x in windows_of(info)])
                assert named(info, "Static", f"'{creator}'"), (name, [caption(x) for x in windows_of(info)])
                artwork = named(info, "Static", "Get Info icon")
                assert artwork
                assert post(artwork, 0x201, 1, 8 | 8 << 16)
                assert post(artwork, 0x202, 0, 8 | 8 << 16)
                assert post(desktop, 0x111, commands["copy_command"], 0)
                family = wait(lambda: copied(info), "copy the displayed MacBinary icon")
                assert family[:4] == b"icns"
                at, chunks = 8, {}
                while at < len(family):
                    kind, size = struct.unpack_from(">4sI", family, at)
                    chunks[kind] = family[at + 8 : at + size]
                    at += size
                assert chunks[b"ICN#"][:128] == bytes([pattern]) * 128
                assert post(info, 0x10, 0, 0)
                wait(lambda: not is_window(info), "close MacBinary Get Info")
            print("PASS Windows MacBinary custom/BNDL icons and packaged Type/Creator in Get Info", flush=True)
            for outer, inner, moving, data, finder, fork in incoming:
                paste(win_copy_folder, outer, win_folder, moving)
                imported(windows / inner, data, finder, fork)
                assert (windows_copy / outer).exists() != moving
                if outer == "Explicit.foo":
                    assert not (windows_copy / "._Explicit.foo").exists()
                if outer != inner:
                    assert not (windows / outer).exists()
            for name in ("FolderCopy", "FolderMove"):
                paste(win_copy_folder, name, win_folder, name == "FolderMove")
                imported(windows / name / "Nested", fixtures.DATA, fixtures.FINDER, resource)
                imported(windows / name / "Raw", b"nested mapped data", b"DATATEST" + bytes(24), b"")
                assert not (windows / name / "Outer.bin").exists()
                assert not (windows / name / "Raw.foo").exists()
                assert (windows_copy / name).exists() == (name == "FolderCopy")
            paste(win_copy_folder, "VacatedFolder", win_folder)
            imported(windows / "VacatedFolder" / "Raw.foo", fixtures.DATA, fixtures.FINDER, resource)
            imported(windows / "VacatedFolder" / "Raw", b"nested mapped data", b"DATATEST" + bytes(24), b"")
            for name in ("CollisionFolder", "Blocked.bin", "Corrupt.bin"):
                source = windows_copy / name
                before = source.read_bytes() if source.is_file() else (source / "Outer.bin").read_bytes()
                paste(win_copy_folder, name, win_folder, True)
                expected_failure()
                assert source.exists()
                assert (source.read_bytes() if source.is_file() else (source / "Outer.bin").read_bytes()) == before
                if name == "CollisionFolder":
                    assert not (windows / name).exists() and (source / "Other.bin").is_file()
            paste(win_copy_folder, "SameVolume.bin", desktop, True)
            imported(desktop_path / "SameVolume", fixtures.DATA, fixtures.FINDER, resource, "OSX")
            assert not (windows_copy / "SameVolume.bin").exists()
            print(
                f"PASS Windows-to-{args.import_format} copies/moves, FTM names/codes, trees and retained failed sources",
                flush=True,
            )
            if args.import_format == "OSX":
                image_folder_name = "Desktop Metadata"
                command(desktop, image_folder_name, "open_command")
                mounted = wait(lambda: named(desktop, "FinderFolder", image_folder_name), "writable HFS image")
                for name, moving in (
                    ("ImageCopy", False),
                    ("ImageMove", True),
                    ("ImageFolderMove", True),
                    ("ImageExplicit.foo", True),
                    ("ImagePercent.foo", True),
                ):
                    outer = name if name == "ImageFolderMove" or name.endswith(".foo") else name + ".bin"
                    paste(win_copy_folder, outer, mounted, moving)
                    wait(lambda name=name: name in rows(mounted)[1], "published image import")
                    wait(finished, "completed Windows-to-image transfer")
                    assert (windows_copy / outer).exists() != moving
                    if name.endswith(".foo"):
                        assert not (windows_copy / (("%" if name == "ImagePercent.foo" else "._") + name)).exists()
                    unix = lambda p: "/mnt/" + str(p.resolve())[0].lower() + str(p.resolve())[2:].replace("\\", "/")
                    subprocess.run(
                        ["wsl.exe", "--exec", args.image_reader, unix(image), name, unix(verify / name)],
                        check=True,
                        timeout=30,
                    )
                    if name == "ImageFolderMove":
                        imported(verify / name / "Nested", fixtures.DATA, fixtures.FINDER, resource, "OSX")
                        imported(verify / name / "Raw", b"nested mapped data", b"DATATEST" + bytes(24), b"", "OSX")
                    else:
                        imported(verify / name, fixtures.DATA, fixtures.FINDER, resource, "OSX")
                large = windows_copy / "TooLarge.bin"
                before = hashlib.sha256(large.read_bytes()).digest()
                paste(win_copy_folder, large.name, mounted, True)
                expected_failure()
                assert hashlib.sha256(large.read_bytes()).digest() == before
                assert "TooLarge" not in rows(mounted)[1]
                print(
                    "PASS Windows-to-HFS copies/moves, independent fork/FinderInfo read-back and disk-full source retention",
                    flush=True,
                )
            assert free(handle, remote, 0, 0x8000)
            assert post(desktop, 0x10, 0, 0)
            assert process.wait(30) == 0
            assert hashlib.sha256(runtime.read_bytes()).hexdigest() == digest
            report = {
                "runtime": str(runtime),
                "sha256": digest,
                "profile": str(base),
                "backing": str(backing),
                "format": args.import_format,
                "passed": True,
            }
            (ROOT / f"build/windows-import-{args.import_format}.json").write_text(json.dumps(report, indent=2))
            return
        def check_extraction_progress(destination, archive_name, archive_format):
            title = f"Extracting to \u201c{destination}\u201d"
            window = wait(lambda: named(desktop, "FinderOperation", title), title)
            assert caption(get_item(window, 1)) == "Items remaining to be extracted:"
            assert caption(get_item(window, 21)) == "Extracting:"
            assert caption(get_item(window, 24)) == "Bytes Extracted:"
            assert caption(get_item(window, 25)) == f"Format: {archive_format}"
            assert caption(get_item(window, 12)) == archive_name

        for name, payload, archive_format in (
            ("Zip Here", "ZipPayload/Test", "ZIP"),
            ("By Type", "TypePayload/Test", "ZIP"),
            ("BinHex", "Test", "BinHex"),
            ("MacBinary", "Test", "MacBinary"),
            ("StuffIt", "Test", "StuffIt"),
        ):
            folder, archive = cases[name]
            command(
                folders[name],
                archive.name,
                "open_command" if name in ("Zip Here", "By Type") else "extract_here_command",
            )
            verify_file(folder / payload, name in ("Zip Here", "By Type"))
            check_extraction_progress(name, archive.name, archive_format)
            assert archive.exists()
        print("PASS native ZIP, StuffIt, BinHex and MacBinary Extract Here with exact forks", flush=True)
        check_nearby(folders["Zip Here"], cases["Zip Here"][1].name, ["ZipPayload"])
        scrolled = folders["Scrolled Zip"]
        listing, names = rows(scrolled)
        assert send(listing, 0x1013, names.index(scrolled_archive.name), 0)  # LVM_ENSUREVISIBLE
        before = point_message(listing, 0x1029)[1]
        assert before > 0, before
        outputs = ["Near One.txt", "Near Two.txt"]
        command(scrolled, scrolled_archive.name, "extract_here_command")
        for name in outputs:
            path = scrolled_folder / name
            wait(path.is_file, f"extracted {name}")
            assert path.read_bytes() == fixtures.DATA
        wait(lambda: set(outputs).issubset(rows(scrolled)[1]), "published both extracted icons")
        assert check_nearby(scrolled, scrolled_archive.name, outputs) == before
        print("PASS extracted icons near their archives, including multiple results in a scrolled folder", flush=True)
        folder, archive = cases["Zip Picker"]
        command(folders["Zip Picker"], archive.name, "extract_to_command")
        picker = wait(lambda: named(None, "#32770", "Extract To"), "classic folder picker")
        assert enabled(get_item(picker, 1))
        assert post(picker, 0x111, 2, 0)
        wait(lambda: not named(None, "#32770", "Extract To"), "cancel picker")
        assert not (folder / "PickerPayload").exists()
        command(folders["Zip Picker"], archive.name, "extract_to_command")
        picker = wait(lambda: named(None, "#32770", "Extract To"), "classic picker again")
        assert post(picker, 0x111, 1, 0)
        verify_file(folder / "PickerPayload/Test")
        command(win_folder, "Blocked.zip", "extract_here_command")
        notice = wait(lambda: next(iter(windows_of(None, "FinderNotice")), None), "Windows extraction refusal")
        wait(lambda: not enabled(desktop), "modal alert blocks Finder")
        assert not get_style(notice, -16) & (0x00C00000 | 0x00080000)  # WS_CAPTION | WS_SYSMENU
        outer, inner = w.RECT(), w.RECT()
        assert get_bounds(notice, c.byref(outer)) and get_client(notice, c.byref(inner))
        assert outer.right - outer.left - inner.right == outer.bottom - outer.top - inner.bottom == 16
        border_point = ((outer.top + 2) & 0xFFFF) << 16 | ((outer.left + 12) & 0xFFFF)
        assert send(notice, 0x84, 0, border_point) == 18  # WM_NCHITTEST, HTBORDER
        assert post(notice, 0x100, 13, 0)  # Return activates OK.
        wait(lambda: not is_window(notice) and enabled(desktop), "dismiss alert and re-enable Finder")
        assert not (windows / "ForbiddenPayload").exists()
        print("PASS classic picker and titleless modal alert frame, focus and Return dismissal", flush=True)
        for name, _, message in incompatible:
            command(check_folder, name, "open_command")
            notice = wait(lambda: next(iter(windows_of(None, "FinderNotice")), None), f"{name} alert")
            wait(lambda: not enabled(desktop), f"{name} alert blocks Finder")
            assert named(notice, "Static", message)
            assert [caption(button) for button in windows_of(notice, "Button")] == ["OK"]
            assert post(notice, 0x100, 13, 0)
            wait(lambda: not is_window(notice) and enabled(desktop), f"Return dismisses the {name} alert")
        assert post(desktop, 0x111, commands["register_images_command"], 0)
        notice = wait(lambda: next(iter(windows_of(None, "FinderNotice")), None), "disk image registration prompt")
        assert caption(get_item(notice, 6)) == "Register"  # IDYES
        assert caption(get_item(notice, 7)) == "Cancel"  # IDNO
        assert post(notice, 0x111, 7, 0)
        wait(lambda: not is_window(notice) and enabled(desktop), "cancel disk image registration")

        def configure(name):
            command(check_folder, name, "configure_app_command")
            dialog = wait(
                lambda: next(
                    (alert for alert in windows_of(None, "FinderNotice")
                     if named(alert, "Static", f"Configure App: {name}")),
                    None,
                ),
                f"configure {name}",
            )
            wait(lambda: not enabled(desktop), "Configure App blocks Finder")
            assert not get_style(dialog, -16) & (0x00C00000 | 0x00080000)
            return dialog

        def field(dialog, id_, value):
            text = c.create_unicode_buffer(value)
            assert send(get_item(dialog, id_), 0xC, 0, c.addressof(text))  # WM_SETTEXT

        def control_command(dialog, id_, notification=0):
            assert post(dialog, 0x111, id_ | notification << 16, 0)

        prefs = apps.parent / "ROMAW.prefs"
        dialog = configure("FAT Fixture")
        assert caption(get_item(dialog, 100)) == "Existing title"
        assert caption(get_item(dialog, 102)) == "640x480"
        assert send(get_item(dialog, 112), 0xF0) == 1  # BM_GETCHECK
        list_ = get_item(dialog, 113)
        assert send(list_, 0x18B) == 2  # LB_GETCOUNT
        assert send(list_, 0x186, 0) == 0  # LB_SETCURSEL
        control_command(dialog, 113, 1)  # LBN_SELCHANGE
        wait(lambda: caption(get_item(dialog, 118)) == "keep", "load the selected Gestalt type")
        assert caption(get_item(dialog, 114)) == "1"
        field(dialog, 114, "3")
        send(dialog, 0x111, 116)  # Commit Update before changing the selected row.
        assert send(list_, 0x186, 1) == 1
        control_command(dialog, 117)  # Delete
        wait(lambda: send(list_, 0x18B) == 1, "delete Gestalt")
        field(dialog, 118, "0x74657374")
        field(dialog, 114, "'TEXT'")
        control_command(dialog, 115)  # Add
        wait(lambda: send(list_, 0x18B) == 2, "add Gestalt")
        values = ("Configured Finder test", "7.6.1", "800x600", "16", "8m", "32m")
        for id_, value in enumerate(values, 100):
            field(dialog, id_, value)
        send(get_item(dialog, 111), 0xF5)  # BM_CLICK
        control_command(dialog, 1)
        wait(lambda: not is_window(dialog) and enabled(desktop), "save Configure App")
        saved = prefs.read_text()
        assert "DisplayScale=125" in saved and "Gestalt('gone')" not in saved
        assert "Gestalt('keep')={0,3};" in saved and "Gestalt(0x74657374)={0,0x54455854};" in saved
        assert "PreferM68K=1" in saved
        dialog = configure("FAT Fixture")
        assert [caption(get_item(dialog, id_)) for id_ in range(100, 106)] == list(values)
        assert send(get_item(dialog, 111), 0xF0) == 1
        field(dialog, 100, "discard")
        control_command(dialog, 2)
        wait(lambda: not is_window(dialog), "cancel Configure App")
        assert prefs.read_text() == saved
        dialog = configure("68k Fixture")
        assert not get_item(dialog, 110)
        assert not caption(get_item(dialog, 100))
        field(dialog, 100, "New application settings")
        control_command(dialog, 1)
        wait(lambda: not is_window(dialog), "create Configure App section")
        assert '[APPL.CFGM]\nWindowName="New application settings"' in prefs.read_text()
        print("PASS classic Configure App, FAT CPU selection, Gestalt editing, reload and cancellation", flush=True)

        command(win_folder, "OrdinaryWindows", "copy_command")
        time.sleep(0.15)
        assert post(win_copy_folder, 0x111, commands["paste_command"], 0)
        copied_windows = windows_copy / "OrdinaryWindows/Data"
        wait(copied_windows.is_file, "ordinary Windows folder copy")
        wait(finished, "ordinary Windows copy completion")
        assert copied_windows.read_bytes() == b"ordinary Windows folder"
        assert not named(None, "#32770", "Folder Archive Format")
        command(win_folder, "PlainWindows.txt", "copy_command")
        time.sleep(0.15)
        assert post(win_copy_folder, 0x111, commands["paste_command"], 0)
        wait(lambda: (windows_copy / "PlainWindows.txt").is_file(), "ordinary Windows file copy")
        wait(finished, "ordinary Windows file copy complete")
        assert (windows_copy / "PlainWindows.txt").read_bytes() == b"ordinary Windows file"
        assert not (windows_copy / "PlainWindows.txt.bin").exists()
        for name, moving, destination, companion in binary_cases:
            command(desktop, name, "cut_command" if moving else "copy_command")
            time.sleep(0.15)
            assert post(
                win_copy_folder if destination == windows_copy else win_folder, 0x111, commands["paste_command"], 0
            )
            output = destination / (name + ".bin")
            if name in ("MBFailedMove", "MBBlockedMove"):
                wait(
                    lambda: any(
                        op not in expected_failures and caption(get_item(op, 10)) == "Move failed"
                        for op in windows_of(desktop, "FinderOperation")
                    ),
                    "failed MacBinary move",
                )
                assert not output.is_file() and (desktop_path / name).read_bytes() == fixtures.DATA
                assert output.is_dir() if name == "MBBlockedMove" else not output.exists()
                assert companion.read_bytes() == (b"bad" if name == "MBFailedMove" else ad)
                for notice in windows_of(None, "FinderNotice"):
                    assert post(notice, 0x111, 1, 0)
                for op in windows_of(desktop, "FinderOperation"):
                    if caption(get_item(op, 10)) == "Move failed":
                        expected_failures.add(op)
                        assert post(op, 0x111, 3, 0)
                time.sleep(0.2)
                continue
            wait(output.is_file, f"published MacBinary file {name}")
            wait(finished, "MacBinary packaging complete")
            raw = output.read_bytes()
            assert raw[2 : 2 + raw[1]].decode("mac_roman") == name
            assert raw[65:73] == fixtures.FINDER[:8] and raw[73] == fixtures.FINDER[8]
            assert raw[101] == fixtures.FINDER[9]
            assert int.from_bytes(raw[124:126], "big") == binascii.crc_hqx(raw[:124], 0)
            secondary_size = int.from_bytes(raw[120:122], "big")
            secondary = raw[128 : 128 + secondary_size]
            parts = {}
            for i in range(int.from_bytes(secondary[24:26], "big")):
                kind, offset, size = struct.unpack_from(">III", secondary, 26 + i * 12)
                parts[kind] = secondary[offset : offset + size]
            assert parts[9] == fixtures.FINDER
            data_at = 128 + (secondary_size + 127) // 128 * 128
            resource_at = data_at + (len(fixtures.DATA) + 127) // 128 * 128
            assert struct.unpack_from(">II", raw, 83) == (len(fixtures.DATA), len(resource))
            assert raw[data_at : data_at + len(fixtures.DATA)] == fixtures.DATA
            assert raw[resource_at : resource_at + len(resource)] == resource
            if name == "MBOSXCopy":
                assert (destination / name).read_bytes() == b"unrelated unwrapped destination"
            else:
                assert not (destination / name).exists()
            assert not (destination / ("._" + name + ".bin")).exists()
            if moving:
                assert not (desktop_path / name).exists() and not companion.exists()
                assert not (desktop_path / ".rsrc" / name).exists()
            else:
                assert (desktop_path / name).read_bytes() == fixtures.DATA and companion.exists()
        print(
            "PASS automatic MacBinary copies/moves from all four host formats, same-volume conversion and safe failure",
            flush=True,
        )
        command(win_folder, "MBOSXMove.bin", "extract_to_command")
        picker = wait(lambda: named(None, "#32770", "Extract To"), "MacBinary round-trip picker")
        assert post(picker, 0x111, 4, 0)
        wait(lambda: str(desktop_path) in rows(picker)[1], "Mac-compatible desktop shortcut")
        select(picker, str(desktop_path))
        assert enabled(get_item(picker, 1))
        assert post(picker, 0x111, 1, 0)
        verify_file(desktop_path / "MBOSXMove")
        assert (windows / "MBOSXMove.bin").exists()
        print("PASS packaged MacBinary extraction restores both forks and all 32 FinderInfo bytes", flush=True)
        for name, index, extension, moving in (
            ("PackageZip", 0, "zip", False),
            ("PackageSit", 1, "sit", False),
            ("PackageMoveZip", 0, "zip", True),
            ("PackageFailedMove", 0, "zip", True),
        ):
            command(desktop, name, "cut_command" if moving else "copy_command")
            time.sleep(0.15)
            assert post(win_folder, 0x111, commands["paste_command"], 0)
            choice = wait(lambda: named(None, "#32770", "Folder Archive Format"), "ZIP/StuffIt choice")
            combo = get_item(choice, 15)
            assert send(combo, 0x146) == 2
            send(combo, 0x14E, index, 0)
            assert post(choice, 0x111, 1, 0)
            output = windows / (name + "." + extension)
            if name == "PackageFailedMove":
                wait(
                    lambda: any(
                        op not in expected_failures and caption(get_item(op, 10)) == "Move failed"
                        for op in windows_of(desktop, "FinderOperation")
                    ),
                    "failed move status",
                )
                assert not output.exists()
                assert (desktop_path / name / "Test").read_bytes() == fixtures.DATA
                assert (desktop_path / name / "._Test").read_bytes() == b"bad"
                for notice in windows_of(None, "FinderNotice"):
                    assert post(notice, 0x111, 1, 0)
                for op in windows_of(desktop, "FinderOperation"):
                    if caption(get_item(op, 10)) == "Move failed":
                        expected_failures.add(op)
                        assert post(op, 0x111, 3, 0)
                time.sleep(0.2)
                continue
            wait(output.is_file, "published folder archive")
            wait(finished, "folder packaging complete")
            if moving:
                assert not (desktop_path / name).exists()
            else:
                assert (desktop_path / name / "Test").read_bytes() == fixtures.DATA
            if extension == "zip":
                with zipfile.ZipFile(output) as archive:
                    assert archive.read(name + "/Test") == fixtures.DATA
                    assert archive.read("__MACOSX/" + name + "/._Test") == ad
        print(
            "PASS cross-drive ZIP/StuffIt packaging, successful move removal and failed move source preservation",
            flush=True,
        )
        command(win_folder, "PackageSit.sit", "copy_command")
        time.sleep(0.15)
        assert post(check_folder, 0x111, commands["paste_command"], 0)
        wait(lambda: "PackageSit.sit" in rows(check_folder)[1], "copy StuffIt 5 archive to the verification folder")
        command(check_folder, "PackageSit.sit", "extract_here_command")
        verify_file(verify / "PackageSit/Test", full=False)
        check_extraction_progress("Verify", "PackageSit.sit", "StuffIt 5")
        print("PASS extraction progress labels and content-detected formats, including StuffIt 5", flush=True)
        image_folder_name = "Desktop Metadata"
        command(desktop, image_folder_name, "open_command")
        mounted = wait(lambda: named(desktop, "FinderFolder", image_folder_name), "writable mounted image")
        folder, archive = cases["Image Archive"]
        command(folders["Image Archive"], archive.name, "extract_to_command")
        picker = wait(lambda: named(None, "#32770", "Extract To"), "image destination picker")
        assert post(picker, 0x111, 4, 0)
        wait(lambda: image_folder_name in rows(picker)[1], "image in picker")
        select(picker, image_folder_name)
        assert post(picker, 0x111, 1, 0)
        wait(lambda: "ImagePayload" in rows(mounted)[1], "extracted live image folder")
        wait(finished, "image import completion")
        image_readback("ImagePayload")
        command(folders["Image Source"], cases["Image Source"][1].name, "copy_command")
        time.sleep(0.15)
        assert post(mounted, 0x111, commands["paste_command"], 0)
        wait(lambda: "Archive.zip" in rows(mounted)[1], "archive copied into image")
        wait(finished, "image archive copy completed")
        command(mounted, "Archive.zip", "extract_here_command")
        wait(lambda: "ImageSourcePayload" in rows(mounted)[1], "native extraction from an image archive")
        wait(finished, "image archive extraction complete")
        check_nearby(mounted, "Archive.zip", ["ImageSourcePayload"])
        image_readback("ImageSourcePayload")
        print("PASS extraction into live HFS image and independent host fork/FinderInfo read-back", flush=True)
        assert free(handle, remote, 0, 0x8000)
        assert post(desktop, 0x10, 0, 0)
        assert process.wait(30) == 0
    assert hashlib.sha256(runtime.read_bytes()).hexdigest() == digest, "Staged executable changed during UI test"
    report = {"runtime": str(runtime), "sha256": digest, "profile": str(base), "windows": str(windows), "passed": True}
    (ROOT / "build/archive-ui-report.json").write_text(json.dumps(report, indent=2))
    print("PASS staged normal ROMAW.exe native archive UI regression", flush=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup_drives()
        DRIVE_MAPPINGS.clear()
