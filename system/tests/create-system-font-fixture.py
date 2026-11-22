# +===================================================================
#   File:      create-system-font-fixture.py
#
#   Summary:   Create a disposable font suitcase and an unrelated non-font resource.
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

"""Create a disposable font suitcase and an unrelated non-font resource."""

import importlib.util
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("system_resources", root / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
folder = Path(sys.argv[1]) / "Fonts"
folder.mkdir(parents=True, exist_ok=True)
fork = builder.build_fork(
    [
        (b"FONT", 30000, 0, b"Font fixture", b"Fixture payload"),
        (b"Tst!", 30001, 0, None, b"Not a font"),
    ]
)
(folder / "FontFixture").write_bytes(b"")
(folder / "._FontFixture").write_bytes(builder.apple_double(fork))
