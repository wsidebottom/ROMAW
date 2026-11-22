# +===================================================================
#   File:      check-softfloat-source.py
#
#   Summary:   Check the immutable official SoftFloat 3e subset before CPU tests.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

"""Check the immutable official SoftFloat 3e subset before CPU tests."""

import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "third_party/softfloat3"
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
for relative, expected in manifest["files"].items():
    source = (root / relative).resolve()
    if not source.is_relative_to(root) or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise SystemExit(f"Modified SoftFloat upstream source: {relative}")
print(f"Verified {len(manifest['files'])} official SoftFloat source/header/notice files")
