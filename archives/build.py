# +===================================================================
#   File:      build.py
#
#   Summary:   Compile the portable archive codec and pinned miniz in an object tree.
#
#   Classes:   None.
#
#   Functions: main.
#
#   Origin:    ROMAW archive package build coordinator.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+
"""Build the portable C++ archive package; Windows x64 is the default target."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--native", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    prefix = "" if args.native else "x86_64-w64-mingw32-"
    includes = [
        "-I" + str(root / path)
        for path in (
            "include",
            "archives/include",
            "archives/third_party/miniz",
            "stuffit/third_party/stuffit-rs/stuffit-ffi/include",
        )
    ]
    flags = ["-O2", "-g", "-DMINIZ_NO_STDIO", "-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES"]
    objects = []
    sources = [
        root / "archives/third_party/miniz" / (name + ".c")
        for name in ("miniz", "miniz_tinfl", "miniz_tdef", "miniz_zip")
    ]
    sources.append(root / "archives/src/archives.cpp")
    for source in sources:
        obj = out / (source.stem + ".o")
        compiler = prefix + ("g++" if source.suffix == ".cpp" else "gcc")
        standard = "-std=c++17" if source.suffix == ".cpp" else "-std=c17"
        subprocess.run([compiler, standard, *flags, *includes, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run([prefix + "ar", "rcs", str(out / "libromaw-archives.a"), *objects], check=True)


if __name__ == "__main__":
    main()
