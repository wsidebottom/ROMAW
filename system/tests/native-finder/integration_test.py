# +===================================================================
#   File:      integration_test.py
#
#   Summary:   Check native ROMAW startup and the built-in classic application launcher.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Existing repository source in executor/tests/native-finder; original attribution and provenance comments
#              are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Check native ROMAW startup and the built-in classic application launcher."""

import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

import runtime_fixtures as fixtures

ROOT = Path(__file__).resolve().parents[3]


def main():
    """Check native entry, real guest launches and optional installed desk hosts.

    GDB stops at specific runtime milestones; child handles and bounded waits
    keep probes separate from any pre-existing ROMAW windows.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=ROOT / "windows-binaries/bin/ROMAW.exe")
    parser.add_argument("--gdb", type=Path, default=Path("C:/msys64/mingw64/bin/gdb.exe"))
    parser.add_argument("--desk-hosts", action="store_true")
    get_info = parser.add_mutually_exclusive_group()
    get_info.add_argument("--get-info", action="store_true", help="exercise Get Info renames in the native Finder UI")
    get_info.add_argument("--get-info-icons", action="store_true", help="exercise Get Info icon editing and remounting")
    get_info.add_argument(
        "--get-info-redraw", action="store_true", help="check exposed pixels during mouse drags of child windows"
    )
    get_info.add_argument(
        "--get-info-resize-scroll", action="store_true", help="check grow box and scrollbars with mouse input"
    )
    get_info.add_argument("--finder-zoom", action="store_true", help="check folder Zoom and redraw with mouse input")
    args = parser.parse_args()
    if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
        parser.error("Run with Windows x64 Python after building the integrated launcher test")
    runtime = args.runtime.resolve()
    if args.get_info or args.get_info_icons or args.get_info_redraw or args.get_info_resize_scroll or args.finder_zoom:
        from get_info_ui import check_get_info

        check_get_info(
            ROOT,
            runtime,
            icons=args.get_info_icons,
            redraw=args.get_info_redraw,
            resize_scroll=args.get_info_resize_scroll,
            zoom=args.finder_zoom,
        )
        return
    folder = Path(tempfile.mkdtemp(prefix="integrated-", dir=ROOT / "system/obj/native-finder"))
    print(f"Integration fixtures and logs: {folder}", flush=True)

    # Stop at the native entry point. SDL and guest CPU startup must never be
    # entered on this path, including when launched from an unrelated directory.
    script = folder / "native.gdb"
    script.write_text(
        "set pagination off\nset confirm off\nset breakpoint pending on\n"
        "break SDL_Init\ncommands\nsilent\nquit 2\nend\n"
        "break initialize_68k_emulator\ncommands\nsilent\nquit 3\nend\n"
        "break romaw_finder_main\ncommands\nsilent\n"
        'printf "ROMAW_NATIVE_FINDER_READY\\n"\nquit 0\nend\nrun\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        [str(args.gdb), "-q", "-batch", "-x", str(script), str(runtime)],
        cwd=folder,
        capture_output=True,
        text=True,
        timeout=45,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    (folder / "native.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode or "ROMAW_NATIVE_FINDER_READY" not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    print("PASS: no-argument launch enters native Finder before SDL or guest CPU startup")

    m68k = fixtures.make_68k(folder)
    script = folder / "blessing.gdb"
    script.write_text(
        "set pagination off\nset confirm off\nset $system_volume = 0\n"
        "break bless_host_system_folder\ncommands\nsilent\n"
        "if !volume || volume->vcbCTRef || !directory\nquit 2\nend\n"
        "set $system_volume = volume\nset $system_directory = directory\ncontinue\nend\n"
        "break romaw_main\ncommands\nsilent\n"
        "if !$system_volume || $system_volume->vcbFndrInfo[0] != $system_directory\nquit 3\nend\n"
        'printf "ROMAW_SYSTEM_VOLUME:%s\\n", ((VCBExtra *)$system_volume)->unixname\n'
        "set $finder_bytes = (unsigned char *)$system_volume->vcbFndrInfo\n"
        'printf "ROMAW_BLESSED_BYTES:%02x%02x%02x%02x\\n", $finder_bytes[0], $finder_bytes[1], $finder_bytes[2], $finder_bytes[3]\n'
        'printf "ROMAW_SYSTEM_FOLDER_BLESSED\\n"\nquit 0\nend\n'
        f'set args "{m68k.as_posix()}"\nrun\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        [str(args.gdb), "-q", "-batch", "-x", str(script), str(runtime)],
        cwd=folder,
        capture_output=True,
        text=True,
        timeout=45,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    (folder / "blessing.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode or "ROMAW_SYSTEM_FOLDER_BLESSED" not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    volume = next(
        line.partition(":")[2] for line in result.stdout.splitlines() if line.startswith("ROMAW_SYSTEM_VOLUME:")
    )
    expected = Path(os.environ["USERPROFILE"]) / ".romaw/Apps"
    if Path(volume).resolve() != expected.resolve():
        raise RuntimeError(f"System Folder is on {volume}, expected {expected}")
    blessing = next(
        line.partition(":")[2] for line in result.stdout.splitlines() if line.startswith("ROMAW_BLESSED_BYTES:")
    )
    records = expected.parent / "volume-metadata"
    record = next(
        path.parent / "volume.finderinfo"
        for path in records.glob("*/volume.path")
        if path.read_text(encoding="utf-8").casefold() == volume.casefold()
    )
    stored = record.read_bytes()
    if len(stored) != 32 or stored[:4].hex() != blessing:
        raise RuntimeError("The System Folder blessing was not persisted correctly")
    print("PASS: volume Finder information names the active System Folder catalog ID")
    ppc = fixtures.module("ppc_probe", ROOT / "synppc/tests/make_ppc_probe.py")
    ppc.make_probe(folder)
    launcher = ROOT / "system/obj/native-finder/windows-x64/integrated-launch-test.exe"
    for app in (m68k, folder / "PPC Toolbox Probe"):
        result = subprocess.run(
            [str(launcher), "--probe", str(runtime), str(app), "1", "0"],
            cwd=folder,
            capture_output=True,
            text=True,
            timeout=45,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        (folder / (app.name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        print(f"PASS: built-in launcher runs {app.name} and its child exits cleanly")

    if args.desk_hosts:
        system = Path(os.environ["USERPROFILE"]) / ".romaw/Apps/System Folder"
        for app in (
            system / "Apple Menu Items/Calculator",
            system / "Control Panels/Sound",
        ):
            command = [str(runtime), "-syszone", "2m", "-applzone", "4m"]
            if app.name == "Calculator":
                resource = fixtures.drvr_id(app)
                command += ["-desk-resource", str(resource)]
            command.append(str(app))
            child = subprocess.Popen(command, cwd=folder, creationflags=subprocess.CREATE_NO_WINDOW)
            script = folder / "desk.gdb"
            script.write_text(
                "set pagination off\nset confirm off\nset print thread-events off\n"
                "break C_WaitNextEvent\ncommands\nsilent\n"
                "if ROMlib_desk_host_active && !startup_pending\n"
                "if desk_shape_key == 0\nquit 1\nend\n"
                'printf "INTEGRATED_DESK_HOST_READY\\n"\n'
                "set ROMlib_desk_host_close = 1\ndetach\nquit 0\nend\ncontinue\nend\ncontinue\n",
                encoding="utf-8",
            )
            try:
                result = subprocess.run(
                    [
                        str(args.gdb),
                        "-q",
                        "-batch",
                        "-p",
                        str(child.pid),
                        "-x",
                        str(script),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=35,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                (folder / (app.name + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
                if result.returncode or "INTEGRATED_DESK_HOST_READY" not in result.stdout:
                    raise RuntimeError(result.stdout + result.stderr)
                if child.wait(timeout=15):
                    raise RuntimeError(f"{app.name} did not exit cleanly")
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            print(f"PASS: {app.name} guest UI initializes and closes cleanly")


if __name__ == "__main__":
    main()
