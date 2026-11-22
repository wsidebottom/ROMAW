# +===================================================================
#   File:      create-launch-desk-accessory-probe.py
#
#   Summary:   Create original DRVR fixtures and a private profile for OSDispatch tests.
#
#   Classes:   None declared.
#
#   Functions: driver, sidecar, main
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Create original DRVR fixtures and a private profile for OSDispatch tests."""

import importlib.util
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from mac_resources import AppleDouble, ResourceFork

spec = importlib.util.spec_from_file_location("system_resources", ROOT / "tools/romaw/system_resources.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def driver(name, beep=None):
    code = bytearray(48)
    struct.pack_into(">5H", code, 8, 32, 44, 44, 44, 44)
    code[18 : 19 + len(name)] = bytes([len(name)]) + name
    opening = bytes.fromhex("70004e75")
    if beep is not None:
        opening = struct.pack(">HHH", 0x3F3C, beep, 0xA9C8) + opening
    code[32 : 32 + len(opening)] = opening
    code[44:] = bytes.fromhex("70004e75")
    return bytes(code)


def sidecar(records, info):
    encoded = bytearray(builder.apple_double(builder.build_fork(records)))
    encoded[50:82] = info[:32].ljust(32, b"\0")
    return encoded


def main():
    folder = Path(sys.argv[1]).resolve()
    apps = folder / ".romaw/Apps"
    apps.mkdir(parents=True, exist_ok=True)
    (apps / "System Folder").mkdir(exist_ok=True)
    (folder / ".romaw/ROMAW.prefs").write_text("[HostDrives]\nSidecarDrives=none\n")
    original = ROOT / "system/obj/tests/startup-display"
    data = (original / "StartupDisplayProbe.ad").read_bytes()
    double = AppleDouble((original / "%StartupDisplayProbe.ad").read_bytes())
    fork = ResourceFork(double.get(2))
    names = struct.unpack_from(">H", fork.map, 26)[0]
    records = []
    for (kind, rid), (payload, flags, reference) in fork.records.items():
        name_offset = struct.unpack_from(">H", fork.map, reference + 2)[0]
        name = None
        if name_offset != 0xFFFF:
            start = names + name_offset
            name = fork.map[start + 1 : start + 1 + fork.map[start]]
        records.append((kind, rid, flags, name, payload))
    records.append((b"DRVR", 17, 0, b".SelectorA", driver(b".SelectorA")))
    (apps / "Probe").write_bytes(data)
    (apps / "._Probe").write_bytes(sidecar(records, double.get(9)))
    accessories = [
        (b"DRVR", 17, 0, b".SelectorA", driver(b".SelectorA", 778)),
        (b"DRVR", 18, 0, b".SelectorB", driver(b".SelectorB", 777)),
    ]
    (apps / "Accessory").write_bytes(b"")
    (apps / "._Accessory").write_bytes(sidecar(accessories, b"dfilTEST"))
    script = ROOT / "system/tests/launch_desk_accessory_probe.py"
    root = folder.as_posix()
    common = f"""set pagination off
set confirm off
set print thread-events off
set environment USERPROFILE {root}
break ROMlib_win_drive_preferences
commands
silent
quit 3
end
"""
    parent = (
        common
        + f"""break C_ExitToShell
run "{root}/.romaw/Apps/Probe"
delete breakpoints
set $fixture_path="{root}/.romaw/Apps/Accessory"
source {script.as_posix()}
break ROMlib_win_relaunch
python
try:
 gdb.parse_and_eval('(int)C_LaunchDeskAccessory((FSSpec *)$accessory_file,(StringPtr)$accessory_name)')
except gdb.error:
 pass
assert gdb.newest_frame().name()=='ROMlib_win_relaunch'
count=int(gdb.parse_and_eval('count'))
argv=gdb.parse_and_eval('arguments')
args=[argv[i].string() for i in range(count)]
assert count==6 and args[1:5]==['-system','7.1.1','-desk-resource','18'],args
assert args[-1].replace(chr(92),'/').endswith('/Accessory'),args
print('LAUNCH_DESK_ACCESSORY_NAMED_CHILD_OK')
end
quit 0
"""
    )
    (folder / "parent.gdb").write_bytes(parent.encode())
    child = (
        common
        + f"""break C_SysBeep
commands
silent
if i != 777
quit 2
end
printf "LAUNCH_DESK_ACCESSORY_SELECTED_DRIVER_OK\\n"
quit 0
end
run -desk-resource 18 "{root}/.romaw/Apps/Accessory"
quit 2
"""
    )
    (folder / "child.gdb").write_bytes(child.encode())


if __name__ == "__main__":
    main()
