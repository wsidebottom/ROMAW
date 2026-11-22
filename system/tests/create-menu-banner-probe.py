# +===================================================================
#   File:      create-menu-banner-probe.py
#
#   Summary:   Exercise DrawMBARString through the real 68K MenuDispatch trap.
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

"""Exercise DrawMBARString through the real 68K MenuDispatch trap."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    script = r"""
set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run APPLICATION
disable 1
set $proc=(unsigned char *)_NewPtr_flags(256,1,1)
python
import gdb, struct
inferior=gdb.selected_inferior()
base=int(gdb.parse_and_eval('ROMlib_offset'))
proc=int(gdb.parse_and_eval('$proc'))
guest=proc-base
def action(expression):
 gdb.execute('call '+expression,to_string=True)
def bar():
 framebuffer=int(gdb.parse_and_eval('vdriver_fbuf'))
 stride=int(gdb.parse_and_eval('vdriver_row_bytes'))
 height=int.from_bytes(inferior.read_memory(base+0xbaa,2),'big')
 assert 1 <= height <= 64, height
 return bytes(inferior.read_memory(framebuffer,stride*height))
action('C_DrawMenuBar()')
original=bar()
action('C_GetPort((void *)($proc+192))')
port=bytes(inferior.read_memory(proc+192,4))
snapshots=[]
for script,justification,text in ((0,-1,b'Drag banner'),(1,0,b'Drag banner'),(0,1,b'Drag banner'),(0,0,b''),(128,0,b'Roman banner')):
 inferior.write_memory(proc+128,bytes([len(text)])+text)
 # A word result, then Pascal arguments; copy the result into our long result.
 words=[0x3f3c,0xa55a,0x2f3c,(guest+128)>>16,(guest+128)&65535,
        0x3f3c,script,0x3f3c,justification&65535,0x303c,0x08fb,0xa825,
        0x7000,0x301f,0x2f40,4,0x4e75]
 inferior.write_memory(proc,struct.pack('>'+str(len(words))+'H',*words))
 action('destroy_blocks('+str(guest)+',128)')
 result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))
 assert result==0,('Pascal result/stack',result)
 action('C_GetPort((void *)($proc+192))')
 assert bytes(inferior.read_memory(proc+192,4))==port,'changed current port'
 assert bytes(inferior.read_memory(base+0xb20,1))[0]&0x40,'missing dirty flag'
 snapshots.append(bar())
 if text: assert snapshots[-1]!=original,'banner was not drawn'
 action('C_HiliteMenu(0)')
 assert not bytes(inferior.read_memory(base+0xb20,1))[0]&0x40,'dirty flag not cleared'
 assert bar()==original,'menu titles not restored'
assert len(set(snapshots[:3]))==3,'justification ignored'
assert snapshots[3]!=original,'empty banner did not clear titles'
print('MENU_BANNER_PASCAL_OK cases=5 restore=5',flush=True)
end
call DisposPtr($proc)
quit 0
"""
    (directory / "probe.gdb").write_text(script.replace("APPLICATION", json.dumps(application.replace("\\", "/"))))


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("application")
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    generate(args.directory, args.application)
