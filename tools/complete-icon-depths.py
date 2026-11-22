#!/usr/bin/env python3
# +===================================================================
#   File:      complete-icon-depths.py
#
#   Summary:   Add missing colour depths or 48-pixel huge members to shared ICNS files.
#
#   Classes:   None declared.
#
#   Functions: main
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Add missing colour depths or 48-pixel huge members to shared ICNS files."""

import argparse
from pathlib import Path

import mac_icons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bitmaps", type=Path, nargs="*")
    parser.add_argument("--huge", action="store_true", help="Add missing ich#, ich4 and ich8 members at 48 pixels")
    args = parser.parse_args()
    artwork = Path(__file__).resolve().parents[1] / "bitmaps"
    paths = args.bitmaps or sorted(artwork.rglob("*.icns"))
    no_huge = set((artwork / "no-huge-icons.txt").read_text().splitlines())
    changed = 0
    for path in paths:
        original = path.read_bytes()
        if path.name in no_huge:
            completed = mac_icons.without_huge(original)
            if not args.huge:
                completed = mac_icons.complete_depths(completed)
        else:
            completed = mac_icons.complete_huge(original) if args.huge else mac_icons.complete_depths(original)
        if completed != original:
            path.write_bytes(completed)
            changed += 1
    print(f"Checked {len(paths)} icon families; updated {changed}")


if __name__ == "__main__":
    main()
