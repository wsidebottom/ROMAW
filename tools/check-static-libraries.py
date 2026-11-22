#!/usr/bin/env python3
# +===================================================================
#   File:      check-static-libraries.py
#
#   Summary:   Validate ROMAW static-library ownership, archive members and incremental dependencies.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Repository build validation for the static-library organization.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Check the configured build's real archives and source ownership."""

import argparse
from collections import Counter
import filecmp
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
COMPILED = {".c", ".cpp", ".cc", ".S", ".s", ".y"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--obj", type=Path, default=ROOT / "system/obj")
    parser.add_argument("--ar", default="x86_64-w64-mingw32-ar")
    parser.add_argument("--stage", type=Path, help="Also verify the complete staged runtime archive set")
    args = parser.parse_args()
    obj = args.obj.resolve()
    manifest = subprocess.check_output(
        ["make", "--no-print-directory", "-s", "-C", str(obj), "library-manifest"], text=True
    )
    owners = {}
    count = 0
    for line in manifest.splitlines():
        archive, _, sources = line.partition("\t")
        assert sources, f"Missing source list for {archive}"
        compiled = [Path(source) for source in sources.split() if Path(source).suffix in COMPILED]
        assert compiled, f"Empty build module: {archive}"
        for source in compiled:
            assert source not in owners, f"Duplicate source ownership: {source} in {archive} and {owners[source]}"
            owners[source] = archive
        if archive == "ROMAW.exe":
            assert compiled == [Path("frontend-sdl/winmain.c")], compiled
            continue
        actual = Counter(subprocess.check_output([args.ar, "t", str(obj / archive)], text=True).splitlines())
        expected = Counter(source.stem + ".o" for source in compiled)
        assert actual == expected, f"Archive membership differs: {archive}: {actual - expected}; {expected - actual}"
        # A single source edit must rebuild its member and owning archive.
        source = compiled[0]
        dry_run = subprocess.check_output(
            [
                "make",
                "--no-print-directory",
                "-s",
                "-n",
                "-C",
                str(obj),
                "-W",
                str(ROOT / source),
                archive,
            ],
            text=True,
        )
        assert str(source) in dry_run and archive in dry_run, f"Missing rebuild propagation: {source} -> {archive}"
        count += 1
    shared = obj / "classic-ui/libromaw-classic-ui.a"
    members = subprocess.check_output([args.ar, "t", str(shared)], text=True).splitlines()
    assert len(members) == 1 and members[0] in {"classic_ui.c.o", "classic_ui.c.obj"}, members
    finder = obj / "native-finder/windows-x64/libfinder.a"
    finder_members = subprocess.check_output([args.ar, "t", str(finder)], text=True).splitlines()
    assert not any("classic_ui" in member for member in finder_members), "Finder embeds a second shared renderer"
    owners[Path("classic-ui/src/classic_ui.c")] = str(shared)
    count += 1
    relink = subprocess.check_output(
        [
            "make",
            "--no-print-directory",
            "-s",
            "-n",
            "-C",
            str(obj),
            "-W",
            "libraries/libromaw-controls.a",
            "ROMAW.exe",
        ],
        text=True,
    )
    assert "-o ROMAW.exe" in relink, "Changing a subsystem archive did not relink the application"
    assert count >= 29, f"Unexpected module count: {count}"
    print(f"Static libraries: {count} archives, {len(owners)} uniquely owned compilation units and rebuilds passed")
    if args.stage:
        rows = [line.split("\t") for line in (args.stage / "manifest.tsv").read_text().splitlines()]
        expected = {Path(archive).name for archive in owners.values()}
        expected.discard("ROMAW.exe")
        expected.update(
            {
                "libfinder.a",
                "libsyn68k.a",
                "libsynppc.a",
                "libsynppc-romaw.a",
                "libromaw-carbon.a",
                "libromaw-images.a",
                "libromaw-finder-metadata.a",
            }
        )
        names = [row[0] for row in rows]
        assert len(names) == len(set(names)) and set(names) == expected, "Staged archive inventory differs"
        for name, source in rows:
            assert filecmp.cmp(args.stage / name, source, shallow=False), f"Staged archive is stale: {name}"
            assert subprocess.check_output([args.ar, "t", str(args.stage / name)], text=True).strip(), name
        print(f"Staged runtime libraries: {len(rows)} current, nonempty archives passed")


if __name__ == "__main__":
    main()
