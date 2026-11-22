# +===================================================================
#   File:      drive_formats_ui.py
#
#   Summary:   Check first-start drive-format dropdowns without saving changes to real drive policy.
#
#   Classes:   Uses the private desktop process harness.
#
#   Functions: main.
#
#   Origin:    ROMAW native preferences regression fixture.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Validate first-use detection and dropdown order in the normal Windows executable."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import tempfile
import time

from private_desktop import api, private_desktop


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=root / "windows-binaries/bin/ROMAW.exe")
    args = parser.parse_args()
    base = Path(tempfile.mkdtemp(prefix="romaw-drive-formats-ui-"))
    apps = base / ".romaw/Apps"
    (apps / "Desktop Folder").mkdir(parents=True)
    (apps / "System Folder").mkdir()
    local = base / "AppData/Local"
    local.mkdir(parents=True)
    (base / "AppData/Roaming").mkdir()
    (apps.parent / "ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=\n")
    environment = {
        **os.environ,
        "USERPROFILE": str(base),
        "LOCALAPPDATA": str(local),
        "APPDATA": str(base / "AppData/Roaming"),
    }
    with private_desktop() as (desktop, launch):
        process = launch([str(args.runtime.resolve())], cwd=base, env=environment)
        user = c.WinDLL("user32", use_last_error=True)
        callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        enumerate_windows = api(user, "EnumDesktopWindows", w.BOOL, w.HANDLE, callback, w.LPARAM)
        pid_of = api(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
        text_of = api(user, "GetWindowTextW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
        class_of = api(user, "GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
        get_item = api(user, "GetDlgItem", w.HWND, w.HWND, c.c_int)
        send = api(
            user,
            "SendMessageTimeoutA",
            c.c_size_t,
            w.HWND,
            w.UINT,
            c.c_size_t,
            c.c_size_t,
            w.UINT,
            w.UINT,
            c.POINTER(c.c_size_t),
        )
        post = api(user, "PostMessageW", w.BOOL, w.HWND, w.UINT, c.c_size_t, c.c_size_t)

        def window(title):
            result = []

            @callback
            def visit(handle, _):
                pid, text, kind = w.DWORD(), c.create_unicode_buffer(256), c.create_unicode_buffer(64)
                pid_of(handle, c.byref(pid))
                text_of(handle, text, len(text))
                class_of(handle, kind, len(kind))
                expected = "FinderFolder" if title == "ROMAW" else "#32770"
                if pid.value == process.pid and text.value == title and kind.value == expected:
                    result.append(handle)
                return True

            enumerate_windows(desktop, visit, 0)
            return result[0] if result else None

        def wait(title):
            until = time.monotonic() + 20
            while time.monotonic() < until:
                found = window(title)
                if found:
                    return found
                assert process.poll() is None, process.returncode
                time.sleep(0.05)
            raise AssertionError("Missing window: " + title)

        def message(handle, code, wp=0, lp=0):
            result = c.c_size_t()
            assert send(handle, code, wp, lp, 2, 3000, c.byref(result))
            return result.value

        preferences = wait("Drive Formats")
        expected = ["Windows", "OSX", "Netatalk", "BasiliskII", "Executor"]
        for letter in range(26):
            control = get_item(preferences, 100 + letter)
            assert control
            name = c.create_unicode_buffer(64)
            class_of(control, name, len(name))
            assert name.value.casefold() == "combobox", name.value
            assert message(control, 0x146) == 5
            values = []
            for index in range(5):
                text = c.create_string_buffer(64)
                message(control, 0x148, index, c.addressof(text))
                values.append(text.value.decode("ascii"))
            assert values == expected, values
            assert message(control, 0x147) in range(5)
        # Cancel the first-start dialog: never mark the machine's real C:/D:
        # drives as Mac volumes merely to exercise the preference controls.
        assert post(preferences, 0x111, 2, 0)
        finder = wait("ROMAW")
        assert post(finder, 0x10, 0, 0)
        assert process.wait(30) == 0
    print(
        "PASS first-start preferences: 26 format dropdowns, detected selections, Windows/OSX/Netatalk/BasiliskII/Executor order"
    )


if __name__ == "__main__":
    main()
