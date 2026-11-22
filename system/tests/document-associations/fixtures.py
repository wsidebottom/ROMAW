# +===================================================================
#   File:      fixtures.py
#
#   Summary:   Build private association fixtures from original Retro68 test applications.
#
#   Classes:   None declared.
#
#   Functions: sidecar, main
#
#   Origin:    Existing repository source in executor/tests/document-associations; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Build private association fixtures from original Retro68 test applications."""

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from mac_resources import AppleDouble, ResourceFork

spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
BUILD = ROOT / "build/document-association-probe"
OUTPUT = ROOT / "build/document-association-check"


def sidecar(info, fork):
    info = info.ljust(32, b"\0")[:32]
    return (
        struct.pack(">II16sH", 0x00051607, 0x00020000, b"\0" * 16, 2)
        + struct.pack(">IIIIII", 9, 50, 32, 2, 82, len(fork))
        + info
        + fork
    )


def main():
    apps = OUTPUT / "Apps"
    (apps / "System Folder").mkdir(parents=True, exist_ok=True)
    for app, dest in (("DocumentProbe", OUTPUT), ("DocumentTarget", apps)):
        original = AppleDouble((BUILD / f"%{app}.ad").read_bytes())
        fork = original.get(2)
        if app == "DocumentTarget":
            parsed = ResourceFork(fork)
            replace = {b"BNDL", b"FREF", b"ICN#"}
            records = [
                (kind, rid, attrs, None, data)
                for (kind, rid), (data, attrs, _) in parsed.records.items()
                if kind not in replace
            ]
            bundle = struct.pack(">4sHH4sHHHHH", b"fAst", 0, 0, b"ICN#", 1, 0, 128, 1, 129)
            records.extend(
                [
                    (b"BNDL", 128, 0, None, bundle),
                    (b"FREF", 128, 0, None, struct.pack(">4sHB", b"APPL", 0, 0)),
                    (b"FREF", 129, 0, None, struct.pack(">4sHB", b"TEST", 1, 0)),
                    (b"ICN#", 128, 0, None, b"\xaa" * 128 + b"\xff" * 128),
                    (b"ICN#", 129, 0, None, b"\x55" * 128 + b"\xff" * 128),
                ]
            )
            fork = builder.build_fork(records)
        (dest / app).write_bytes((BUILD / f"{app}.ad").read_bytes())
        (dest / f"._{app}").write_bytes(sidecar(original.get(9), fork))
    (apps / "Sample.ftst").write_bytes(b"mapped data")
    (apps / "Legacy.ftst").write_bytes(b"legacy neutral metadata")
    (apps / "._Legacy.ftst").write_bytes(sidecar(b"TEXTUNIX", b""))
    (apps / "Forked.ftst").write_bytes(b"new resource fork")
    (apps / "._Forked.ftst").unlink(missing_ok=True)
    (apps / "Explicit.ftst").write_bytes(b"explicit metadata")
    (apps / "._Explicit.ftst").write_bytes(sidecar(b"EXPLownr", b""))
    mapping = (ROOT / "system/ROMAW.ftm").read_bytes()
    (OUTPUT / "ROMAW.ftm").write_bytes(mapping + b"\nFTST,,TEST,fAst,x,x,Test document\n")
    print("Prepared document mapping, explicit-metadata and BNDL/FREF icon fixtures")


if __name__ == "__main__":
    main()
