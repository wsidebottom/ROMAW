# +===================================================================
#   File:      create-launch-parameters-probe.py
#
#   Summary:   Convert standard System 7 launch events with distinct document aliases.
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

"""Convert standard System 7 launch events with distinct document aliases."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    files = [directory / "one", directory / "two"]
    for file in files:
        file.write_text("Launch document fixture\n")
    script = r"""
set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run APPLICATION
disable 1
set $data=(unsigned char *)_NewPtr_flags(8192,1,1)
python
import gdb,struct
mem=gdb.selected_inferior()
base=int(gdb.parse_and_eval('ROMlib_offset'))
data=int(gdb.parse_and_eval('$data'))
def call(expression):
 return int(gdb.parse_and_eval(expression))
def action(expression):
 gdb.execute('call '+expression,to_string=True)
def pointer(address):
 return '(void *)'+str(address)
aliases=[]
specs=[]
for filename in FILES:
 path=(filename[:2]+filename[3:].replace('/',':')).encode('mac_roman')
 mem.write_memory(data+4096,bytes([len(path)])+path)
 error=call('(int)C_FSMakeFSSpec(0,0,'+pointer(data+4096)+','+pointer(data+2048)+')')
 assert error==0,('FSSpec',filename,error)
 specs.append(bytes(mem.read_memory(data+2048,70)))
 error=call('(int)C_NewAliasMinimal('+pointer(data+2048)+','+pointer(data+3000)+')')
 assert error==0,('alias',filename,error)
 handle=base+int.from_bytes(mem.read_memory(data+3000,4),'big')
 payload=base+int.from_bytes(mem.read_memory(handle,4),'big')
 size=call('(int)GetHandleSize('+pointer(handle)+')')
 aliases.append(bytes(mem.read_memory(payload,size)))
 action('DisposHandle('+pointer(handle)+')')
def event(identifier,wire):
 # EventRecord, refcon, messageLength; where carries the event ID.
 return struct.pack('>H4sI4sHII',0,b'aevt',0,identifier,0,0,len(wire))+wire
header=b'aevt'+struct.pack('>I',0x10001)+b';;;;'
def docs(items):
 body=struct.pack('>II',len(items),0)
 for item in items:
  body+=b'alis'+struct.pack('>I',len(item))+item+b'\0'*(len(item)&1)
 return event(b'odoc',header+b'----list'+struct.pack('>I',len(body))+body)
def convert(payload,expected):
 mem.write_memory(data,payload)
 mem.write_memory(data+7000,b'\xa5'*16)
 error=call('(int)ROMlib_decode_launch_parameters('+pointer(data)+','+pointer(data+7000)+')')
 handle=call('*(unsigned long long *)'+pointer(data+7000))
 assert error==expected,(error,expected)
 if error:
  assert handle==0
  return
 record=base+int.from_bytes(mem.read_memory(handle,4),'big')
 count=int.from_bytes(mem.read_memory(record+4,2),'big')
 assert bytes(mem.read_memory(record,4))==bytes.fromhex('d6434a1b')
 for index in range(count):
  actual=bytes(mem.read_memory(record+6+index*70,70))
  expected_spec=specs[index]
  assert actual[:7+actual[6]]==expected_spec[:7+expected_spec[6]],('document',index,actual,expected_spec)
 action('DisposHandle('+pointer(handle)+')')
 return count
assert convert(docs(aliases),0)==2,'document count'
assert convert(event(b'oapp',header),0)==0,'open application count'
assert convert(docs([]),0)==0,'empty document count'
convert(event(b'xxxx',header),-50)
malformed=bytearray(docs(aliases)); malformed[20:24]=b'\xff'*4
convert(malformed,-50)
convert(event(b'odoc',header+b'----list'+struct.pack('>III',8,1,0)),-1700)
end_address=base+int(gdb.parse_and_eval('ROMlib_guest_memory_size'))-8
assert call('(int)ROMlib_decode_launch_parameters('+pointer(end_address)+','+pointer(data+7000)+')')==-50
assert call('(int)ROMlib_decode_launch_parameters(0,'+pointer(data+7000)+')')==-50
assert call('(int)ROMlib_decode_launch_parameters('+pointer(data)+',0)')==-50
print('LAUNCH_PARAMETERS_OK distinct_documents=2 malformed=6',flush=True)
end
call DisposPtr($data)
quit 0
"""
    paths = [str(file.resolve()).replace("\\", "/") for file in files]
    (directory / "probe.gdb").write_text(
        script.replace("APPLICATION", json.dumps(application.replace("\\", "/"))).replace("FILES", repr(paths))
    )


if __name__ == "__main__":
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2])
