#!/usr/bin/env python3
# +===================================================================
#   File:      jit-build-id-test.py
#
#   Summary:   Check persistent-cache identity stability and source, ABI and flag invalidation.
#
#   Classes:   None.
#
#   Functions: main
#
#   Origin:    ARDI Executor native cache build regression fixture.
#
#   Copyright and Legal notices: Applicable license: syn68k/COPYING.
# ===================================================================+

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    source = Path(__file__).resolve().parents[1] / "tools/jit-build-id.py"
    with tempfile.TemporaryDirectory(prefix="syn68k-build-id-") as temporary:
        root = Path(temporary) / "syn68k"
        shared = root.parent / "tools/native_code_image.py"
        shared.parent.mkdir()
        for directory in ("tools", "src/runtime", "third_party", "obj/src/include", "obj/src/runtime"):
            (root / directory).mkdir(parents=True)
        script = root / "tools/jit-build-id.py"
        shutil.copyfile(source, script)
        inputs = [
            shared,
            root / "tools/build-native040.py",
            root / "src/runtime/operation.c",
            root / "third_party/helper.c",
            root / "obj/src/include/config.h",
            root / "obj/src/include/syn68k_private.h",
            root / "obj/src/runtime/Makefile",
        ]
        for path in inputs:
            path.write_text("original\n")
        output = root / "obj/src/runtime/generated/jit-build-id.h"

        def generate(flags="-O0"):
            subprocess.run(
                [sys.executable, str(script), str(output), "gcc", flags], cwd=output.parent.parent, check=True
            )
            return output.read_bytes()

        identity = generate()
        timestamp = output.stat().st_mtime_ns
        assert generate() == identity and output.stat().st_mtime_ns == timestamp
        for path in inputs:
            path.write_text("changed\n")
            changed = generate()
            assert changed != identity
            identity = changed
        assert generate("-O2") != identity
    print("PASS: stable cache identity; source, helper, configuration and compiler-flag invalidation")


if __name__ == "__main__":
    main()
