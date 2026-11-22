# +===================================================================
#   File:      cooperative_context_m68k.gdb
#
#   Summary:   Provides cooperative context m68k debugger commands for regression tests and test fixtures.
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
blob=(root/"build/cooperative-context-probe.bin").read_bytes()
code=int(gdb.parse_and_eval('(void *)_NewPtr_flags('+str(len(blob))+',1,1)'));assert code
stack=int(gdb.parse_and_eval('(void *)_NewPtr_flags(65536,1,1)'));assert stack
m.write_memory(code,blob)
sp=(stack-offset+65536-4)&0xffffffff
magic=(int(gdb.parse_and_eval('(uintptr_t)syn68k_callback_address_space'))+32-offset)&0xffffffff
m.write_memory(offset+sp,struct.pack('>I',magic))
gdb.execute('set cpu_state.regs[15].ul.n = '+str(sp))
entry=(code-offset)&0xffffffff
# A Toolbox callback reenters interpret_code while the outer frame is live.
# Even a capture-only request must defer rather than bind that nested frame.
print('NESTING_BEFORE',gdb.parse_and_eval('ROMlib_cooperative_depth'),gdb.parse_and_eval('emulation_depth'),flush=True)
nested_code=int(gdb.parse_and_eval('(void *)_NewPtr_flags(28,1,1)'));assert nested_code
nested_entry=(nested_code-offset)&0xffffffff
nested=bytes.fromhex('2f3c000000002f3c000000004eb9')+struct.pack('>I',nested_entry+24)+bytes.fromhex('4fef00084e75abfe4e75')
assert len(nested)==28
m.write_memory(nested_code,nested)
gdb.execute('call (void)destroy_blocks('+str(nested_entry)+','+str(len(nested))+')')
gdb.execute('call (void)interpret_code((const unsigned short *)_hash_lookup_code_and_create_if_needed('+str(nested_entry)+'))')
print('NESTING_AFTER',gdb.parse_and_eval('ROMlib_cooperative_depth'),gdb.parse_and_eval('emulation_depth'),gdb.parse_and_eval('cpu_state.regs[0].ul.n'),flush=True)
assert int(gdb.parse_and_eval('cpu_state.regs[0].ul.n'))==1,'Nested context switch did not defer'
print('NESTED_CONTEXT_SWITCH_DEFERRED',flush=True)
m.write_memory(offset+sp,struct.pack('>I',magic))
gdb.execute('set cpu_state.regs[15].ul.n = '+str(sp))
# This isolated injected program owns a nested interpreter until process exit.
gdb.execute('set ROMlib_cooperative_interpreter = (void *)0')
gdb.execute('set ROMlib_cooperative_depth = 0')
gdb.execute('call (void)destroy_blocks('+str(entry)+','+str(len(blob))+')')
print('CONTEXT_PROBE_START',hex(entry),hex(sp),hex(magic),flush=True)
try:
 gdb.execute('call (void)interpret_code((const unsigned short *)_hash_lookup_code_and_create_if_needed('+str(entry)+'))')
except gdb.error:
 gdb.execute('p cpu_state')
 regs=[int(gdb.parse_and_eval('cpu_state.regs['+str(i)+'].ul.n')) for i in range(16)]
 sp=regs[15]
 print('FAULT_GUEST_STACK',hex(sp),bytes(m.read_memory(offset+sp,48)).hex(),flush=True)
 print('FAULT_FRAME_PC',hex(int.from_bytes(m.read_memory(offset+sp+2,4),'big')),flush=True)
 (root/'build/cooperative-context-failure-stack.bin').write_bytes(bytes(m.read_memory(stack,65536)))
 raise
value=int(gdb.parse_and_eval('cpu_state.regs[0].ul.n'))
assert value==1,('Context probe failed',value)
print('COOPERATIVE_M68K_CONTEXTS_PASSED',flush=True)
end
quit
