# +===================================================================
#   File:      get_info_ui.py
#
#   Summary:   Exercise Get Info renames, icon editing and child redraws through live Finder controls.
#
#   Classes:   Uses Win32 ListView item records.
#
#   Functions: check_get_info, called by integration_test.py.
#
#   Origin:    Executor native Finder regression fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

from private_desktop import private_desktop


WM_GETTEXT = 0xD
WM_SETTEXT = 0xC
WM_CLOSE = 0x10
WM_COMMAND = 0x111
WM_KEYDOWN = 0x100
WM_KEYUP = 0x101
WM_LBUTTONDOWN = 0x201
WM_LBUTTONUP = 0x202
VK_ESCAPE = 27
VK_RETURN = 13
LVM_GETITEMCOUNT = 0x1004
LVM_ENSUREVISIBLE = 0x1013
LVM_GETITEMRECT = 0x100E
LVM_GETITEMTEXTW = 0x1073
LVM_SETITEMSTATE = 0x102B
FINDER_OPEN = 100
FINDER_GET_INFO = 103
FINDER_COPY = 107
FINDER_PASTE = 108
FINDER_LIST = 111
FINDER_CLEAR = 124


def icon_clipboard_reader(api, user, kernel):
    """Read the classic family published by Copy on the private station."""
    open_clipboard = api(user, "OpenClipboard", w.BOOL, w.HWND)
    close_clipboard = api(user, "CloseClipboard", w.BOOL)
    get = api(user, "GetClipboardData", w.HANDLE, w.UINT)
    register = api(user, "RegisterClipboardFormatW", w.UINT, w.LPCWSTR)
    sequence = api(user, "GetClipboardSequenceNumber", w.DWORD)
    owner = api(user, "GetClipboardOwner", w.HWND)
    size = api(kernel, "GlobalSize", c.c_size_t, w.HANDLE)
    lock = api(kernel, "GlobalLock", c.c_void_p, w.HANDLE)
    unlock = api(kernel, "GlobalUnlock", w.BOOL, w.HANDLE)
    icon_format = register("ROMAW Macintosh icon family")
    assert icon_format
    previous = sequence()

    def copied(expected_owner):
        nonlocal previous
        if not open_clipboard(None):
            return None
        try:
            current = sequence()
            if current == previous or owner() != expected_owner:
                return None
            handle = get(icon_format)
            assert handle
            length = size(handle)
            pointer = lock(handle)
            assert pointer and 0 < length <= 4096
            try:
                previous = current
                return c.string_at(pointer, length)
            finally:
                unlock(handle)
        finally:
            close_clipboard()

    return copied


def check_get_info(root, runtime, icons=False, redraw=False, resize_scroll=False, zoom=False):
    if icons:
        with private_desktop() as (desktop_handle, process):
            _check_get_info(root, runtime, icons, redraw, desktop_handle, process)
    else:
        _check_get_info(root, runtime, icons, redraw, resize_scroll=resize_scroll, zoom=zoom)


def _check_get_info(
    root, runtime, icons, redraw, desktop_handle=None, process=subprocess.Popen, resize_scroll=False, zoom=False
):
    """Exercise Finder with a private profile and a writable copy of the HFS fixture."""
    base = Path(tempfile.mkdtemp(prefix="get-info-ui-", dir=root / "build"))
    print(f"Get Info fixtures and logs: {base}", flush=True)
    profile = base / "profile"
    apps = profile / ".romaw" / "Apps"
    fixture = apps / "Desktop Folder"
    fixture.mkdir(parents=True)
    (apps / "System Folder").mkdir()
    (apps.parent / "ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=none\n")
    (apps.parent / "ROMAW.ftm").write_text("TXT,,TEXT,ttxt,x,x,Plain text fixture\nMTXT,,TEXT,Mtst,,,FTM fallback\n")
    (fixture / "Host Text.txt").write_bytes(b"plain host text")
    (fixture / "Host File").write_bytes(b"source")
    # Give the host file an independent custom family. Clearing it from
    # Get Info must leave the file, its directory entry, and its data intact.
    data = struct.pack(">I", 256) + bytes([0x55]) * 128 + bytes([0xFF]) * 128
    header = struct.pack(">IIII", 256, 256 + len(data), len(data), 50)
    mapping = header + bytes(8) + struct.pack(">HHH4sHHhHI4x", 28, 50, 0, b"ICN#", 0, 10, -16455, 65535, 0)
    fork = header + bytes(240) + data + mapping
    sidecar = fixture / "._Host File"
    sidecar.write_bytes(
        struct.pack(">II16xHIIIIII", 0x00051607, 0x00020000, 2, 9, 50, 32, 2, 82, len(fork))
        + bytes(8)
        + bytes([4])
        + bytes(23)
        + fork
    )
    (fixture / "Host Conflict").write_bytes(b"destination")
    (fixture / "Host Folder").mkdir()
    contents = "W" * 84 + " document" if zoom else "Contents"
    (fixture / "Host Folder" / contents).write_bytes(b"nested")
    if zoom:
        for folder, count in (("Empty List", 0), ("Compact List", 12), ("Oversized List", 96), ("Oversized Icons", 2)):
            directory = fixture / folder
            directory.mkdir()
            for row in range(count):
                (directory / f"Row {row:02}").write_bytes(b"contents")
    if resize_scroll:
        for row in range(48):
            (fixture / "Host Folder" / f"Scroll row {row:02}").write_bytes(b"scroll")
    memory_app = None
    if not (icons or redraw or resize_scroll or zoom):
        from runtime_fixtures import make_68k, module

        memory_app = make_68k(fixture)
        resources = module("suitcase_resources", root / "tools/romaw/system_resources.py")
        fork = resources.build_fork(
            [
                (b"FOND", 129, 0, b"Caf\x8e", b"font family"),
                (b"NFNT", 130, 0, None, b"bitmap font"),
                (b"snd ", 128, 0, b"Chime", b"sound"),
                (b"DRVR", 131, 0, b"Calculator", b"accessory"),
                (b"KCHR", 132, 0, b"Test Layout", b"keyboard layout"),
                (b"STR#", 128, 0, b"Internal", b"\0\0"),
            ]
        )
        for name, file_type, data_fork in (
            ("Test Resource Suitcase", b"zsys", False),
            ("Test Data Suitcase", b"ZSYS", True),
        ):
            (fixture / name).write_bytes(fork if data_fork else b"")
            table = struct.pack(">II16xHIII", 0x51607, 0x20000, 1 if data_fork else 2, 9, 38 if data_fork else 50, 32)
            if not data_fork:
                table += struct.pack(">III", 2, 82, len(fork))
            (fixture / ("._" + name)).write_bytes(
                table + file_type + b"MACS" + bytes(24) + (b"" if data_fork else fork)
            )
    image = base / "Get Info Test.hfv"
    shutil.copyfile(root / "build/image-desktop-fixture/DesktopMetadata.hfv", image)
    local = profile / "AppData" / "Local"
    local.mkdir(parents=True)
    (local.parent / "Roaming").mkdir()

    def launch():
        return process(
            [str(runtime), "-mount-rw", str(image)],
            cwd=base,
            env={
                **os.environ,
                "USERPROFILE": str(profile),
                "LOCALAPPDATA": str(local),
                "APPDATA": str(local.parent / "Roaming"),
            },
        )

    child = launch()

    def api(lib, name, result, *arguments):
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = result, arguments
        return fn

    u, k = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
    p = c.c_void_p
    word = c.c_size_t
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    enum_windows = api(u, "EnumWindows", w.BOOL, callback, w.LPARAM)
    enum_private = api(u, "EnumDesktopWindows", w.BOOL, w.HANDLE, callback, w.LPARAM)
    enum_children = api(u, "EnumChildWindows", w.BOOL, w.HWND, callback, w.LPARAM)
    window_pid = api(u, "GetWindowThreadProcessId", w.DWORD, w.HWND, c.POINTER(w.DWORD))
    class_name = api(u, "GetClassNameW", c.c_int, w.HWND, w.LPWSTR, c.c_int)
    send_timeout = api(u, "SendMessageTimeoutW", word, w.HWND, w.UINT, word, word, w.UINT, w.UINT, c.POINTER(word))
    post = api(u, "PostMessageW", w.BOOL, w.HWND, w.UINT, word, word)
    enabled = api(u, "IsWindowEnabled", w.BOOL, w.HWND)
    get_parent = api(u, "GetParent", w.HWND, w.HWND)
    style = api(u, "GetWindowLongPtrW", word, w.HWND, c.c_int)
    alloc = api(k, "VirtualAllocEx", p, w.HANDLE, p, word, w.DWORD, w.DWORD)
    free = api(k, "VirtualFreeEx", w.BOOL, w.HANDLE, p, word, w.DWORD)
    write = api(k, "WriteProcessMemory", w.BOOL, w.HANDLE, p, p, word, p)
    read = api(k, "ReadProcessMemory", w.BOOL, w.HANDLE, p, p, word, p)
    handle = int(child._handle)
    # List-view messages above WM_USER expect buffers in the window's process.
    # Allocate the buffer in the disposable ROMAW child we just launched.
    remote = alloc(handle, None, 4096, 0x3000, 4)

    class ListItem(c.Structure):
        _fields_ = [
            ("mask", w.UINT),
            ("item", c.c_int),
            ("subitem", c.c_int),
            ("state", w.UINT),
            ("stateMask", w.UINT),
            ("text", p),
            ("capacity", c.c_int),
            ("image", c.c_int),
            ("param", w.LPARAM),
            ("group", c.c_int),
            ("columns", w.UINT),
            ("indexes", p),
            ("formats", p),
            ("groupIndex", c.c_int),
        ]

    def send(hwnd, msg, wp=0, lp=0):
        value = word()
        if not send_timeout(hwnd, msg, wp, lp, 2, 2400, c.byref(value)):
            raise OSError(c.get_last_error(), f"message {msg} to {hwnd}")
        return value.value

    def caption(hwnd):
        text = c.create_unicode_buffer(512)
        send(hwnd, WM_GETTEXT, len(text), c.addressof(text))
        return text.value

    def windows(owner=None, kind=None):
        result = []

        @callback
        def collect(hwnd, _):
            pid, name = w.DWORD(), c.create_unicode_buffer(128)
            window_pid(hwnd, c.byref(pid))
            class_name(hwnd, name, len(name))
            if (
                pid.value == child.pid
                and (not owner or get_parent(hwnd) == owner)
                and (kind is None or name.value == kind)
            ):
                result.append(hwnd)
            return True

        if owner:
            enum_children(owner, collect, 0)
        elif desktop_handle:
            enum_private(desktop_handle, collect, 0)
        else:
            enum_windows(collect, 0)
        return result

    def wait(test, description, pause=0.06):
        until = time.monotonic() + 12
        while time.monotonic() < until:
            result = test()
            if result:
                return result
            if child.poll() is not None:
                raise RuntimeError(f"{description}: ROMAW exited {child.returncode}")
            time.sleep(pause)
        raise RuntimeError(
            f"{description}: top-level {[(caption(x), [caption(y) for y in windows(x)]) for x in windows()]}"
        )

    def named(owner, kind, name):
        return next((h for h in windows(owner, kind) if caption(h) == name), None)

    def list_rows(hwnd, column=0):
        field = windows(hwnd, "SysListView32")[0]
        rows = []
        for i in range(send(field, LVM_GETITEMCOUNT)):
            lv = ListItem(subitem=column, text=remote + 512, capacity=512)
            assert write(handle, remote, c.byref(lv), c.sizeof(lv), None)
            send(field, LVM_GETITEMTEXTW, i, remote)
            text = c.create_unicode_buffer(512)
            assert read(handle, remote + 512, c.byref(text), c.sizeof(text), None)
            rows.append(text.value)
        return field, rows

    def command(folder, name, cmd):
        field, rows = list_rows(folder)
        if name not in rows:
            raise RuntimeError(f"{name!r} missing from {caption(folder)!r}: {rows}")
        index = rows.index(name)
        send(field, LVM_ENSUREVISIBLE, index, 0)
        rect = w.RECT(1, 0, 0, 0)
        assert write(handle, remote, c.byref(rect), c.sizeof(rect), None)
        assert send(field, LVM_GETITEMRECT, index, remote)
        assert read(handle, remote, c.byref(rect), c.sizeof(rect), None)
        point = (rect.left + rect.right) // 2 | ((rect.top + rect.bottom) // 2 << 16)
        assert post(field, WM_LBUTTONDOWN, 1, point)
        assert post(field, WM_LBUTTONUP, 0, point)
        time.sleep(0.25)
        lv = ListItem(stateMask=3)
        assert write(handle, remote, c.byref(lv), c.sizeof(lv), None)
        send(field, LVM_SETITEMSTATE, word(-1).value, remote)
        lv.state = 3
        assert write(handle, remote, c.byref(lv), c.sizeof(lv), None)
        send(field, LVM_SETITEMSTATE, index, remote)
        assert post(folder, WM_COMMAND, cmd, 0)

    def info(folder, name):
        command(folder, name, FINDER_GET_INFO)
        view = wait(lambda: named(desktop, "FinderInfo", name + " Info"), f"Get Info for {name}")
        edit = named(view, "Edit", name)
        assert edit and not style(edit, -16) & 0x800, (name, [caption(x) for x in windows(view)])
        return view, edit

    def change(edit, name):
        text = c.create_unicode_buffer(name)
        assert send(edit, WM_SETTEXT, 0, c.addressof(text))

    def key(edit, value):
        assert post(edit, WM_KEYDOWN, value, 1)
        assert post(edit, WM_KEYUP, value, 1)

    def conflict(view, edit, name, close_window=False):
        change(edit, name)
        if close_window:
            assert post(view, WM_CLOSE, 0, 0)
        else:
            key(edit, VK_RETURN)
        notice = wait(lambda: next(iter(windows(None, "FinderNotice")), None), f"conflict for {name}")
        print(f"Conflict {name}: {[caption(x) for x in windows(notice)]}", flush=True)
        assert post(notice, WM_COMMAND, 1, 0)
        wait(lambda: not windows(None, "FinderNotice") and enabled(view), "dismiss conflict")
        assert caption(edit) == name

    def close(view):
        assert post(view, WM_CLOSE, 0, 0)
        wait(lambda: view not in windows(desktop, "FinderInfo"), "close Get Info")

    def check_memory_panel():
        control = api(u, "GetDlgItem", w.HWND, w.HWND, c.c_int)
        sibling = api(u, "GetWindow", w.HWND, w.HWND, w.UINT)
        get_rect = api(u, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        for values in (("1024", "1024"), ("1152", "1536")):
            view, _ = info(desktop, memory_app.name)
            panel = named(view, "Button", "Memory Requirements")
            assert panel and style(panel, -16) & 0xF == 7, "Memory Requirements should be a group box"
            assert not sibling(panel, 2), "The panel must sit behind the fields it frames"
            labels = [caption(label) for label in windows(view, "Static")]
            assert {"Suggested size:", "Minimum size:", "Preferred size:", "1024"}.issubset(labels)
            assert labels.count("K") == 3
            bounds = w.RECT()
            assert get_rect(panel, c.byref(bounds))
            for identifier, value, replacement in zip((17301, 17302), values, ("1152", "1536")):
                edit = control(view, identifier)
                assert edit and caption(edit) == value and not style(edit, -16) & 0x800
                inside = w.RECT()
                assert get_rect(edit, c.byref(inside))
                assert bounds.left < inside.left < inside.right < bounds.right
                assert bounds.top < inside.top < inside.bottom < bounds.bottom
                if value != replacement:
                    change(edit, replacement)
            close(view)
        print("PASS inline application memory panel and saved Minimum and Preferred sizes", flush=True)

    def check_suitcases():
        expected = ["Bitmap font #130", "Caf\u00e9", "Calculator", "Chime", "Test Layout"]
        for name in ("Test Resource Suitcase", "Test Data Suitcase"):
            command(desktop, name, FINDER_OPEN)
            window = wait(lambda: named(desktop, "FinderSuitcase", name), f"open {name}")
            listing, names = list_rows(window)
            assert names == expected, names
            for index, kind in enumerate(("Bitmap font", "Font family", "Desk accessory", "Sound", "Keyboard layout")):
                lv = ListItem(subitem=1, text=remote + 512, capacity=512)
                assert write(handle, remote, c.byref(lv), c.sizeof(lv), None)
                send(listing, LVM_GETITEMTEXTW, index, remote)
                label = c.create_unicode_buffer(512)
                assert read(handle, remote + 512, c.byref(label), c.sizeof(label), None)
                assert label.value == kind
            assert post(window, WM_CLOSE, 0, 0)
            wait(lambda: not named(desktop, "FinderSuitcase", name), f"close {name}")
        print("PASS classic members in resource-fork and data-fork suitcases", flush=True)

    def icon_command(view, command_):
        icon = named(view, "Static", "Get Info icon")
        assert icon
        assert post(icon, WM_LBUTTONDOWN, 1, 8 | 8 << 16)
        assert post(icon, WM_LBUTTONUP, 0, 8 | 8 << 16)
        assert post(desktop, WM_COMMAND, command_, 0)

    def copy_family(view, copied):
        icon_command(view, FINDER_COPY)
        return wait(lambda: copied(view), "copy selected Get Info icon")

    def check_exposed_pixels(
        view, background, center, description, resize=False, scroll_list=None, zoom_list=None, zoom_case=None
    ):
        """Compare visible pixels during native mouse gestures and after release."""
        get_rect = api(u, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        window_at = api(u, "WindowFromPoint", w.HWND, w.POINT)
        set_cursor = api(u, "SetCursorPos", w.BOOL, c.c_int, c.c_int)
        get_cursor = api(u, "GetCursorPos", w.BOOL, c.POINTER(w.POINT))
        button_state = api(u, "GetAsyncKeyState", w.SHORT, c.c_int)
        metric = api(u, "GetSystemMetrics", c.c_int, c.c_int)
        left, top, width, height = (metric(index) for index in (76, 77, 78, 79))

        class Mouse(c.Structure):
            _fields_ = [
                ("x", w.LONG),
                ("y", w.LONG),
                ("data", w.DWORD),
                ("flags", w.DWORD),
                ("time", w.DWORD),
                ("extra", c.c_size_t),
            ]

        class Input(c.Structure):
            _fields_ = [("kind", w.DWORD), ("mouse", Mouse)]

        send_input = api(u, "SendInput", w.UINT, w.UINT, c.POINTER(Input), c.c_int)
        get_dc = api(u, "GetDC", w.HDC, w.HWND)
        release_dc = api(u, "ReleaseDC", c.c_int, w.HWND, w.HDC)
        gdi = c.WinDLL("gdi32", use_last_error=True)
        compatible = api(gdi, "CreateCompatibleDC", w.HDC, w.HDC)
        delete_dc = api(gdi, "DeleteDC", w.BOOL, w.HDC)
        dib_section = api(
            gdi,
            "CreateDIBSection",
            w.HBITMAP,
            w.HDC,
            c.c_void_p,
            w.UINT,
            c.POINTER(c.c_void_p),
            w.HANDLE,
            w.DWORD,
        )
        select = api(gdi, "SelectObject", w.HANDLE, w.HDC, w.HANDLE)
        delete_object = api(gdi, "DeleteObject", w.BOOL, w.HANDLE)
        bit_blt = api(
            gdi,
            "BitBlt",
            w.BOOL,
            w.HDC,
            c.c_int,
            c.c_int,
            c.c_int,
            c.c_int,
            w.HDC,
            c.c_int,
            c.c_int,
            w.DWORD,
        )
        dwm = c.WinDLL("dwmapi")
        original = w.RECT()
        assert get_rect(view, c.byref(original))
        if resize:
            gutters = windows(view, "ROMAWFinderScrollGutters")[0]
            grip = w.POINT(original.right - 9, original.bottom - 9)
            target = w.POINT(center.x + 36, center.y + 36)
        else:
            grip = w.POINT(original.left + 48, original.top + 8)
            target = w.POINT(center.x - 64 + grip.x - original.left, center.y - 52 + grip.y - original.top)

        def covered_by(hwnd, point=center):
            top = window_at(point)
            while top:
                if top == hwnd:
                    return True
                top = get_parent(top)
            return False

        def capture(point=center, composited=False):
            dwm.DwmFlush()
            dc = get_dc(None)
            assert dc
            buffer = compatible(dc)
            address = c.c_void_p()
            width, height = 9, 13
            header = c.create_string_buffer(struct.pack("<IiiHH24x", 40, width, -height, 1, 32))
            bitmap = dib_section(dc, header, 0, c.byref(address), None, 0)
            previous = select(buffer, bitmap) if buffer and bitmap else None
            try:
                assert previous and address
                # Read the entire patch at once so samples cannot combine two
                # different composited frames near the moving window's edge.
                raster_op = 0x40CC0020 if composited else 0x00CC0020
                assert bit_blt(buffer, 0, 0, width, height, dc, point.x - 4, point.y - 6, raster_op)
                gdi.GdiFlush()
                return tuple(color & 0xFFFFFF for color in (w.DWORD * (width * height)).from_address(address.value))
            finally:
                if previous:
                    select(buffer, previous)
                if bitmap:
                    delete_object(bitmap)
                if buffer:
                    delete_dc(buffer)
                release_dc(None, dc)

        def fully_exposed():
            area = w.RECT()
            assert get_rect(view, c.byref(area))
            return (
                center.x + 4 < area.left
                or center.x - 4 >= area.right
                or center.y + 6 < area.top
                or center.y - 6 >= area.bottom
            ) and covered_by(background)

        def grow_corner():
            bounds = w.RECT()
            assert get_rect(gutters, c.byref(bounds))
            return w.POINT(bounds.right - 8, bounds.bottom - 8)

        def press(flags, data=0, x=0, y=0):
            event = Input(mouse=Mouse(x=x, y=y, flags=flags, data=data))
            assert send_input(1, c.byref(event), c.sizeof(event)) == 1

        def idle_pointer():
            point = w.POINT()
            wait(lambda: button_state(1) >= 0 and get_cursor(c.byref(point)), "an idle mouse pointer")
            return point

        def move(x, y):
            actual = w.POINT()
            assert get_cursor(c.byref(actual))
            # The system move loop can round the cursor by a pixel as it
            # tracks the title bar. Larger changes indicate separate input.
            if max(abs(actual.x - last_cursor[0]), abs(actual.y - last_cursor[1])) > 1:
                raise RuntimeError(
                    f"The cursor moved independently of the Finder drag from {tuple(last_cursor)} to {(actual.x, actual.y)}"
                )
            # Absolute movement goes through the same input queue as the
            # buttons and remains ordered with the resize loop's capture.
            press(0xC001, x=((x - left) * 65536 + width - 1) // width, y=((y - top) * 65536 + height - 1) // height)
            last_cursor[:] = [x, y]

        class ScrollInfo(c.Structure):
            _fields_ = [
                ("size", w.UINT),
                ("mask", w.UINT),
                ("minimum", c.c_int),
                ("maximum", c.c_int),
                ("page", w.UINT),
                ("position", c.c_int),
                ("track", c.c_int),
            ]

        def range_for(listing, axis):
            query = api(u, "GetScrollInfo", w.BOOL, w.HWND, c.c_int, c.POINTER(ScrollInfo))
            state = ScrollInfo(size=c.sizeof(ScrollInfo), mask=7)
            assert query(listing, axis, c.byref(state))
            return state

        def scrollbar_end(state):
            return max(state.minimum, state.maximum - max(1, state.page) + 1)

        if zoom_list:
            position = api(u, "SetWindowPos", w.BOOL, w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT)
            redraw_window = api(u, "RedrawWindow", w.BOOL, w.HWND, c.POINTER(w.RECT), w.HANDLE, w.UINT)
            client_rect = api(u, "GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
            is_maximized = api(u, "IsZoomed", w.BOOL, w.HWND)

            def bounds():
                result = w.RECT()
                assert get_rect(view, c.byref(result))
                return result

            def rect_tuple(rect):
                return rect.left, rect.top, rect.right, rect.bottom

            def click_at(point):
                # These are independent clicks. The pointer only has to be
                # idle when beginning each gesture.
                current = idle_pointer()
                last_cursor[:] = [current.x, current.y]
                move(point.x, point.y)
                press(0x02)

            def click_zoom():
                rect = bounds()
                point = w.POINT(rect.right - 19, rect.top + 9)
                hit = point.x & 0xFFFF | (point.y & 0xFFFF) << 16
                assert covered_by(view, point) and send(view, 0x84, 0, hit) == 9
                released = capture(point)
                click_at(point)
                try:
                    wait(lambda: capture(point) != released, "the Zoom box pressed artwork", pause=0.01)
                finally:
                    press(0x04)

            def check_arrows():
                gutters = windows(view, "ROMAWFinderScrollGutters")[0]
                bars = w.RECT()
                assert get_rect(gutters, c.byref(bars))
                for axis in (1, 0):
                    state = range_for(zoom_list, axis)
                    assert state.minimum < scrollbar_end(state), f"Missing scroll range on axis {axis}"
                    forward = state.position < scrollbar_end(state)
                    # The arrows nearest the grow box finish before the
                    # 16-pixel corner tile.
                    along = (
                        (bars.bottom if axis else bars.right) - 24 if forward else (bars.top if axis else bars.left) + 8
                    )
                    arrow = w.POINT(bars.right - 8, along) if axis else w.POINT(along, bars.bottom - 8)
                    assert covered_by(view, arrow)
                    click_at(arrow)
                    try:
                        wait(
                            lambda: range_for(zoom_list, axis).position != state.position,
                            f"mouse scroll arrow at Zoom stage on axis {axis}",
                        )
                    finally:
                        press(0x04)
                send(zoom_list, 0)

            def restore_pointer():
                actual = w.POINT()
                if (
                    get_cursor(c.byref(actual))
                    and max(abs(actual.x - last_cursor[0]), abs(actual.y - last_cursor[1])) <= 1
                ):
                    set_cursor(pointer.x, pointer.y)

            wait(lambda: covered_by(background), "the unobstructed Get Info test patch")
            assert redraw_window(desktop, None, None, 0x0585)
            baseline = capture()
            desktop_bounds = w.RECT()
            assert get_rect(desktop, c.byref(desktop_bounds))
            desktop_patch = w.POINT(desktop_bounds.left + 102, desktop_bounds.top + 105)
            assert covered_by(desktop, desktop_patch) and not covered_by(view, desktop_patch)
            desktop_baseline = capture(desktop_patch)
            assert position(view, None, 365, 335, 240, 120, 0x14)
            send(view, 0x21, desktop)
            assert redraw_window(desktop, None, None, 0x0585)
            user = rect_tuple(bounds())
            assert covered_by(view) and capture() != baseline
            folder_baseline = capture()
            pointer = idle_pointer()
            last_cursor = [pointer.x, pointer.y]
            if zoom_case:
                # Start Zoom away from the origin on both axes.
                if zoom_case != "empty":
                    check_arrows()
                    assert all(range_for(zoom_list, axis).position > 0 for axis in (0, 1))
                click_zoom()
                wait(lambda: rect_tuple(bounds()) != user, f"Zoom {description}")
                send(view, 0)
                fitted = bounds()
                work = w.RECT()
                assert client_rect(desktop, c.byref(work))
                size = fitted.right - fitted.left, fitted.bottom - fitted.top
                assert not is_maximized(view)
                if zoom_case == "oversized":
                    assert size == (work.right, work.bottom), (size, rect_tuple(work))
                    check_arrows()
                else:
                    assert size[0] < work.right and size[1] < work.bottom, (size, rect_tuple(work))
                    assert fully_exposed() and capture() == baseline, "List Zoom left stale pixels over Get Info"
                    content = w.RECT()
                    assert client_rect(zoom_list, c.byref(content))
                    # Keep the column headers visible even when there are no rows.
                    columns = sum(send(zoom_list, 0x101D, column) for column in range(9))  # LVM_GETCOLUMNWIDTH
                    assert content.right >= columns, f"The fitted list clips columns: {content.right} < {columns}"
                    if zoom_case == "compact":
                        for axis in (0, 1):
                            state = range_for(zoom_list, axis)
                            assert state.position == scrollbar_end(state) == state.minimum, (
                                axis,
                                state.position,
                                scrollbar_end(state),
                            )
                click_zoom()
                wait(lambda: rect_tuple(bounds()) == user, f"restore {description} bounds")
                send(view, 0)
                assert capture(desktop_patch) == desktop_baseline, "Zoom restore left a desktop trail"
                if zoom_case != "empty":
                    check_arrows()
                restore_pointer()
                print(f"PASS mouse Zoom and exact restore for {description}", flush=True)
                return

            for _ in range(2):
                click_zoom()
                wait(lambda: rect_tuple(bounds()) != user, "Zoom to folder contents")
                send(view, 0)
                fitted = bounds()
                work = w.RECT()
                assert client_rect(desktop, c.byref(work))
                assert not is_maximized(view) and fitted.right - fitted.left < work.right
                assert fitted.bottom - fitted.top < work.bottom
                assert fully_exposed(), f"The test patch is still under the fitted window {rect_tuple(fitted)}"
                assert capture() == baseline, "Zoom left stale pixels over the Get Info window"
                assert covered_by(view, desktop_patch), "Zoom must cover the desktop test patch before restoring"
                # The icon starts outside the original viewport. Its bounds
                # must be entirely visible after zooming.
                item = c.create_string_buffer(struct.pack("<iiii", 1, 0, 0, 0))
                assert write(handle, remote, item, 16, None)
                assert send(zoom_list, 0x100E, 0, remote)
                assert read(handle, remote, item, 16, None)
                x, y, right, bottom = struct.unpack_from("<iiii", item.raw)
                content = w.RECT()
                assert client_rect(zoom_list, c.byref(content))
                # This single-line name is wider than the native icon cell.
                assert content.right >= 500, f"Zoom clipped the long name in a {content.right}-pixel viewport"
                assert 0 <= x < right <= content.right and 0 <= y < bottom <= content.bottom, (
                    (x, y, right, bottom),
                    rect_tuple(content),
                )
                click_zoom()
                wait(lambda: rect_tuple(bounds()) == user, "restore exact pre-Zoom position and size")
                send(view, 0)
                assert covered_by(view) and capture() == folder_baseline, "Restore left stale pixels inside the folder"
                assert capture(desktop_patch) == desktop_baseline, "Restore left stale pixels over the desktop"
            restore_pointer()
            print(
                "PASS mouse Zoom fits the folder, restores its exact bounds, and repaints overlapping pixels",
                flush=True,
            )
            return

        if scroll_list:

            def scroll_thumb(axis, start, end):
                assert covered_by(view, start), "The scrollbar must be visible"
                move(start.x, start.y)
                press(0x02)
                try:
                    # Let the control capture the pointer before its first move.
                    time.sleep(0.08)
                    for i in range(1, 7):
                        move(start.x + (end.x - start.x) * i // 6, start.y + (end.y - start.y) * i // 6)
                        send(scroll_list, 0)
                        assert capture() == grow, "The grow box changed while scrolling"
                finally:
                    press(0x04)
                send(scroll_list, 0)
                return range_for(scroll_list, axis)

            send(view, 0x21, desktop)
            gutters = windows(view, "ROMAWFinderScrollGutters")[0]
            bars, content = w.RECT(), w.RECT()
            get_client = api(u, "GetClientRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
            to_screen = api(u, "ClientToScreen", w.BOOL, w.HWND, c.POINTER(w.POINT))
            assert get_rect(gutters, c.byref(bars)) and get_client(scroll_list, c.byref(content))
            origin = w.POINT()
            assert to_screen(scroll_list, c.byref(origin))
            content = w.RECT(origin.x, origin.y, origin.x + content.right, origin.y + content.bottom)
            pointer = idle_pointer()
            last_cursor = [pointer.x, pointer.y]
            grow = capture()
            assert covered_by(background, w.POINT(bars.right + 24, bars.bottom - 60))
            overlap = w.POINT(bars.right + 24, bars.bottom - 60)
            exposed = capture(overlap)
            for axis in (1, 0):
                state = range_for(scroll_list, axis)
                maximum = scrollbar_end(state)
                assert state.position == state.minimum < maximum, (axis, state.position, maximum)
                thickness = bars.right - content.right if axis else bars.bottom - content.bottom
                start = w.POINT(
                    bars.right - thickness // 2 if axis else content.left + thickness + 8,
                    content.top + thickness + 8 if axis else bars.bottom - thickness // 2,
                )
                end = w.POINT(
                    start.x if axis else content.right + 1 - thickness - 8,
                    content.bottom + 1 - thickness - 8 if axis else start.y,
                )
                before = capture(start)
                state = scroll_thumb(axis, start, end)
                assert state.position >= maximum - 8, f"Axis {axis} thumb did not reach the end: {state.position}"
                assert capture(start) != before, "The old thumb image remained in the scrollbar gutter"
                state = scroll_thumb(axis, end, start)
                assert state.position == state.minimum, f"Axis {axis} thumb did not return to the start"
                assert capture(start) == before, "The scrollbar did not repaint its original thumb"
                arrow = w.POINT(start.x, content.bottom - 6) if axis else w.POINT(content.right - 6, start.y)
                hit = arrow.x & 0xFFFF | (arrow.y & 0xFFFF) << 16
                assert window_at(arrow) == gutters, (axis, arrow.x, arrow.y, window_at(arrow), gutters)
                hit_kind = send(gutters, 0x84, 0, hit)
                assert hit_kind == (7 if axis else 6), (axis, arrow.x, arrow.y, hit_kind)
                unpressed_arrow = capture(arrow)
                move(arrow.x, arrow.y)
                press(0x02)
                try:
                    wait(lambda: capture(arrow) != unpressed_arrow, f"axis {axis} pressed scroll arrow artwork")
                    wait(
                        lambda: range_for(scroll_list, axis).position > state.minimum, f"axis {axis} scroll arrow click"
                    )
                    assert capture() == grow
                finally:
                    press(0x04)
                wait(lambda: capture(arrow) == unpressed_arrow, "restore the released scroll arrow")
                first = w.POINT(start.x, content.top + 8) if axis else w.POINT(content.left + 8, start.y)
                move(first.x, first.y)
                press(0x02)
                try:
                    wait(lambda: range_for(scroll_list, axis).position == state.minimum, "return by scrollbar arrow")
                finally:
                    press(0x04)
                assert capture(overlap) == exposed, "Scrolling left a trail on the overlapping Get Info window"
            move(content.left + 30, content.top + 40)
            press(0x0800, (-120) & 0xFFFFFFFF)
            vertical_start = range_for(scroll_list, 1).minimum
            wait(lambda: range_for(scroll_list, 1).position > vertical_start, "vertical mouse wheel scrolling")
            press(0x0800, 120)
            wait(lambda: range_for(scroll_list, 1).position == vertical_start, "mouse wheel return")
            assert capture() == grow and capture(overlap) == exposed
            move(pointer.x, pointer.y)
            print(
                "PASS both mouse-driven scrollbars and the wheel with stable grow and overlapping window pixels",
                flush=True,
            )
            return

        wait(lambda: covered_by(background) and not covered_by(view), f"visible {description}")
        assert covered_by(view, grip), "The mouse drag handle must be visible"
        hit_point = grip.x & 0xFFFF | (grip.y & 0xFFFF) << 16
        if resize:
            send(view, 0x21, desktop)
            redraw_window = api(u, "RedrawWindow", w.BOOL, w.HWND, c.POINTER(w.RECT), w.HANDLE, w.UINT)
            assert redraw_window(view, None, None, 0x0585)
        wait(lambda: send(view, 0x84, 0, hit_point) == (17 if resize else 2), "the intended drag handle")
        baseline = capture()

        def stable_baseline():
            nonlocal baseline
            current = capture()
            if current == baseline:
                return True
            baseline = current
            return False

        wait(stable_baseline, f"stable background under {description}")
        gdi_region = api(gdi, "CreateRectRgn", w.HANDLE, c.c_int, c.c_int, c.c_int, c.c_int)
        point_in_region = api(gdi, "PtInRegion", w.BOOL, w.HANDLE, c.c_int, c.c_int)
        window_region = api(u, "GetWindowRgn", c.c_int, w.HWND, w.HANDLE)
        find_window = api(u, "FindWindowExW", w.HWND, w.HWND, w.HWND, w.LPCWSTR, w.LPCWSTR)

        def frame(hwnd):
            area = w.RECT()
            assert get_rect(hwnd, c.byref(area))
            return area.left, area.top, area.right, area.bottom

        def outline():
            hwnd = None
            while hwnd := find_window(None, hwnd, "Static", "Finder window outline"):
                pid = w.DWORD()
                window_pid(hwnd, c.byref(pid))
                if pid.value == child.pid:
                    return hwnd
            return find_window(desktop, None, "Static", "Finder window outline")

        def near(actual, expected):
            return all(abs(a - b) <= 1 for a, b in zip(actual, expected))

        def grow_guides(bounds):
            left, top, right, bottom = bounds
            mid_x, mid_y = (left + right) // 2, (top + bottom) // 2
            # SheepShaver's title guide is 18 pixels from the top and the
            # scrollbar guides are 15 pixels inside the visible outer edge.
            return (
                (w.POINT(mid_x, top), True),
                (w.POINT(mid_x, top + 18), True),
                (w.POINT(mid_x, bottom - 16), True),
                (w.POINT(left, mid_y), False),
                (w.POINT(right - 16, mid_y), False),
                (w.POINT(right - 1, mid_y), False),
                (w.POINT(mid_x, bottom - 1), True),
            )

        def check_grow_pattern(box, references):
            to_screen = api(u, "ClientToScreen", w.BOOL, w.HWND, c.POINTER(w.POINT))
            origin = w.POINT()
            assert to_screen(desktop, c.byref(origin))
            for (guide, horizontal), (point, backdrop) in zip(grow_guides(frame(box)), references):
                actual = capture(point, composited=True)
                expected = []
                for index, pixel in enumerate(backdrop):
                    x, y = index % 9 - 4, index // 9 - 6
                    x, y = point.x + x, point.y + y
                    phase = x - origin.x + y - origin.y
                    invert = (y == guide.y if horizontal else x == guide.x) and phase % 2 == 0
                    expected.append(pixel ^ 0xFFFFFF if invert else pixel)
                differences = [
                    (i % 9 - 4, i // 9 - 6, f"{backdrop[i]:06x}", f"{want:06x}", f"{seen:06x}")
                    for i, (want, seen) in enumerate(zip(expected, actual))
                    if want != seen
                ]
                assert not differences, f"Incorrect checkerboard line at {(point.x, point.y)}: {differences[:8]}"

        def drag_outline(start, end, cancel=False):
            before = frame(view)
            dx, dy = end.x - start.x, end.y - start.y
            expected = (
                before[0] + (0 if resize else dx),
                before[1] + (0 if resize else dy),
                before[2] + dx,
                before[3] + dy,
            )
            current = idle_pointer()
            last_cursor[:] = [current.x, current.y]
            assert covered_by(view, start)
            if resize:
                trail = w.POINT(before[2] + dx // 3 - 1, (before[1] + before[3]) // 2)
            elif abs(dx) > abs(dy):
                trail = w.POINT(before[0] + dx // 3 + 1, (before[1] + before[3]) // 2 + dy // 3)
            else:
                trail = w.POINT((before[0] + before[2]) // 2 + dx // 3, before[1] + dy // 3 + 1)
            background_pixels = capture(trail, composited=True)
            references = (
                [(point, capture(point, composited=True)) for point, _ in grow_guides(expected)] if resize else []
            )
            move(start.x, start.y)
            press(0x02)
            try:
                send(view, 0)
                for step in (1, 2, 3):
                    if step == 3:
                        # Sweep without waiting for the underlying Finder views to repaint.
                        for substep in range(21, 30):
                            move(start.x + dx * substep // 30, start.y + dy * substep // 30)
                            time.sleep(0.003)
                    move(start.x + dx * step // 3, start.y + dy * step // 3)
                    partial_dx, partial_dy = dx * step // 3, dy * step // 3
                    preview = (
                        before[0] + (0 if resize else partial_dx),
                        before[1] + (0 if resize else partial_dy),
                        before[2] + partial_dx,
                        before[3] + partial_dy,
                    )
                    box = wait(lambda: outline(), "the drag outline")
                    wait(
                        lambda: near(frame(box), preview),
                        f"outline following the mouse toward {preview} for {description}; first at {frame(box)}",
                    )
                    assert frame(view) == before, "The real window changed before mouse release"
                    if step == 1:
                        assert capture(trail, composited=True) != background_pixels, "The outline was not visible"
                assert (
                    capture(trail, composited=True) == background_pixels
                ), f"The previous outline left a trail over {description} with the mouse still held"
                if resize:
                    check_grow_pattern(box, references)
                else:
                    region = gdi_region(0, 0, 0, 0)
                    assert region
                    try:
                        assert window_region(box, region) != 0
                        assert point_in_region(region, 0, 0)
                        assert not point_in_region(region, 20, 40), "The outline obscured its interior"
                    finally:
                        delete_object(region)
                if cancel:
                    assert capture() == baseline, "The drag outline covered the background inside its frame"
                    key(view, VK_ESCAPE)
                    wait(lambda: not outline(), "Escape removes the drag outline")
            finally:
                press(0x04)
            wait(lambda: frame(view) == before if cancel else near(frame(view), expected), "final window bounds")
            wait(lambda: not outline(), "outline removed after release")
            send(view, 0)

        pointer = idle_pointer()
        last_cursor = [pointer.x, pointer.y]
        drag_outline(grip, target, cancel=True)
        assert capture() == baseline, f"Cancelled outline left a trail over {description}"
        old_grow = capture(grip) if resize else None
        drag_outline(grip, target)
        assert covered_by(view) and capture() != baseline
        if resize:
            new_grip = grow_corner()
            assert near((new_grip.x, new_grip.y), (target.x, target.y))
            assert capture(grip) != old_grow, "The old grow box remained visible after resizing"
            drag_outline(new_grip, grip)
        else:
            drag_outline(target, grip)
        assert fully_exposed() and capture() == baseline, f"Outline drop left a trail over {description}"
        actual = w.POINT()
        if get_cursor(c.byref(actual)) and near((actual.x, actual.y), last_cursor):
            set_cursor(pointer.x, pointer.y)
        gesture = "grow" if resize else "drag"
        print(f"PASS {gesture} outline and Escape over {description}", flush=True)

    def check_resize_scroll():
        get_rect = api(u, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        position = api(u, "SetWindowPos", w.BOOL, w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT)
        command(desktop, "Host Folder", FINDER_OPEN)
        folder = wait(lambda: named(desktop, "FinderFolder", "Host Folder"), "open folder for grow check")
        view, _ = info(desktop, "Host File")
        assert position(view, None, 390, 240, 0, 0, 0x15)
        assert position(folder, 0, 60, 80, 360, 240, 0x10)
        bounds = w.RECT()
        assert get_rect(view, c.byref(bounds))
        check_exposed_pixels(
            folder, view, w.POINT(bounds.left + 44, bounds.top + 40), "Get Info behind the grow box", resize=True
        )
        # List columns exceed the viewport width and the fixture supplies
        # enough rows to make both scrollbars active.
        assert post(folder, WM_COMMAND, FINDER_LIST, 0)
        listing = windows(folder, "SysListView32")[0]
        wait(lambda: send(listing, 0x108F) == 1, "Finder list view")
        gutters = windows(folder, "ROMAWFinderScrollGutters")[0]
        assert get_rect(gutters, c.byref(bounds))
        check_exposed_pixels(
            folder, view, w.POINT(bounds.right - 8, bounds.bottom - 8), "folder scrollbar", scroll_list=listing
        )
        close(view)
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0

    def check_zoom():
        get_rect = api(u, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        position = api(u, "SetWindowPos", w.BOOL, w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT)
        command(desktop, "Host Folder", FINDER_OPEN)
        folder = wait(lambda: named(desktop, "FinderFolder", "Host Folder"), "open folder for Zoom check")
        view, _ = info(desktop, "Host File")
        assert position(folder, None, 5, 330, 240, 120, 0x14)
        assert position(view, None, 365, 335, 0, 0, 0x15)
        listing = windows(folder, "SysListView32")[0]
        assert send(listing, 0x108F) == 0, "The fixture starts in icon view"
        offset = c.create_string_buffer(struct.pack("<ii", 275, 138))
        assert write(handle, remote, offset, 8, None)
        send(listing, 0x1031, 0, remote)  # LVM_SETITEMPOSITION32
        bounds = w.RECT()
        assert get_rect(view, c.byref(bounds))
        check_exposed_pixels(folder, view, w.POINT(bounds.left + 44, bounds.top + 40), "Zoom", zoom_list=listing)
        close(folder)
        for name, mode, width in (
            ("Empty List", "empty", 56),
            ("Compact List", "compact", 56),
            ("Oversized List", "oversized", 144),
            ("Oversized Icons", "oversized", None),
        ):
            command(desktop, name, FINDER_OPEN)
            folder = wait(lambda: named(desktop, "FinderFolder", name), f"open {name}")
            assert position(folder, None, 5, 330, 240, 120, 0x14)
            listing = windows(folder, "SysListView32")[0]
            if width is None:
                assert send(listing, 0x108F) == 0
                offset = c.create_string_buffer(struct.pack("<ii", 1100, 800))
                assert write(handle, remote, offset, 8, None)
                send(listing, 0x1031, 1, remote)  # LVM_SETITEMPOSITION32
            else:
                assert post(folder, WM_COMMAND, FINDER_LIST, 0)
                wait(lambda: send(listing, 0x108F) == 1, f"{name} in list view")
                for column in range(9):
                    assert send(listing, 0x101E, column, width)  # LVM_SETCOLUMNWIDTH
            check_exposed_pixels(
                folder, view, w.POINT(bounds.left + 44, bounds.top + 40), name, zoom_list=listing, zoom_case=mode
            )
            close(folder)
        close(view)
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0

    def check_redraw():
        get_rect = api(u, "GetWindowRect", w.BOOL, w.HWND, c.POINTER(w.RECT))
        position = api(u, "SetWindowPos", w.BOOL, w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT)
        redraw_window = api(u, "RedrawWindow", w.BOOL, w.HWND, c.POINTER(w.RECT), w.HANDLE, w.UINT)

        def place(window, x, y):
            assert position(window, None, x, y, 0, 0, 0x15)
            # Paint the test layout before sampling pixels for the mouse drag.
            assert redraw_window(desktop, None, None, 0x0585)

        view, _ = info(desktop, "Host File")
        bounds = w.RECT()
        assert get_rect(desktop, c.byref(bounds))
        check_exposed_pixels(view, desktop, w.POINT(bounds.right - 120, bounds.bottom - 120), "desktop pattern")
        close(view)
        command(desktop, "Host Folder", FINDER_OPEN)
        folder = wait(lambda: named(desktop, "FinderFolder", "Host Folder"), "open folder for redraw check")
        view, _ = info(desktop, "Host File")
        assert get_rect(folder, c.byref(bounds))
        check_exposed_pixels(view, folder, w.POINT(bounds.right - 9, bounds.top + 116), "folder scrollbar")
        # Lay out the two title bars without overlap before the reverse drag.
        place(view, 110, 90)
        place(folder, 386, 200)
        assert get_rect(view, c.byref(bounds))
        check_exposed_pixels(folder, view, w.POINT(bounds.left + 44, bounds.top + 40), "Get Info icon under folder")
        close(view)
        place(folder, 60, 112)
        assert get_rect(desktop, c.byref(bounds))
        check_exposed_pixels(folder, desktop, w.POINT(bounds.right - 120, bounds.bottom - 120), "desktop under folder")
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0

    def check_icons():
        nonlocal child, handle, remote, desktop
        volume = "Desktop Metadata"
        copied = icon_clipboard_reader(api, u, k)
        view, _ = info(desktop, "Host File")
        artwork = copy_family(view, copied)
        close(view)
        for name in ("Host Conflict", "Host Folder"):
            view, _ = info(desktop, "Host File")
            assert copy_family(view, copied) == artwork
            close(view)
            view, _ = info(desktop, name)
            icon_command(view, FINDER_PASTE)
            assert copy_family(view, copied) == artwork
            close(view)
            view, _ = info(desktop, name)
            assert copy_family(view, copied) == artwork
            icon_command(view, FINDER_CLEAR)
            assert copy_family(view, copied) != artwork
            close(view)

        view, _ = info(desktop, "Host File")
        assert copy_family(view, copied) == artwork
        close(view)
        command(desktop, volume, FINDER_OPEN)
        mounted = wait(lambda: named(desktop, "FinderFolder", volume), "open image for icon editing")
        for name in ("Document", "Tools"):
            view, _ = info(mounted, name)
            icon_command(view, FINDER_PASTE)
            assert copy_family(view, copied) == artwork
            close(view)
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0
        remote = 0
        (apps.parent / "ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=none\n")
        child = launch()
        handle = int(child._handle)
        remote = alloc(handle, None, 4096, 0x3000, 4)
        assert remote
        desktop = wait(lambda: named(None, "FinderFolder", "ROMAW"), "Finder after remount")
        command(desktop, volume, FINDER_OPEN)
        mounted = wait(lambda: named(desktop, "FinderFolder", volume), "reopen writable image")
        for name in ("Document", "Tools"):
            view, _ = info(mounted, name)
            assert copy_family(view, copied) == artwork
            icon_command(view, FINDER_CLEAR)
            assert copy_family(view, copied) != artwork
            close(view)
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0
        print("PASS live Get Info Copy, Paste, Clear and HFS remount for files and folders", flush=True)

    def custom_flag():
        bytes_ = sidecar.read_bytes()
        for index in range(struct.unpack_from(">H", bytes_, 24)[0]):
            entry, offset, _ = struct.unpack_from(">III", bytes_, 26 + index * 12)
            if entry == 9:
                return bytes_[offset + 8] & 4
        raise RuntimeError("The host file lost its Finder information")

    try:
        assert remote, "Could not allocate the list-view message buffer"
        desktop = wait(lambda: named(None, "FinderFolder", "ROMAW"), "native Finder desktop")
        assert (local / "Finder").is_dir(), "Finder settings must stay in the test profile"
        if icons:
            check_icons()
            return
        if redraw:
            check_redraw()
            return
        if resize_scroll:
            check_resize_scroll()
            return
        if zoom:
            check_zoom()
            return
        check_memory_panel()
        check_suitcases()
        view, _ = info(desktop, "Host Text.txt")
        assert named(view, "Static", "Plain text fixture"), [caption(x) for x in windows(view)]
        close(view)
        view, edit = info(desktop, "Host File")
        icon = named(view, "Static", "Get Info icon")
        assert icon and custom_flag()
        assert post(icon, WM_LBUTTONDOWN, 1, 8 | 8 << 16)
        assert post(icon, WM_LBUTTONUP, 0, 8 | 8 << 16)
        assert post(desktop, WM_COMMAND, FINDER_CLEAR, 0)
        wait(lambda: not custom_flag(), "clear the selected Get Info icon")
        assert (fixture / "Host File").read_bytes() == b"source"
        print("PASS Get Info icon selection and Clear through Edit", flush=True)
        change(edit, "Cancelled Host")
        key(edit, VK_ESCAPE)
        wait(lambda: caption(edit) == "Host File", "Escape on host file")
        conflict(view, edit, "Host Conflict")
        assert (fixture / "Host File").read_bytes() == b"source"
        assert (fixture / "Host Conflict").read_bytes() == b"destination"
        change(edit, "Renamed Host File")
        key(edit, VK_RETURN)
        wait(lambda: caption(view) == "Renamed Host File Info", "Return on host file")
        assert not (fixture / "Host File").exists() and (fixture / "Renamed Host File").read_bytes() == b"source"
        close(view)
        wait(lambda: "Renamed Host File" in list_rows(desktop)[1], "refreshed host file")
        command(desktop, "Host Folder", FINDER_OPEN)
        folder = wait(lambda: named(desktop, "FinderFolder", "Host Folder"), "open host folder")
        view, edit = info(desktop, "Host Folder")
        change(edit, "Cancelled Host Folder")
        key(edit, VK_ESCAPE)
        wait(lambda: caption(edit) == "Host Folder", "Escape on host folder")
        conflict(view, edit, "Host Conflict", close_window=True)
        assert (fixture / "Host Folder" / "Contents").read_bytes() == b"nested"
        change(edit, "Renamed Host Folder")
        close(view)
        wait(lambda: caption(folder) == "Renamed Host Folder", "open host folder title")
        assert "Contents" in list_rows(folder)[1]
        print("PASS host Return, Escape, conflict, close and open folder title", flush=True)
        volume = "Desktop Metadata"
        command(desktop, volume, FINDER_OPEN)
        mounted = wait(lambda: named(desktop, "FinderFolder", volume), "open writable image")
        view, edit = info(mounted, "Document")
        assert named(view, "Static", "Mounted letter"), [caption(x) for x in windows(view)]
        assert post(mounted, WM_COMMAND, FINDER_LIST, 0)
        wait(lambda: "Mounted letter" in list_rows(mounted, 3)[1], "mounted document Kind in list view")
        print("PASS FTM Kind in host Get Info and Desktop DB Kind in mounted Get Info and list view", flush=True)
        change(edit, "Cancelled Image")
        key(edit, VK_ESCAPE)
        wait(lambda: caption(edit) == "Document", "Escape on image file")
        conflict(view, edit, "Tools")
        change(edit, "Renamed Image File")
        key(edit, VK_RETURN)
        wait(lambda: caption(view) == "Renamed Image File Info", "Return on image file")
        close(view)
        wait(lambda: "Renamed Image File" in list_rows(mounted)[1], "refreshed image file")
        command(mounted, "Tools", FINDER_OPEN)
        folder = wait(lambda: named(desktop, "FinderFolder", "Tools"), "open image folder")
        files = list_rows(folder)[1]
        view, edit = info(mounted, "Tools")
        change(edit, "Cancelled Image Folder")
        key(edit, VK_ESCAPE)
        wait(lambda: caption(edit) == "Tools", "Escape on image folder")
        conflict(view, edit, "Renamed Image File")
        change(edit, "Renamed Image Folder")
        close(view)
        wait(lambda: caption(folder) == "Renamed Image Folder", "open image folder title")
        assert list_rows(folder)[1] == files
        assert post(desktop, WM_CLOSE, 0, 0)
        assert child.wait(timeout=12) == 0, "ROMAW failed to exit cleanly"
        print("PASS mounted Return, Escape, conflict, close and open folder title", flush=True)
    except Exception:
        print(f"Failure process status: {child.poll()}", flush=True)
        raise
    finally:
        if remote and child.poll() is None:
            free(handle, remote, 0, 0x8000)
        if child.poll() is None:
            child.kill()
        child.wait()
