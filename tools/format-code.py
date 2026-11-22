#!/usr/bin/env python3
# +===================================================================
#   File:      format-code.py
#
#   Summary:   Apply repository formatting; --check reports drift without writing files.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Apply repository formatting; --check reports drift without writing files.

Run in WSL with clang-format 18.1.8, Black 24.10.0, shfmt 3.8.0 and
Perl::Tidy 20230309. PowerShell uses tools/format-powershell.ps1 separately.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

from file_headers import LAYOUT, ROOTS, maintain_headers

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    os.chdir(ROOT)
    header_count, header_changes = maintain_headers(check=args.check)
    print(
        f"{'Checked' if args.check else 'Updated'} {header_count} source file headers; "
        f"{len(header_changes)} {'missing or invalid' if args.check else 'changed'}."
    )
    if args.check and header_changes:
        print("\n".join(header_changes))
    files = (
        subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"])
        .decode()
        .split("\0")
    )
    changed = []
    count = 0
    for name in sorted(set(files)):
        path = Path(name)
        if not name or not path.is_file():
            continue
        if path.parts[0] not in ROOTS and name not in LAYOUT["root_build_files"]:
            continue  # Downloaded SDKs and other local references are not maintained source.
        if any(part in {"third_party", "build", "obj"} or part.startswith("%") for part in path.parts):
            continue
        suffix = path.suffix
        if suffix in {".c", ".h", ".cc", ".cpp", ".hpp", ".m"} or name.endswith(".h.in"):
            assumed = name.removesuffix(".in")
            command = [
                os.environ.get("CLANG_FORMAT", "clang-format"),
                "--style=file",
                f"--assume-filename={assumed}",
            ]
        elif suffix == ".py":
            command = [
                sys.executable,
                "-m",
                "black",
                "--quiet",
                "--stdin-filename",
                name,
                "-",
            ]
        elif suffix == ".sh":
            command = ["shfmt", "-i", "2", "-ci", "-bn", "-filename", name]
        elif suffix in {".pl", ".pm"}:
            command = ["perltidy", "-st", "-se", f"-pro={ROOT / '.perltidyrc'}"]
        else:
            continue
        original = path.read_bytes()
        if b"\0" in original:
            continue  # Macintosh resource forks and other binary assets.
        result = subprocess.run(
            command,
            input=original.replace(b"\r\n", b"\n").replace(b"\r", b"\n"),
            stdout=subprocess.PIPE,
            check=True,
        )
        formatted = result.stdout
        if name.endswith(".h.in"):
            # Autoconf substitutions are indivisible template tokens. Clang
            # otherwise inserts a space before their closing at-sign.
            formatted = re.sub(rb"@\s*([A-Za-z_]\w*)\s*@", rb"@\1@", formatted)
        count += 1
        if formatted != original:
            changed.append(name)
            if not args.check:
                temporary = path.with_name(path.name + ".format-tmp")
                temporary.write_bytes(formatted)
                temporary.chmod(path.stat().st_mode)
                temporary.replace(path)
    print(
        f"{'Checked' if args.check else 'Formatted'} {count} source files; {len(changed)} {'need formatting' if args.check else 'changed'}."
    )
    if args.check and (changed or header_changes):
        print("\n".join(changed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
