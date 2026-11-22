# +===================================================================
#   File:      archive_ui.py
#
#   Summary:   Exercise native archive commands and classic pickers in an isolated Windows Finder.
#
#   Classes:   ListItem; uses the existing private desktop process harness.
#
#   Functions: main.
#
#   Origin:    ROMAW native Finder regression fixtures; no production test hooks.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Test the staged executable with private files, profile, desktop and clipboard."""
import argparse
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
    parser.add_argument(
        "--image-reader", default="/mnt/d/Source/ROMAW/build/archive-image-reader/archive-image-readback"
    )
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    digest = hashlib.sha256(runtime.read_bytes()).hexdigest()
    base = Path(tempfile.mkdtemp(prefix="romaw-archive-ui-"))
    windows = Path(tempfile.mkdtemp(prefix="archive-ui-windows-", dir=ROOT / "build"))
    profile = base / "profile"
    apps = profile / ".romaw/Apps"
    desktop_path = apps / "Desktop Folder"
    desktop_path.mkdir(parents=True)
    (apps / "System Folder").mkdir()
    local = profile / "AppData/Local"
    (local / "Finder").mkdir(parents=True)
    (profile / "AppData/Roaming").mkdir()
    (apps.parent / "ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=none\nFormatC=Windows\nFormatD=Windows\n")
    (apps.parent / "ROMAW.ftm").write_text(
        "ZIP,,APPL,BAD!,x,x,Attempted override\nSIT,,APPL,BAD!,x,x,Attempted override\n"
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
    ad = fixtures.appledouble()
    verify = desktop_path / "Verify"
    verify.mkdir()
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

    cases = {}
    for name, extension, raw in (
        ("Zip Here", "zip", zipped("ZipPayload")),
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
    for storage in ("OSX", "Netatalk", "BasiliskII", "Executor"):
        for moving in (False, True):
            name = "MB" + storage + ("Move" if moving else "Copy")
            path = desktop_path / name
            path.write_bytes(fixtures.DATA)
            if storage == "OSX":
                companion = path.with_name("._" + name)
            elif storage == "Netatalk":
                companion = desktop_path / ".AppleDouble" / name
            elif storage == "Executor":
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
    (windows / "Blocked.zip").write_bytes(zipped("ForbiddenPayload"))
    session = [folder for folder, _ in cases.values()] + [windows, verify, windows_copy]
    (local / "Finder/settings.ini").write_text(
        "[Session]\nCount="
        + str(len(session))
        + "\n"
        + "".join(f"Window{i + 1}={path}\n" for i, path in enumerate(session)),
        encoding="utf-16",
    )
    image = base / "Archive Test.hfv"
    shutil.copyfile(ROOT / "build/image-desktop-fixture/DesktopMetadata.hfv", image)
    source = (ROOT / "finder/main.cpp").read_text()
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

        expected_failures = set()

        def finished():
            operations = windows_of(desktop, "FinderOperation")
            for operation in operations:
                if operation in expected_failures:
                    continue
                status = caption(get_item(operation, 10))
                assert status not in ("Move failed", "Copy failed"), [caption(x) for x in windows_of(operation)]
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
        for name, payload in (
            ("Zip Here", "ZipPayload/Test"),
            ("By Type", "TypePayload/Test"),
            ("BinHex", "Test"),
            ("MacBinary", "Test"),
            ("StuffIt", "Test"),
        ):
            folder, archive = cases[name]
            command(
                folders[name],
                archive.name,
                "open_command" if name in ("Zip Here", "By Type") else "extract_here_command",
            )
            verify_file(folder / payload, name in ("Zip Here", "By Type"))
            assert archive.exists()
        print("PASS native ZIP, StuffIt, BinHex and MacBinary Extract Here with exact forks", flush=True)
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
        assert post(notice, 0x111, 1, 0)
        assert not (windows / "ForbiddenPayload").exists()
        print("PASS classic picker acceptance/cancellation and Windows destination refusal", flush=True)
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
    main()
