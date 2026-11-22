#!/usr/bin/env python3
# +===================================================================
#   File:      image_lock_test.py
#
#   Summary:   Verify effective mounted-image locks and mutation rejection on disposable original HFS fixtures.
#
#   Classes:   None.
#
#   Functions: probe, main.
#
#   Origin:    Executor mounted-volume lock regression; uses repository-generated HFS images only.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


def probe():
    import gdb

    def call(expression):
        return int(gdb.parse_and_eval(expression))

    def check(expression, expected=0):
        actual = call(expression)
        if actual != expected:
            raise RuntimeError(f"{expression}: got {actual}, expected {expected}")

    volume = call("(void*)calloc(1,sizeof(FinderImageVolume))")
    scratch = call("(void*)calloc(1,1024)")
    check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
    reference = call(f"((FinderImageVolume*){volume})->reference")
    writable = bool(call("$writable"))
    if call("$soft_lock"):
        # VCB fields retain the guest big-endian representation on the x64 host.
        vcb = call(f"(void*)ROMlib_findvcb({reference},0,0,0)")
        original = call(f"((VCB*){vcb})->vcbAtrb")
        gdb.execute(f"set ((VCB*){vcb})->vcbAtrb=(unsigned short)({original}|0x0080)")
        check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
    expected = 0 if writable else -44
    check(f"((FinderImageVolume*){volume})->read_only", int(not writable))
    check(f"ROMlib_finder_image_writable({reference})", expected)
    check(f'ROMlib_finder_image_lookup({reference},2,"Document",(unsigned int*){scratch})')
    document = call(f"*(unsigned int*){scratch}")
    # All native Finder mutations must agree with the snapshot used by the status strip.
    check(
        f'ROMlib_finder_image_create({reference},2,"Lock Test",1,(unsigned char*){scratch+32},(unsigned int*){scratch})',
        expected,
    )
    if writable:
        check(f'ROMlib_finder_image_delete({reference},2,"Lock Test")')
    check(f'ROMlib_finder_image_rename({reference},2,"Document","Renamed")', expected)
    if writable:
        check(f'ROMlib_finder_image_rename({reference},2,"Renamed","Document")')
    check(f'ROMlib_finder_image_position({reference},2,"Document",180,96)', expected)
    check(f"ROMlib_finder_image_window({reference},2,80,80,500,320)", expected)
    check(f'ROMlib_finder_image_set_info({reference},2,"Document",(unsigned char*){scratch+32})', expected)
    for resource in (0, 1):
        check(f'ROMlib_finder_image_open_fork({reference},2,"Document",{resource},(short*){scratch})', expected)
        if writable:
            file = call(f"*(short*){scratch}")
            check(f"ROMlib_finder_image_write({file},(void*){scratch+32},4)")
            # A lock acquired after opening must also protect already-open forks.
            vcb = call(f"(void*)ROMlib_findvcb({reference},0,0,0)")
            original = call(f"((VCB*){vcb})->vcbAtrb")
            for mask in (0x8000, 0x0080):
                swapped = ((mask & 255) << 8) | (mask >> 8)
                gdb.execute(f"set ((VCB*){vcb})->vcbAtrb=(short)({original}|{swapped})")
                check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
                check(f"((FinderImageVolume*){volume})->read_only", 1)
                check(f"ROMlib_finder_image_writable({reference})", -44)
                fcb = gdb.parse_and_eval(f"ROMlib_refnumtofcbp({file})").dereference()
                before = tuple(int(fcb[field]) for field in ("fcbEOF", "fcbPLen", "fcbCrPs"))
                error = call(f"ROMlib_finder_image_write({file},(void*){scratch+32},4)")
                if error not in (-44, -46):
                    raise RuntimeError(f"Already-open fork accepted a locked write: {error}")
                fcb = gdb.parse_and_eval(f"ROMlib_refnumtofcbp({file})").dereference()
                after = tuple(int(fcb[field]) for field in ("fcbEOF", "fcbPLen", "fcbCrPs"))
                if before != after:
                    raise RuntimeError("Rejected write changed the fork size or file position")
                gdb.execute(f"set ((VCB*){vcb})->vcbAtrb=(short){original}")
            check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
            check(f"((FinderImageVolume*){volume})->read_only", 0)
            check(f"ROMlib_finder_image_close_fork({file})")
    # Exercise the guest-facing File Manager too, bypassing Finder's preflight.
    pb = call("(void*)calloc(1,sizeof(HParamBlockRec))")
    name = call("(void*)_NewPtr_flags(256,1,1)")
    guest = name - call("ROMlib_offset")
    swapped = int.from_bytes(guest.to_bytes(4, "little"), "big")
    gdb.execute(f"set ((HParamBlockRec*){pb})->ioParam.ioNamePtr.pp={swapped}")
    swapped_ref = int.from_bytes((reference & 65535).to_bytes(2, "little"), "big")
    gdb.execute(f"set ((HParamBlockRec*){pb})->ioParam.ioVRefNum=(short){swapped_ref}")
    gdb.execute(f"set ((HParamBlockRec*){pb})->fileParam.ioDirID=33554432")
    gdb.selected_inferior().write_memory(name, b"\x08Document")
    gdb.execute(f"set ((HParamBlockRec*){pb})->ioParam.ioPermssn=1")
    check(f"PBOpenDF((HParamBlockRec*){pb},0)")
    raw_file = call(f"((HParamBlockRec*){pb})->ioParam.ioRefNum") & 65535
    file = int.from_bytes(raw_file.to_bytes(2, "little"), "big")
    if file >= 32768:
        file -= 65536
    check(f"ROMlib_finder_image_close_fork({file})")
    if not writable:
        gdb.selected_inferior().write_memory(name, b"\x09Forbidden")
        error = call(f"PBHCreate((HParamBlockRec*){pb},0)")
        if error not in (-44, -46):
            raise RuntimeError(f"Guest File Manager accepted locked create: {error}")
        check(f'ROMlib_finder_image_lookup({reference},2,"Forbidden",(unsigned int*){scratch})', -43)
    check(f'ROMlib_finder_image_delete({reference},2,"Document")', expected)
    if not writable:
        check(f'ROMlib_finder_image_lookup({reference},2,"Document",(unsigned int*){scratch})')
        check(f"*(unsigned int*){scratch}", document)
    check(f"ROMlib_finder_image_flush({reference})")
    check(f"ROMlib_finder_image_unmount({reference})")
    print("IMAGE_LOCK_PASS", flush=True)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=root / "build/image-desktop-fixture/DesktopMetadata.hfv")
    parser.add_argument("--romaw", type=Path, default=root / "windows-binaries/bin/ROMAW.exe")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="image-lock.", dir=root / "build"))
    print(f"Logs: {work}", flush=True)
    profile = work / "profile"
    (profile / ".romaw/Apps/System Folder").mkdir(parents=True)
    local = profile / "AppData/Local"
    local.mkdir(parents=True)
    (profile / ".romaw/ROMAW.prefs").write_text(
        "[HostDrives]\nSidecarDrives=none\nMacDirectoryCount=0\n", encoding="utf-8"
    )
    environment = dict(os.environ, USERPROFILE=str(profile), LOCALAPPDATA=str(local))
    for name, option, writable, host_ro, soft_lock in (
        ("writable.hfv", "-mount-rw", True, False, False),
        ("readonly.hfv", "-mount-ro", False, False, False),
        ("software-lock.hfv", "-mount-rw", False, False, True),
        ("host-readonly.hfv", "-mount-rw", False, True, False),
    ):
        image = work / name
        shutil.copyfile(args.image, image)
        before = hashlib.sha256(image.read_bytes()).digest()
        if host_ro:
            image.chmod(stat.S_IREAD)
        commands = work / f"{name}.gdb"
        commands.write_text(
            "set pagination off\nset confirm off\nset print thread-events off\n"
            "set environment _NO_DEBUG_HEAP 1\n"
            "break ROMlib_win_drive_preferences\ncommands\nsilent\nreturn 0\ncontinue\nend\n"
            "break ROMlib_ecf_restore_mounts\ncommands\nsilent\n"
            f'set preferences="{(work / (name + ".prefs")).as_posix()}"\ncontinue\nend\n'
            "break romaw_main\nrun\n"
            f"set $writable={int(writable)}\nset $soft_lock={int(soft_lock)}\n"
            f"source {Path(__file__).resolve().as_posix()}\nquit 0\n",
            encoding="utf-8",
        )
        try:
            result = subprocess.run(
                [
                    "C:/msys64/mingw64/bin/gdb.exe",
                    "-q",
                    "-batch",
                    "-x",
                    str(commands),
                    "--args",
                    str(args.romaw.resolve()),
                    option,
                    str(image),
                ],
                env=environment,
                cwd=work,
                capture_output=True,
                text=True,
                timeout=60,
            )
            output = result.stdout + result.stderr
            (work / f"{name}.log").write_text(output, encoding="utf-8")
            if result.returncode or "IMAGE_LOCK_PASS" not in output:
                raise RuntimeError(output)
            changed = hashlib.sha256(image.read_bytes()).digest() != before
            if changed != writable:
                raise RuntimeError(f"Unexpected image modification: writable={writable}, changed={changed}")
            print(f"PASS: {name}: lock snapshot, mutations, fork access and image integrity", flush=True)
        finally:
            image.chmod(stat.S_IREAD | stat.S_IWRITE)


if __name__ == "__main__":
    if "gdb" in sys.modules:
        probe()
    else:
        main()
