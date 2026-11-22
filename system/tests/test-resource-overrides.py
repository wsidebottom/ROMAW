# +===================================================================
#   File:      test-resource-overrides.py
#
#   Summary:   Independently parse the C serializer's ResEdit resource fork.
#
#   Classes:   None declared.
#
#   Functions: No public module functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Independently parse the C serializer's ResEdit resource fork."""

import importlib.util
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
from mac_resources import ResourceFork

spec = importlib.util.spec_from_file_location("system_resources", root / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
fork = ResourceFork(Path(sys.argv[1]).read_bytes())
records = builder.read_resources(root / "system/resources/system")
assert len(fork.records) == len(records)
for kind, rid, attrs, name, payload in records:
    actual, flags, reference = fork.records[kind, rid]
    assert actual == payload and flags == (attrs & ~2)
print(f"Independent resource-fork parser verified all {len(records)} resources")
