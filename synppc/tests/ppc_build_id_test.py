#!/usr/bin/env python3
# +===================================================================
#   File:      ppc_build_id_test.py
#
#   Summary:   Verify PPC AOT identity stability and invalidation of helper, ABI and compiler-flag changes.
#
#   Classes:   None.
#
#   Functions: main
#
#   Origin:    ARDI Executor PPC cache build regression fixture.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    generator = Path(__file__).resolve().parents[1] / "tools/ppc-build-id.py"
    with tempfile.TemporaryDirectory(prefix="ppc-build-id-") as directory:
        root = Path(directory) / "synppc"
        root.mkdir()
        shared = root.parent / "tools/native_code_image.py"
        shared.parent.mkdir()
        shared.write_text("original verifier\n", encoding="utf-8")
        for name in ("tools", "src", "include", "platform", "third_party"):
            (root / name).mkdir()
        script = root / "tools/ppc-build-id.py"
        shutil.copyfile(generator, script)
        inputs = [
            shared,
            root / "Makefile",
            *(root / name / "input" for name in ("src", "include", "platform", "third_party")),
        ]
        for path in inputs:
            path.write_text("original\n", encoding="utf-8")
        output = root / "obj/generated/ppc-jit-build-id.h"

        def generate(flags="-O2"):
            subprocess.run([sys.executable, str(script), str(output), "g++", flags], check=True)
            return output.read_bytes()

        identity = generate()
        assert identity.startswith(b'#define SYN_PPC_AOT_BUILD_ID "')
        timestamp = output.stat().st_mtime_ns
        assert generate() == identity and timestamp == output.stat().st_mtime_ns
        for path in inputs:
            path.write_text("changed\n", encoding="utf-8")
            updated = generate()
            assert updated != identity
            identity = updated
        assert generate("-O0 -fsanitize=undefined") != identity
    print("PPC AOT source/helper/ABI/platform/build-flag identity checks passed")


if __name__ == "__main__":
    main()
