# +===================================================================
#   File:      open_deny_m68k.gdb
#
#   Summary:   Provides open deny m68k debugger commands for regression tests and test fixtures.
#
#   Classes:   None.
#
#   Functions: Debugger command sequence; no exported functions.
#
#   Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

set pagination off
set confirm off
set print thread-events off

break unhandled_trap if (long long)arg==4
python
import gdb,struct,os
from pathlib import Path
root=Path(os.environ["ARDI_CONTEXT_TEST_ROOT"])
gdb.execute("set environment USERPROFILE "+str(root/"build/cooperative-context-profile"))
gdb.execute("set substitute-path /mnt/d/Source/ROMAW "+root.as_posix())
seen=set()
class Title(gdb.Breakpoint):
 def stop(self):
  p=int(gdb.parse_and_eval('t'));m=gdb.selected_inferior();n=bytes(m.read_memory(p,1))[0] if p else 0
  name=bytes(m.read_memory(p+1,n)).decode('mac_roman') if n else ''
  print('TITLE',name,flush=True);seen.add(name);return False
Title('C_SetWTitle',internal=True)
class Ready(gdb.Breakpoint):
 def __init__(self):super().__init__('C_WaitNextEvent',internal=True);self.count=0
 def stop(self):
  if 'Desktop' in seen:self.count+=1
  return self.count>=3
ready=Ready()
end
python
gdb.execute('run -system 7.1.1 -applzone 6m "'+(root/"build/cooperative-context-profile/.romaw/Apps/Finder").as_posix()+'"')
end
python
from pathlib import Path
ready.enabled=False
m=gdb.selected_inferior();offset=int(gdb.parse_and_eval('ROMlib_offset'))
fixture=root/'build/cooperative-context-profile/.romaw/Apps/Open Deny Probe'
fixture.write_bytes(b'data-fork')
resource=bytes(range(41))
info=b'TEXTttxt'+bytes(24)
fixture.with_name('._'+fixture.name).write_bytes(struct.pack('>II16sHIIIIII',0x51607,0x20000,b'Mac OS X        ',2,9,50,32,2,82,len(resource))+info+resource)
code=int(gdb.parse_and_eval('(void *)_NewPtr_flags(64,1,1)'));assert code
storage=int(gdb.parse_and_eval('(void *)_NewPtr_flags(512,1,1)'));assert storage
stack=int(gdb.parse_and_eval('(void *)_NewPtr_flags(65536,1,1)'));assert stack
name=fixture.as_posix().replace(':/', ':').replace('/', ':').encode('ascii');assert len(name)<256
name_guest=(storage-offset+128)&0xffffffff
m.write_memory(storage+128,bytes([len(name)])+name)
sp=(stack-offset+65536-4)&0xffffffff
magic=(int(gdb.parse_and_eval('(uintptr_t)syn68k_callback_address_space'))+32-offset)&0xffffffff
for selector,length in ((0x38,9),(0x39,41)):
 pb=bytearray(80);struct.pack_into('>Ih',pb,18,name_guest,0);pb[27]=0x11
 m.write_memory(storage,pb)
 blob=bytes.fromhex('207c')+struct.pack('>I',(storage-offset)&0xffffffff)+bytes((0x70,selector))+bytes.fromhex('a2604e75')
 m.write_memory(code,blob);m.write_memory(offset+sp,struct.pack('>I',magic))
 gdb.execute('set cpu_state.regs[15].ul.n = '+str(sp))
 entry=(code-offset)&0xffffffff
 gdb.execute('call (void)destroy_blocks('+str(entry)+','+str(len(blob))+')')
 gdb.execute('call (void)interpret_code((const unsigned short *)_hash_lookup_code_and_create_if_needed('+str(entry)+'))')
 assert int(gdb.parse_and_eval('cpu_state.regs[0].ul.n'))&0xffff==0,('open failed',selector,int(gdb.parse_and_eval('cpu_state.regs[0].ul.n')))
 ref=struct.unpack('>h',bytes(m.read_memory(storage+24,2)))[0]
 result=int(gdb.parse_and_eval('GetEOF('+str(ref)+',(int *)'+str(storage+96)+')'))
 actual=struct.unpack('<i',bytes(m.read_memory(storage+96,4)))[0]
 assert result==0 and actual==length,(selector,result,actual,length)
 assert int(gdb.parse_and_eval('FSClose('+str(ref)+')'))==0
print('OPEN_DENY_M68K_TRAPS_PASSED',flush=True)
end
quit
