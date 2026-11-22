#!/usr/bin/env python3
# +===================================================================
#   File:      image_readonly_open_test.py
#
#   Summary:   Verify incomplete read-only mounts need no decision and Get Info reads bounded version metadata.
#
#   Classes:   None.
#
#   Functions: main.
#
#   Origin:    Executor direct-image regression using the user's test media without modifying it.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+

from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    work = Path(tempfile.mkdtemp(prefix="image-readonly-open.", dir=root / "build"))
    recovery = Path("D:/Temp/08.99_System_Recovery_cd1.iso")
    tools = Path("D:/Temp/Disk Tools.image")
    for index, (image, option) in enumerate(
        [(recovery, "-mount"), (recovery, "-mount-ro"), (recovery, "-mount-rw"), (tools, "-mount-ro")]
    ):
        before = image.stat()
        version = ""
        if image == tools:
            version = """
set $bytes=(unsigned char*)calloc(1,1024)
set $size=(size_t*)calloc(1,sizeof(size_t))
set $map=(short)C_CurResFile()
set $error=(short)C_ResError()
set $load=*(unsigned char*)(ROMlib_offset+0xa5e)
if ROMlib_finder_image_version($volume->reference,2,"Disk First Aid",$bytes,512,$size) != 1 || *$size < 7 || $bytes[6] == 0 || $bytes[6] > *$size-7
quit 1
end
if (short)C_CurResFile() != $map || (short)C_ResError() != $error || *(unsigned char*)(ROMlib_offset+0xa5e) != $load
quit 1
end
if ROMlib_finder_image_version($volume->reference,2,"Disk First Aid",$bytes,4,$size) != 0 || *$size != 0
quit 1
end
if (short)C_CurResFile() != $map || (short)C_ResError() != $error || *(unsigned char*)(ROMlib_offset+0xa5e) != $load
quit 1
end
printf "GET_INFO_VERSION_PASS\\n"
"""
        script = f"""
set pagination off
set confirm off
set print thread-events off
set environment _NO_DEBUG_HEAP 1
break host_image_confirm_incomplete
commands
silent
printf "UNEXPECTED_IMAGE_DECISION\\n"
quit 1
end
break ROMlib_ecf_restore_mounts
commands
silent
set preferences="{(work / f'{index}.prefs').as_posix()}"
continue
end
break romaw_main
run
set $volume=(FinderImageVolume*)calloc(1,sizeof(FinderImageVolume))
if ROMlib_finder_image_volume(0,$volume) != 1 || !$volume->read_only
quit 1
end
{version}
if ROMlib_finder_image_unmount($volume->reference) != 0
quit 1
end
printf "READONLY_OPEN_PASS\\n"
quit 0
"""
        commands = work / f"{index}.gdb"
        commands.write_text(script, encoding="utf-8")
        result = subprocess.run(
            [
                "C:/msys64/mingw64/bin/gdb.exe",
                "-q",
                "-batch",
                "-x",
                str(commands),
                "--args",
                str(root / "windows-binaries/bin/ROMAW.exe"),
                option,
                str(image),
            ],
            cwd=work,
            capture_output=True,
            text=True,
            timeout=60,
        )
        output = result.stdout + result.stderr
        (work / f"{index}.log").write_text(output, encoding="utf-8")
        assert result.returncode == 0 and "READONLY_OPEN_PASS" in output, output
        assert "UNEXPECTED_IMAGE_DECISION" not in output, output
        if version:
            assert "GET_INFO_VERSION_PASS" in output, output
        after = image.stat()
        assert (after.st_size, after.st_mtime_ns) == (before.st_size, before.st_mtime_ns)
        print(f"PASS: {image.name} {option}, read-only and unchanged, no incomplete-image decision")


if __name__ == "__main__":
    main()
