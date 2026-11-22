# +===================================================================
#   File:      drive_prefs_probe.py
#
#   Summary:   Run inside Windows GDB with an isolated host-volume fixture.
#
#   Classes:   None declared.
#
#   Functions: value, write, read, mount, params, desktop, rename, volume_name, open_wd, wd_info
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Run inside Windows GDB with an isolated host-volume fixture."""

import gdb
import struct
from pathlib import Path


def value(expression):
    return int(gdb.parse_and_eval(expression))


memory = gdb.selected_inferior()
offset = value("ROMlib_offset")
base = value("_NewPtr_flags(8192,0,1)")
assert base
root = Path(FIXTURE)
apps = root / "apps"
dos = root / "dos"
apps.mkdir()
dos.mkdir()
prefs = (root / "ROMAW.prefs").as_posix()
assert value(f'config_drive_prefs_load("{prefs}","{apps.as_posix()}")') == 0


def write(address, data):
    memory.write_memory(address, bytes(data))


def read(address, size):
    return bytes(memory.read_memory(address, size))


def mount(path, number):
    write(base + 2048, path.as_posix().encode() + b"\0")
    saved = value("ROMlib_volumename")
    gdb.execute(f"set ROMlib_volumename=(char *){base+2048}")
    pb = bytearray(128)
    struct.pack_into(">h", pb, 22, number)
    write(base, pb)
    assert value(f"(int)ufsPBMountVol((void *){base})") == 0
    gdb.execute(f"set ROMlib_volumename=(char *){saved}")
    return struct.unpack(">h", read(base + 22, 2))[0]


def params(vref):
    pb = bytearray(128)
    struct.pack_into(">h", pb, 22, vref)
    struct.pack_into(">II", pb, 32, base + 1024 - offset, 20)
    write(base, pb)
    write(base + 1024, bytes(20))
    assert value(f"(int)PBHGetVolParms((void *){base},0)") == 0
    return struct.unpack(">I", read(base + 1026, 4))[0]


def desktop(vref, expected):
    pb = bytearray(128)
    struct.pack_into(">h", pb, 22, vref)
    write(base, pb)
    assert value(f"(int)PBDTOpenInform((void *){base})") == expected


dos_ref = mount(dos, 126)
apps_ref = mount(apps, 127)
desktop_bit = 1 << value("bHasDesktopMgr")
no_items_bit = 1 << value("bNoDeskItems")
assert params(dos_ref) & no_items_bit
assert not params(dos_ref) & desktop_bit
assert params(apps_ref) & desktop_bit
assert not params(apps_ref) & no_items_bit
desktop(dos_ref, -58)
assert not list(dos.iterdir())

# Even the legacy Desktop resource-file fallback cannot create a DOS desktop.
for name in (b"Desktop", b"Desktop DB", b"Desktop DF"):
    write(base + 2048, bytes([len(name)]) + name)
    pb = bytearray(128)
    struct.pack_into(">I", pb, 18, base + 2048 - offset)
    struct.pack_into(">h", pb, 22, dos_ref)
    struct.pack_into(">I", pb, 48, 2)
    write(base, pb)
    assert value(f"(int)ufsPBHCreate((void *){base},0)") == -44
    existing = dos / name.decode()
    existing.write_bytes(b"preserve existing DOS desktop")
    write(base, pb)
    assert value(f"(int)ufsPBHDelete((void *){base},0)") == -44
    assert existing.read_bytes() == b"preserve existing DOS desktop"
    existing.unlink()
assert not list(dos.iterdir())
desktop(apps_ref, 0)
assert (apps / "Desktop DB").is_file()

# A folder can be compatible without enabling its containing drive.
folder = dos / "Mac Folder"
folder.mkdir()
folder_path = folder.as_posix()
write(base + 3000, folder_path.encode() + b"\0")
gdb.execute(f"set *(char **){base+3500}=(char *){base+3000}")
assert value(f'config_drive_prefs_save_all("{prefs}",0,(char **){base+3500},1)') == 1
gdb.execute("call ROMlib_mount_compatible_directories()")
folder_vcb = value(f'ROMlib_vcbbyunixname("{folder_path}")')
assert folder_vcb
folder_ref = struct.unpack(">h", read(value(f"&((VCBExtra *){folder_vcb})->vcb.vcbVRefNum"), 2))[0]
assert params(folder_ref) & desktop_bit
assert not params(dos_ref) & desktop_bit
desktop(folder_ref, 0)
assert (folder / "Desktop DB").is_file()


# Finder's root FSSpec rename changes the VCB label and prefs, never the host path.
def rename(vref, old, new, expected=0):
    write(base + 2048, bytes([len(old)]) + old)
    write(base + 2304, bytes([len(new)]) + new)
    pb = bytearray(128)
    struct.pack_into(">I", pb, 18, base + 2048 - offset)
    struct.pack_into(">h", pb, 22, vref)
    struct.pack_into(">I", pb, 28, base + 2304 - offset)
    struct.pack_into(">I", pb, 48, 1)
    write(base, pb)
    result = value(f"(int)ufsPBHRename((void *){base},0)")
    assert result == expected, (old, new, result)


def volume_name(vref):
    address = value(f"&((VCBExtra *)ROMlib_vcbbyvrn({vref}))->vcb.vcbVN")
    return read(address + 1, read(address, 1)[0])


rename(dos_ref, volume_name(dos_ref), b"Host Games")
assert volume_name(dos_ref) == b"Host Games"
assert dos.is_dir()
assert not (root / "Host Games").exists()
assert (folder / "Desktop DB").is_file()
assert "Host Games" in Path(prefs).read_text()
rename(dos_ref, b"Host Games", volume_name(apps_ref), -48)
rename(dos_ref, b"Host Games", b"bad:name", -37)
rename(dos_ref, b"Host Games", b"Caf\x8e")
assert "Caf\u00e9" in Path(prefs).read_text(encoding="utf-8")
assert value(f'config_drive_prefs_load("{prefs}","{apps.as_posix()}")') == 1
vcb = value(f"ROMlib_vcbbyvrn({dos_ref})")
gdb.execute(f"set ((VCBExtra *){vcb})->vcb.vcbVN[0]=0")
gdb.execute(f"call ROMlib_restore_host_volume_name((VCBExtra *){vcb})")
assert volume_name(dos_ref) == b"Caf\x8e"

# Letter aliases keep resolving even after the Windows drive is relabelled.
letter = dos.drive[0].encode()
write(base + 2048, letter + b"\0")
drive_vcb = value(f"vlookupbyname((char *){base+2048},(char *){base+2049})")
assert drive_vcb
write(base + 2304, b"\x0fTest Host Drive")
assert value(f"(int)ROMlib_rename_host_volume((void *){drive_vcb},(void *){base+2304})") == 0
assert value(f"vlookupbyname((char *){base+2048},(char *){base+2049})") == drive_vcb
write(base + 2048, letter + b"/\0")
assert value(f"vlookupbyname((char *){base+2048},(char *){base+2050})") == drive_vcb

mask = 1 << (ord(dos.drive[0].upper()) - ord("A"))
assert value(f'config_drive_prefs_save("{prefs}",{mask})') == 1
assert params(dos_ref) & desktop_bit
assert not params(dos_ref) & no_items_bit
desktop(dos_ref, 0)
assert (dos / "Desktop DB").is_file()


def open_wd(vref, directory, owner):
    vcb = value(f"ROMlib_vcbbyvrn({vref})")
    assert vcb
    write(base, bytes(128))
    owner_id = int.from_bytes(owner, "big")
    assert value(f"(int)ROMlib_mkwd((void *){base},(void *){vcb},{directory},{owner_id})") == 0
    return struct.unpack(">h", read(base + 22, 2))[0]


def wd_info(selector, index, owner):
    pb = bytearray(128)
    struct.pack_into(">h", pb, 22, selector)
    struct.pack_into(">h4s", pb, 26, index, owner)
    write(base, pb)
    result = value(f"(int)PBGetWDInfo((void *){base},0)")
    if result == 0:
        returned_owner, vref = struct.unpack(">4sh", read(base + 28, 6))
        directory = struct.unpack(">I", read(base + 48, 4))[0]
        return returned_owner, vref, directory
    assert result == -35
    return None


# Finder uses index 1 to check for directories left open under its creator code.
other_wd = open_wd(dos_ref, 2, b"Test")
assert wd_info(0, 1, b"ERIK") is None
assert wd_info(other_wd, 0, bytes(4)) == (b"Test", dos_ref, 2)
assert wd_info(dos_ref, 1, bytes(4)) == (b"Test", dos_ref, 2)
open_wd(apps_ref, 2, b"ERIK")
open_wd(dos_ref, 2, b"ERIK")
write(base, bytes(22) + struct.pack(">h", other_wd) + bytes(104))
assert value(f"(int)PBCloseWD((void *){base},0)") == 0
assert wd_info(0, 1, b"ERIK") == (b"ERIK", apps_ref, 2)
assert wd_info(0, 2, b"ERIK") == (b"ERIK", dos_ref, 2)
assert wd_info(0, 3, b"ERIK") is None
assert wd_info(dos_ref, 1, b"ERIK") == (b"ERIK", dos_ref, 2)
assert wd_info(127, 1, b"ERIK") == (b"ERIK", apps_ref, 2)
assert wd_info(32767, 1, b"ERIK") is None
print("DOS_VOLUME_AND_APPS_DESKTOP_POLICY_OK")
print("HOST_VOLUME_RENAME_AND_DIRECTORY_OVERRIDE_OK")
print("WORKING_DIRECTORY_OWNER_AND_VOLUME_FILTERS_OK")
