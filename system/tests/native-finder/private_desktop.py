# +===================================================================
#   File:      private_desktop.py
#
#   Summary:   Isolate live Finder clipboard tests in a private Windows station.
#
#   Classes:   FinderProcess and Win32 process startup records.
#
#   Functions: private_desktop.
#
#   Origin:    Executor native Finder regression fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

from contextlib import contextmanager
import ctypes as c
from ctypes import wintypes as w
import subprocess
from uuid import uuid4


class StartupInfo(c.Structure):
    _fields_ = [
        ("size", w.DWORD),
        ("reserved", w.LPWSTR),
        ("desktop", w.LPWSTR),
        ("title", w.LPWSTR),
        ("x", w.DWORD),
        ("y", w.DWORD),
        ("width", w.DWORD),
        ("height", w.DWORD),
        ("columns", w.DWORD),
        ("rows", w.DWORD),
        ("fill", w.DWORD),
        ("flags", w.DWORD),
        ("show", w.WORD),
        ("reserved_size", w.WORD),
        ("reserved_data", c.c_void_p),
        ("stdin", w.HANDLE),
        ("stdout", w.HANDLE),
        ("stderr", w.HANDLE),
    ]


class ProcessInfo(c.Structure):
    _fields_ = [("process", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]


def api(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype, function.argtypes = result, arguments
    return function


class FinderProcess:
    def __init__(self, kernel, handle, pid):
        self.kernel = kernel
        self._handle = handle
        self.pid = pid
        self.returncode = None

    def poll(self):
        status = w.DWORD()
        if not self.kernel.GetExitCodeProcess(self._handle, c.byref(status)):
            raise c.WinError(c.get_last_error())
        self.returncode = None if status.value == 259 else status.value
        return self.returncode

    def wait(self, timeout=30):
        if self.kernel.WaitForSingleObject(self._handle, int(timeout * 1000)) != 0:
            raise TimeoutError("The isolated ROMAW did not exit")
        return self.poll()

    def kill(self):
        self.kernel.TerminateProcess(self._handle, 1)


@contextmanager
def private_desktop():
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    pointer = c.c_void_p
    handle = w.HANDLE
    create_station = api(user, "CreateWindowStationW", handle, w.LPCWSTR, w.DWORD, w.DWORD, pointer)
    set_station = api(user, "SetProcessWindowStation", w.BOOL, handle)
    get_station = api(user, "GetProcessWindowStation", handle)
    close_station = api(user, "CloseWindowStation", w.BOOL, handle)
    create_desktop = api(user, "CreateDesktopW", handle, w.LPCWSTR, w.LPCWSTR, pointer, w.DWORD, w.DWORD, pointer)
    set_desktop = api(user, "SetThreadDesktop", w.BOOL, handle)
    get_desktop = api(user, "GetThreadDesktop", handle, w.DWORD)
    close_desktop = api(user, "CloseDesktop", w.BOOL, handle)
    thread_id = api(kernel, "GetCurrentThreadId", w.DWORD)
    create_process = api(
        kernel,
        "CreateProcessW",
        w.BOOL,
        w.LPCWSTR,
        w.LPWSTR,
        pointer,
        pointer,
        w.BOOL,
        w.DWORD,
        pointer,
        w.LPCWSTR,
        c.POINTER(StartupInfo),
        c.POINTER(ProcessInfo),
    )
    api(kernel, "WaitForSingleObject", w.DWORD, handle, w.DWORD)
    api(kernel, "GetExitCodeProcess", w.BOOL, handle, c.POINTER(w.DWORD))
    api(kernel, "TerminateProcess", w.BOOL, handle, w.UINT)
    api(kernel, "CloseHandle", w.BOOL, handle)

    name = "ROMAWTest" + uuid4().hex
    original_station, original_desktop = get_station(), get_desktop(thread_id())
    station = create_station(name, 0, 0x37F, None)
    if not station:
        raise c.WinError(c.get_last_error())
    desktop = None
    children = []
    try:
        if not set_station(station):
            raise c.WinError(c.get_last_error())
        desktop = create_desktop("Test", None, None, 0, 0x1FF, None)
        if not desktop or not set_desktop(desktop):
            raise c.WinError(c.get_last_error())

        def launch(args, cwd, env):
            # subprocess.STARTUPINFO does not expose lpDesktop. Name both the
            # station and desktop so ROMAW joins this private clipboard.
            startup = StartupInfo(size=c.sizeof(StartupInfo), desktop=name + "\\Test")
            result = ProcessInfo()
            command = c.create_unicode_buffer(subprocess.list2cmdline(args))
            variables = sorted(env.items(), key=lambda item: item[0].casefold())
            environment = c.create_unicode_buffer("\0".join(f"{key}={value}" for key, value in variables) + "\0\0")
            if not create_process(
                None, command, None, None, False, 0x400, environment, str(cwd), c.byref(startup), c.byref(result)
            ):
                raise c.WinError(c.get_last_error())
            kernel.CloseHandle(result.thread)
            process = FinderProcess(kernel, result.process, result.pid)
            children.append(process)
            return process

        yield desktop, launch
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
                child.wait()
            kernel.CloseHandle(child._handle)
        set_station(original_station)
        set_desktop(original_desktop)
        if desktop:
            close_desktop(desktop)
        close_station(station)
