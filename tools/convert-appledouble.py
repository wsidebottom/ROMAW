#!/usr/bin/env python3
# +===================================================================
#   File:      convert-appledouble.py
#
#   Summary:   Move Netatalk/percent AppleDouble files to OS X names without changing bytes.
#
#   Classes:   None declared.
#
#   Functions: validate, plan, convert
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Move Netatalk/percent AppleDouble files to OS X names without changing bytes."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def validate(data, path):
    if len(data) < 26 or data[:8] != bytes.fromhex("0005160700020000"):
        raise ValueError(f"Not AppleDouble version 2: {path}")
    count = struct.unpack_from(">H", data, 24)[0]
    end = 26 + count * 12
    if end > len(data):
        raise ValueError(f"Truncated descriptor table: {path}")
    ids = set()
    for i in range(count):
        entry, offset, size = struct.unpack_from(">III", data, 26 + i * 12)
        # ROMAW leaves empty resource forks at offset 512 even in short files.
        if entry in ids or offset < end or (size and offset + size > len(data)):
            raise ValueError(f"Invalid descriptor: {path}, entry {entry}")
        ids.add(entry)


def plan(root, replace_conflicts=False):
    moves = []
    targets = {}
    for source in sorted(root.rglob("*")):
        if not source.is_file():
            continue
        if source.parent.name == ".AppleDouble":
            parent = source.parent.parent
            if source.name == ".Parent":
                target = parent.parent / ("._" + parent.name)
            else:
                target = parent / ("._" + source.name)
        elif source.name.startswith("%"):
            if source.read_bytes()[:4] != bytes.fromhex("00051607"):
                continue
            target = source.with_name("._" + source.name[1:])
        else:
            continue
        data = source.read_bytes()
        validate(data, source)
        if target.exists() and target.read_bytes() != data and not replace_conflicts:
            raise ValueError(f"Conflicting existing target: {target}")
        if target in targets and targets[target] != data:
            raise ValueError(f"Conflicting source sidecars for: {target}")
        targets[target] = data
        moves.append((source, target, data))
    return moves


def convert(root, backup, replace_conflicts=False):
    root, backup = root.resolve(strict=True), backup.resolve()
    if not root.is_dir() or backup == root or root in backup.parents:
        raise ValueError("Backup must be outside the source directory")
    moves = plan(root, replace_conflicts)
    backup.mkdir(parents=True, exist_ok=False)
    manifest = []
    existing = {}
    # Complete and verify all backups before changing any source.
    for source, target, data in moves:
        saved = backup / source.relative_to(root)
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, saved)
        if saved.read_bytes() != data:
            raise OSError(f"Backup verification failed: {source}")
        if target.exists() and target.read_bytes() != data:
            original = target.read_bytes()
            saved_target = backup / "existing-targets" / target.relative_to(root.parent)
            saved_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved_target)
            if saved_target.read_bytes() != original:
                raise OSError(f"Target backup verification failed: {target}")
            existing[target] = original
        manifest.append(
            {
                "source": str(source),
                "target": str(target),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    (backup / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    # Exclusive creation prevents silently replacing a target created meanwhile.
    for source, target, data in moves:
        if target in existing:
            if target.read_bytes() != existing[target]:
                raise OSError(f"Target changed during conversion: {target}")
            # Backed up above; Netatalk has precedence in ROMAW's lookup.
            temporary = target.with_name(target.name + ".conversion-tmp")
            with temporary.open("xb") as output:
                output.write(data)
            temporary.replace(target)
            del existing[target]
        if not target.exists():
            with target.open("xb") as output:
                output.write(data)
            shutil.copystat(source, target)
        if target.read_bytes() != data or source.read_bytes() != data:
            raise OSError(f"Source/target changed during conversion: {source}")
    for source, target, data in moves:
        if target.read_bytes() != data or source.read_bytes() != data:
            raise OSError(f"Verification failed before source removal: {source}")
        source.unlink()
    for directory in sorted(root.rglob(".AppleDouble"), reverse=True):
        if directory.is_dir() and not any(directory.iterdir()):
            directory.rmdir()
    print(f"Converted {len(moves)} sidecars; verified backups: {backup}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument(
        "--replace-conflicts",
        action="store_true",
        help="back up conflicting OS X targets and prefer the source metadata",
    )
    args = parser.parse_args()
    convert(args.directory, args.backup, args.replace_conflicts)
