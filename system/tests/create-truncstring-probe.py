# +===================================================================
#   File:      create-truncstring-probe.py
#
#   Summary:   Generate Windows GDB checks for classic text truncation and both stack ABIs.
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

"""Generate Windows GDB checks for text truncation and both classic stack ABIs."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    script = r"""
set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run -debug trace -system 9.0 -syszone 16m -applzone 64m APPLICATION
disable 1
python
import gdb,struct
mem=gdb.selected_inferior();base=int(gdb.parse_and_eval('ROMlib_offset'))
def call(expr):return int(gdb.parse_and_eval(expr))
storage=call('(void *)_NewPtr_flags(128,1,1)')
assert storage
cpu=int(gdb.parse_and_eval('&cpu_state'))
saved=bytes(mem.read_memory(cpu,gdb.parse_and_eval('cpu_state').type.sizeof))
try:
 for selector,where in [(0x8206ffe0,None),(0x8208ffe0,0),(0x8208ffe0,0x4000)]:
  string=storage+96
  mem.write_memory(string,b'\x03abc')
  args=(b'' if where is None else struct.pack('>H',where))
  args+=struct.pack('>IH',string-base,160)
  stack=struct.pack('>II',0x123456,selector)+args+struct.pack('>HI',0xcccc,0xdeadbeef)
  mem.write_memory(storage,stack)
  gdb.execute('set cpu_state.regs[15].ul.n='+str(storage-base),to_string=True)
  returned=call('(unsigned int)_ScriptUtil(0,0)')
  assert returned==0x123456,('return',hex(returned))
  actual=call('cpu_state.regs[15].ul.n')
  expected=storage-base+8+len(args)
  assert actual==expected,(hex(selector),hex(actual),hex(expected))
  assert bytes(mem.read_memory(base+actual+2,4))==struct.pack('>I',0xdeadbeef),('guard',hex(selector))
  result=int.from_bytes(mem.read_memory(base+actual,2),'big',signed=True)
  assert result==0,result
  assert bytes(mem.read_memory(string,4))==b'\x03abc',bytes(mem.read_memory(string,4))
  print('TRUNCSTRING_STACK_OK selector='+hex(selector),flush=True)
 # Use real QuickDraw metrics; verify fitting, end/middle truncation, empty
 # results, invalid lengths, Pascal byte order and guards through the trap.
 text=storage+64
 length=storage+100
 sample=b'abcdefghijklmno'
 mem.write_memory(text,sample)
 target=call('(short)C_TextWidth((char *)'+str(text)+',0,7)')
 marker=call('(short)C_CharWidth(201)')
 assert target>marker>0
 for selector,where in [(0x820affde,None),(0x820cffde,0),(0x820cffde,0x4000)]:
  for width,count,expected in [(32767,len(sample),0),(target,len(sample),1),
                               (0,len(sample),-1),(100,0,0),(100,-1,-1)]:
   mem.write_memory(text,sample+b'\xaa')
   mem.write_memory(length,struct.pack('>h',count))
   args=(b'' if where is None else struct.pack('>H',where))
   args+=struct.pack('>IIh',length-base,text-base,width)
   stack=struct.pack('>II',0x123456,selector)+args+struct.pack('>HI',0xcccc,0xdeadbeef)
   mem.write_memory(storage,stack)
   gdb.execute('set cpu_state.regs[15].ul.n='+str(storage-base),to_string=True)
   assert call('(unsigned int)_ScriptUtil(0,0)')==0x123456
   actual=call('cpu_state.regs[15].ul.n')
   assert actual==storage-base+8+len(args)
   assert bytes(mem.read_memory(base+actual+2,4))==struct.pack('>I',0xdeadbeef)
   result=int.from_bytes(mem.read_memory(base+actual,2),'big',signed=True)
   assert result==expected,(hex(selector),width,count,result)
   size=int.from_bytes(mem.read_memory(length,2),'big',signed=True)
   assert bytes(mem.read_memory(text+len(sample),1))==b'\xaa'
   if expected==1:
    output=bytes(mem.read_memory(text,size))
    assert 0<size<len(sample) and output.count(b'\xc9')==1,output
    assert call('(short)C_TextWidth((char *)'+str(text)+',0,'+str(size)+')')<=width
    left,right=output.split(b'\xc9')
    assert sample.startswith(left) and sample.endswith(right)
    if where==0x4000: assert left and right,output
    else: assert not right,output
   elif count>=0: assert size==(0 if expected==-1 else count)
  print('TRUNCTEXT_STACK_OK selector='+hex(selector),flush=True)
finally:
 mem.write_memory(cpu,saved)
 gdb.execute('call (void)DisposPtr((void *)'+str(storage)+')',to_string=True)
end
quit 0
"""
    script = script.replace("APPLICATION", json.dumps(application.replace("\\", "/")))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "probe.gdb").write_text(script.lstrip(), encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), sys.argv[2])
