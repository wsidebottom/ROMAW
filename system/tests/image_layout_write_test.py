#!/usr/bin/env python3
# +===================================================================
#   File:      image_layout_write_test.py
#
#   Summary:   Verify mounted Finder layout writes, metadata preservation and effective image access policy.
#
#   Classes:   None.
#
#   Functions: image_digest; probe; main.
#
#   Origin:    Executor mounted-image layout regression fixture.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

import hashlib
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


def image_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").digest()


def probe():
    import gdb

    def call(expression):
        return int(gdb.parse_and_eval(expression))

    def check(expression, expected=0):
        result = call(expression)
        if result != expected:
            raise RuntimeError(f"{expression}: {result} != {expected}")

    inferior = gdb.selected_inferior()
    volume = call("(void*)calloc(1,sizeof(FinderImageVolume))")
    entry = call("(void*)calloc(1,sizeof(FinderImageEntry))")
    scratch = call("(void*)calloc(1,1024)")
    check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
    reference = call(f"((FinderImageVolume*){volume})->reference")
    source = gdb.parse_and_eval(f"((FinderImageVolume*){volume})->source").string()
    writable = call("$layout_writable") != 0
    expected = 0 if writable else -44
    check(f"ROMlib_finder_image_writable({reference})", expected)
    check(f'ROMlib_finder_image_lookup({reference},2,"Tools",(unsigned int*){scratch})')
    tools = call(f"*(unsigned int*){scratch}")

    def snapshot(directory, name=None):
        if name:
            index = 1
            while True:
                check(f"ROMlib_finder_image_entry({reference},{directory},{index},(FinderImageEntry*){entry})")
                if gdb.parse_and_eval(f"((FinderImageEntry*){entry})->name").string() == name:
                    break
                index += 1
        else:
            check(f"ROMlib_finder_image_entry({reference},{directory},-1,(FinderImageEntry*){entry})")
        info = call(f"(void*)&((FinderImageEntry*){entry})->finder_info")
        rest = tuple(
            call(f"((FinderImageEntry*){entry})->{field}")
            for field in ("id", "parent", "modified", "data_size", "resource_size")
        )
        return bytes(inferior.read_memory(info, 32)), rest

    if writable:
        for name in ("Document", "Tools"):
            info, _ = snapshot(2, name)
            seeded = bytearray(info[:24] + bytes((0x12, 0x34, 0x56, 0x78)) + info[28:])
            if name == "Tools":
                seeded[15] |= 0x25  # Low-byte non-view Finder state must survive.
            inferior.write_memory(scratch, seeded)
            check(f'ROMlib_finder_image_set_info({reference},2,"{name}",(unsigned char*){scratch})')
            assert snapshot(2, name)[0] == seeded, "Extended FinderInfo was not preserved"

    print("Snapshot before layout writes", flush=True)
    before = [snapshot(2, "Document"), snapshot(tools), snapshot(2)]
    check(f'ROMlib_finder_image_info({reference},2,"Document",(FinderImageEntry*){entry})')
    document = call(f"((FinderImageEntry*){entry})->id")
    created = call(f"((FinderImageEntry*){entry})->created")
    check(f'ROMlib_finder_image_lock({reference},2,"Document",{document},1)', expected)
    check(f'ROMlib_finder_image_info({reference},2,"Document",(FinderImageEntry*){entry})')
    check(f"((FinderImageEntry*){entry})->locked", int(writable))
    check(f"((FinderImageEntry*){entry})->created", created)
    check(f'ROMlib_finder_image_lock({reference},2,"Document",{document},0)', expected)
    check(f'ROMlib_finder_image_lock({reference},2,"Document",{document+1},1)', -43 if writable else -44)

    check(f'ROMlib_finder_image_position({reference},2,"Document",-16,-8)', expected)
    check(f'ROMlib_finder_image_position({reference},2,"Tools",146,92)', expected)
    check(f"ROMlib_finder_image_window({reference},2,110,96,554,322)", expected)
    check(f"ROMlib_finder_image_window({reference},{tools},180,130,460,300)", expected)
    for selector in range(1, 9):
        check(f"ROMlib_finder_image_view({reference},2,{selector << 8})", expected)
    check(f"ROMlib_finder_image_view({reference},2,0x200)", expected)
    check(f"ROMlib_finder_image_view({reference},{tools},0x140)", expected)
    if writable:
        check(f"ROMlib_finder_image_view({reference},2,0xffff)", -50)
        check(f'ROMlib_finder_image_position({reference},2,"Document",32768,0)', -50)
        check(f"ROMlib_finder_image_window({reference},2,0,0,99,100)", -50)
    print("Snapshot after layout writes", flush=True)
    after = [snapshot(2, "Document"), snapshot(tools), snapshot(2)]

    def changed(info, *, position=None, bounds=None, view=None):
        data = bytearray(info)
        if position:
            flags = int.from_bytes(data[8:10], "big") | 0x100
            data[8:10] = flags.to_bytes(2, "big")
            x, y = position
            data[10:12] = y.to_bytes(2, "big", signed=True)
            data[12:14] = x.to_bytes(2, "big", signed=True)
        if bounds:
            left, top, right, bottom = bounds
            data[:8] = b"".join(n.to_bytes(2, "big", signed=True) for n in (top, left, bottom, right))
        if view is not None:
            previous = int.from_bytes(data[14:16], "big")
            data[14:16] = ((previous & 0xBF) | view).to_bytes(2, "big")
        return bytes(data)

    expected_info = [
        changed(before[0][0], position=(-16, -8)),
        changed(before[1][0], position=(146, 92), bounds=(180, 130, 460, 300), view=0x140),
        changed(before[2][0], bounds=(110, 96, 554, 322), view=0x200),
    ]
    for index in range(3):
        assert after[index][0] == (expected_info[index] if writable else before[index][0])
        assert after[index][1] == before[index][1], "Layout changed file identity, dates or fork sizes"
    check(f"ROMlib_finder_image_flush({reference})")
    check(f"ROMlib_finder_image_unmount({reference})")
    escaped = source.replace("\\", "/")
    check(f'ROMlib_mount_image_mode("{escaped}",{1 if writable else -1})')
    check(f"ROMlib_finder_image_volume(0,(FinderImageVolume*){volume})", 1)
    reference = call(f"((FinderImageVolume*){volume})->reference")
    print("Snapshot after remount", flush=True)
    assert [snapshot(2, "Document"), snapshot(tools), snapshot(2)] == after
    check(f"ROMlib_finder_image_unmount({reference})")
    print("IMAGE_LAYOUT_WRITE_PASS")


def main():
    root = Path(__file__).resolve().parents[2]
    image = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/image-layout-fixture/LayoutMetadata.hfv"
    executable = root / "windows-binaries/bin/ROMAW.exe"
    gdb = Path("C:/msys64/mingw64/bin/gdb.exe")
    work = Path(tempfile.mkdtemp(prefix="image-layout-write.", dir=root / "build"))
    for name, option, writable, host_ro in (
        ("normal.hfv", "-mount", True, False),
        ("forced.hfv", "-mount-ro", False, False),
        ("default.toast", "-mount", False, False),
        ("default.iso", "-mount", False, False),
        ("default.cdr", "-mount", False, False),
        ("explicit.toast", "-mount-rw", True, False),
        ("explicit.iso", "-mount-rw", True, False),
        ("explicit.cdr", "-mount-rw", True, False),
        ("host-readonly.toast", "-mount-rw", False, True),
    ):
        target = work / name
        shutil.copyfile(image, target)
        before = image_digest(target)
        if host_ro:
            target.chmod(stat.S_IREAD)
        commands = work / f"{name}.gdb"
        commands.write_text(
            "set pagination off\nset confirm off\nset print thread-events off\n"
            "set environment _NO_DEBUG_HEAP 1\nbreak ROMlib_ecf_restore_mounts\ncommands\nsilent\n"
            f'set preferences="{(work / (name + ".prefs")).as_posix()}"\ncontinue\nend\n'
            "break romaw_main\nrun\n"
            f"set $layout_writable={int(writable)}\n"
            f"source {Path(__file__).resolve().as_posix()}\nquit 0\n",
            encoding="utf-8",
        )
        try:
            result = subprocess.run(
                [str(gdb), "-q", "-batch", "-x", str(commands), "--args", str(executable), option, str(target)],
                cwd=work,
                capture_output=True,
                text=True,
                timeout=60,
            )
            output = result.stdout + result.stderr
            (work / f"{name}.log").write_text(output, encoding="utf-8")
            assert result.returncode == 0 and "IMAGE_LAYOUT_WRITE_PASS" in output, output
            assert (image_digest(target) != before) == writable
            print(f"PASS: {name} ({option}), layout persistence and preserved metadata; writable={writable}")
        finally:
            target.chmod(stat.S_IREAD | stat.S_IWRITE)


if __name__ == "__main__":
    if "gdb" in sys.modules:
        probe()
    else:
        main()
