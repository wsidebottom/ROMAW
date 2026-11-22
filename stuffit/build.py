# +===================================================================
#   File:      build.py
#
#   Summary:   Build the pinned StuffIt C ABI for native or Windows C++ consumers.
#
#   Classes:   None.
#
#   Functions: main.
#
#   Origin:    ROMAW build coordination for the vendored stuffit-rs dependency.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Build StuffIt in an explicit object directory; never generate in source."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--native", action="store_true", help="Build for the current host instead of Windows x64")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cargo = shutil.which("cargo") or str(Path.home() / ".cargo/bin/cargo")
    command = [
        cargo,
        "build",
        "--locked",
        "--release",
        "--manifest-path",
        str(root / "third_party/stuffit-rs/Cargo.toml"),
        "--target-dir",
        str(out / "cargo"),
        "-p",
        "stuffit-ffi",
    ]
    env = os.environ.copy()
    target = "x86_64-pc-windows-gnu"
    if not args.native:
        command += ["--target", target]
        env.setdefault("CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER", "x86_64-w64-mingw32-gcc")
    subprocess.run(command, check=True, env=env)
    library = out / "cargo" / ("" if args.native else target) / "release/libstuffit_ffi.a"
    shutil.copyfile(library, out / "libstuffit_ffi.a")


if __name__ == "__main__":
    main()
