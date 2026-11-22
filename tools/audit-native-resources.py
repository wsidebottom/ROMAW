#!/usr/bin/env python3
# +===================================================================
#   File:      audit-native-resources.py
#
#   Summary:   Report and validate every executable entry in ROMAW's shipped resource catalogs.
#
#   Classes:   None.
#
#   Functions: main (script entry point).
#
#   Origin:    ROMAW System and Finder native resource dispatch registries.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/romaw"))

from system_resources import CODE_HANDLERS
from finder_guest_resources import FINDER_HANDLERS
from native_code_resources import EXECUTABLE_TYPES, audit_native_resources
from resource_catalog import load_scope


def main():
    handlers = CODE_HANDLERS | FINDER_HANDLERS
    for scope, directory in (("System", ROOT / "system/resources/system"), ("Finder", ROOT / "finder/resources")):
        catalog, _ = load_scope(directory)
        records = []
        for entry in catalog["resources"]:
            kind = bytes.fromhex(entry["type_hex"])
            if (kind in EXECUTABLE_TYPES or (kind, entry["id"]) == (b"INTL", 1)) and "payload" not in entry:
                raise ValueError(f"Executable resource {kind!r}/{entry['id']} lacks a generated native payload")
            records.append((kind, entry["id"], entry["attributes"], entry["name_hex"], entry.get("payload", b"")))
        entries = audit_native_resources(records, handlers, require_system=scope == "System")
        print(f"{scope}: {len(records)} resources; {len(entries)} validated native entry points")
        for kind, identity, offset, slot, target in entries:
            print(f"  {kind.decode('ascii')} {identity:+d} +{offset}: slot {slot} -> {target}")
    print("PASS: no unreviewed executable resource bodies; INTL 1's embedded hook is C-backed")
    print(
        "Compression dictionaries, opaque movp preferences, fixtures and external applications are not native code resources."
    )


if __name__ == "__main__":
    main()
