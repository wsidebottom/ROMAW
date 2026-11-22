#!/usr/bin/env python3
# +===================================================================
#   File:      ppc-build-id.py
#
#   Summary:   Fingerprint native-code sources, compiler and configured ABI for persistent caches.
#
#   Classes:   None.
#
#   Functions: main
#
#   Origin:    ARDI Executor persistent PPC translation build support.
#
#   Copyright and Legal notices: Applicable license: synppc/COPYING.
# ===================================================================+

import hashlib
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[1])
    digest = hashlib.sha256()
    directories = ("src", "include", "platform", "third_party", "tools")
    files = sorted(
        path
        for directory in directories
        for path in (root / directory).rglob("*")
        if path.is_file() and "__pycache__" not in path.parts
    )
    files += [root / "Makefile", Path(__file__).resolve(), root.parent / "tools/native_code_image.py"]
    for path in files:
        digest.update(str(path.relative_to(root.parent)).encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
    digest.update(subprocess.check_output(shlex.split(sys.argv[2]) + ["--version"]))
    digest.update(sys.argv[3].encode())
    text = '#define SYN_PPC_AOT_BUILD_ID "' + digest.hexdigest() + '"\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text, newline="\n")


if __name__ == "__main__":
    main()
