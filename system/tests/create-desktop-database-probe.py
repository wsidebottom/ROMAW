# +===================================================================
#   File:      create-desktop-database-probe.py
#
#   Summary:   Exercise Desktop Manager calls through real 68K HFSDispatch traps.
#
#   Classes:   None declared.
#
#   Functions: generate
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

"""Exercise Desktop Manager calls through real 68K HFSDispatch traps."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    volume = directory / "volume"
    volume.mkdir()
    (volume / "App").write_bytes(b"fixture")
    body = r"""
import gdb,struct
mem=gdb.selected_inferior()
offset=int(gdb.parse_and_eval('ROMlib_offset'))
base=int(gdb.parse_and_eval('$state'))
proc=int(gdb.parse_and_eval('$proc'))
path=VOLUME.encode()+b'\0'
mem.write_memory(base+2048,path)
assert int(gdb.parse_and_eval('config_drive_prefs_load("",(char *)'+str(base+2048)+')'))==0
saved=gdb.parse_and_eval('ROMlib_volumename')
gdb.execute('set ROMlib_volumename=(char *)'+str(base+2048))
pb=bytearray(128)
struct.pack_into('>h',pb,22,127)
mem.write_memory(base,bytes(pb))
assert int(gdb.parse_and_eval('(int)ufsPBMountVol((void *)'+str(base)+')'))==0
gdb.execute('set ROMlib_volumename=(char *)'+str(int(saved)))
vref=struct.unpack('>h',bytes(mem.read_memory(base+22,2)))[0]
pb=bytearray(128)
struct.pack_into('>h',pb,22,vref)
def call(selector,expected=0):
 mem.write_memory(base,bytes(pb))
 words=[0x207c,(base-offset)>>16,(base-offset)&65535,0x7000|selector,0xa260,0x2f40,4,0x4e75]
 mem.write_memory(proc,struct.pack('>8H',*words))
 gdb.execute('call destroy_blocks((unsigned int)'+str(proc-offset)+',128)',to_string=True)
 result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))&65535
 assert result==(expected&65535),(hex(selector),result,expected)
 pb[:]=bytes(mem.read_memory(base,128))
 assert struct.unpack_from('>h',pb,16)[0]==expected,('ioResult',hex(selector),pb.hex())
call(0x2e)
assert struct.unpack_from('>I',pb,28)[0]==0
# The volume reports a real catalog snapshot, not the historical ten-file
# Windows statfs placeholder. Two invisible database data forks also exist.
volume=int(gdb.parse_and_eval('(void *)ROMlib_vcbbyvrn('+str(vref)+')'))
file_count=int.from_bytes(mem.read_memory(volume+int(gdb.parse_and_eval('(unsigned long long)&((VCB *)0)->vcbFilCnt')),4),'big')
assert file_count == 3,('catalog total',file_count)
volume_info=bytearray(128)
struct.pack_into('>h',volume_info,22,vref)
mem.write_memory(base+3072,bytes(volume_info))
assert int(gdb.parse_and_eval('(int)PBHGetVInfo((void *)'+str(base+3072)+',0)'))==0
count_offset=int(gdb.parse_and_eval('(unsigned long long)&((HVolumeParam *)0)->ioVFilCnt'))
assert int.from_bytes(mem.read_memory(base+3072+count_offset,4),'big')==file_count
reference=bytes(pb[24:26])
call(0x2e)
assert struct.unpack_from('>I',pb,28)[0]!=0
art=bytes(i&255 for i in range(1024))
mem.write_memory(base+512,art)
struct.pack_into('>III',pb,28,0x10203040,base+512-offset,1024)
pb[45]=3
pb[52:60]=b'TESTAPPL'
call(0x22)
mem.write_memory(base+512,b'\xc3'*1024)
call(0x23)
assert bytes(mem.read_memory(base+512,1024))==art
assert struct.unpack_from('>II',pb,28)[0]==0x10203040
struct.pack_into('>h',pb,26,1)
call(0x24)
assert pb[45]==3 and pb[56:60]==b'APPL'
assert struct.unpack_from('>I',pb,40)[0]==1024
struct.pack_into('>h',pb,26,0)
call(0x24,-5012)
struct.pack_into('>I',pb,36,256)
call(0x22,-5030)
struct.pack_into('>I',pb,36,4501)
call(0x22,-50)
# Catalog-backed application and comment keys; no fixture app is launched.
mem.write_memory(base+256,b'\x03App')
struct.pack_into('>I',pb,18,base+256-offset)
struct.pack_into('>I',pb,48,2)
call(0x25)
struct.pack_into('>h',pb,26,0)
call(0x27)
assert bytes(mem.read_memory(base+256,4))==b'\x03App'
assert struct.unpack_from('>I',pb,100)[0]==2
comment=b'Classic Desktop comment'
mem.write_memory(base+512,comment)
struct.pack_into('>II',pb,32,base+512-offset,len(comment))
call(0x28)
mem.write_memory(base+512,b'\xc3'*len(comment))
call(0x2a)
assert bytes(mem.read_memory(base+512,len(comment)))==comment
call(0x29)
call(0x2a,-5012)
call(0x26)
call(0x27,-5012)
call(0x2b)
call(0x21)
call(0x23,-51)
call(0x20)
call(0x2c)
struct.pack_into('>I',pb,36,1024)
call(0x23,-5012)
call(0x2f)
call(0x2e)
assert struct.unpack_from('>I',pb,28)[0]==0
call(0x21)
print('DESKTOP_DATABASE_68K_OK')
"""
    body = body.replace("VOLUME", repr(volume.as_posix()))
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "break ROMlib_win_drive_preferences",
        "commands",
        "silent",
        "return 0",
        "continue",
        "end",
        "break C_WaitNextEvent",
        f"run {json.dumps(application.replace(chr(92), '/'))}",
        "disable 1 2",
        "set $state=(unsigned char *)_NewPtr_flags(4096,1,1)",
        "set $proc=(unsigned char *)_NewPtr_flags(128,1,1)",
        "python",
        body.strip(),
        "end",
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n")


if __name__ == "__main__":
    destination = Path(sys.argv[1]).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2])
