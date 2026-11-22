# +===================================================================
#   File:      create-follow-alias-probe.py
#
#   Summary:   Exercise FollowFinderAlias with real 68K Pascal stack calls under GDB.
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

"""Exercise FollowFinderAlias with real 68K Pascal stack calls under GDB."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    script = """set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run APP
 disable 1
set $data=(unsigned char *)_NewPtr_flags(1024,1,1)
set $proc=(unsigned char *)_NewPtr_flags(128,1,1)
python
import gdb,struct
mem=gdb.selected_inferior()
data=int(gdb.parse_and_eval('$data'))
proc=int(gdb.parse_and_eval('$proc'))
offset=int(gdb.parse_and_eval('ROMlib_offset'))
spec=int(gdb.parse_and_eval('&current_process_info->app_spec'))
original=bytes(mem.read_memory(spec,70))
mem.write_memory(data,original)
mem.write_memory(data+6,b'\\x05Alias')
assert int(gdb.parse_and_eval('C_NewAliasMinimal((FSSpec *)'+str(spec)+',(HIDDEN_AliasHandle *)'+str(data+80)+')'))==0
alias_guest=int.from_bytes(bytes(mem.read_memory(data+80,4)),'big')
alias=alias_guest+offset
payload_guest=int.from_bytes(bytes(mem.read_memory(alias,4)),'big')
payload=payload_guest+offset
size=int(gdb.parse_and_eval('GetHandleSize((Handle)'+str(alias)+')'))
saved=bytes(mem.read_memory(payload,size))
def invoke(source,logon,expected):
 mem.write_memory(data+128,b'\\xa5'*96)
 code=struct.pack('>H',0x554f)
 code+=struct.pack('>HI',0x4879,source)
 code+=struct.pack('>HI',0x2f3c,alias_guest)
 code+=struct.pack('>HH',0x3f3c,logon<<8)
 code+=struct.pack('>HI',0x4879,data+128-offset)
 code+=struct.pack('>HI',0x4879,data+208-offset)
 code+=struct.pack('>HHHHHHH',0x700f,0xa823,0x301f,0x48c0,0x2f40,4,0x4e75)
 gdb.execute('call destroy_blocks('+str(proc-offset)+',128)',to_string=True)
 mem.write_memory(proc,code)
 stack=int(gdb.parse_and_eval('cpu_state.regs[15].ul.n'))
 result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))
 assert result==expected,(result,expected)
 assert int(gdb.parse_and_eval('cpu_state.regs[15].ul.n'))==stack,'Pascal stack imbalance'
 raw=bytes(mem.read_memory(data+128,96))
 if expected==0:
  assert raw[:6]==original[:6] and raw[6:7+original[6]]==original[6:7+original[6]],raw.hex()
  assert raw[80]==0
 else: assert raw==b'\\xa5'*96,'failure modified outputs'
 assert bytes(mem.read_memory(payload,size))==saved,'alias mutated'
for logon in (0,1): invoke(data-offset,logon,0)
invoke(spec-offset,0,-43)
invoke(0,0,-50)
# Real v2 parent ID differs from ROMAW's historical minimal record layout.
changed=bytearray(saved)
changed[46:50]=original[2:6]
changed[114:118]=b'\\x12\\x34\\x56\\x78'
mem.write_memory(payload,changed)
saved=bytes(changed)
invoke(data-offset,1,0)
# Malformed version, extent and Pascal names fail without publishing outputs.
standard=saved
for start,raw in ((4,b'\\x00\\x01'),(4,b'\\xff\\xff'),(6,b'\\x00\\x03'),(10,b'\\x1c'),(50,b'\\x40')):
 changed=bytearray(standard);changed[start:start+len(raw)]=raw
 mem.write_memory(payload,changed);saved=bytes(changed)
 invoke(data-offset,0,-50)
mem.write_memory(payload,standard);saved=standard
# A valid absolute-path record is supported, including even record padding.
fullpath=standard[11:11+standard[10]]+b':'+original[7:7+original[6]]
changed=bytearray(standard[:150])
changed+=struct.pack('>HH',2,len(fullpath))+fullpath
if len(fullpath)&1: changed+=b'\\x00'
changed+=b'\\xff\\xff\\x00\\x00'
# The generated minimal alias has ample tail storage; keep the handle extent.
assert len(changed)<=size
changed[4:6]=struct.pack('>H',len(changed))
changed+=b'\\x00'*(size-len(changed))
mem.write_memory(payload,changed);saved=bytes(changed)
invoke(data-offset,1,0)
# A data length exceeding the declared alias record must be rejected.
changed[152:154]=b'\\xff\\xff'
mem.write_memory(payload,changed);saved=bytes(changed)
invoke(data-offset,0,-50)
print('FOLLOW_FINDER_ALIAS_OK trap_calls=12',flush=True)
end
quit 0
"""
    (directory / "probe.gdb").write_text(script.replace("APP", json.dumps(application.replace("\\", "/"))))


if __name__ == "__main__":
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    generate(directory, sys.argv[2])
