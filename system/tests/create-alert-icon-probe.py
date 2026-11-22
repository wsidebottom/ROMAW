# +===================================================================
#   File:      create-alert-icon-probe.py
#
#   Summary:   Probe masked alert-icon loading and drawing in the Windows runtime.
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

"""Probe masked alert-icon loading and drawing in the Windows runtime."""

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mac_icons


def generate(directory, application, depth):
    rows = mac_icons.image((ROOT / "bitmaps/warning.icns").read_bytes(), 32)
    transparent = next((x, y) for y, row in enumerate(rows) for x, p in enumerate(row) if p[3] < 128)
    opaque = next((x, y) for y, row in enumerate(rows) for x, p in enumerate(row) if p[3] >= 128 and sum(p[:3]) > 500)
    color = rows[opaque[1]][opaque[0]][:3]
    expected = tuple(c * 257 for c in color) if depth > 2 else (65535,) * 3
    script = r"""
set pagination off
set confirm off
set print thread-events off
break C_GetNextEvent if *(unsigned char *)(ROMlib_offset+0x8f2)==0
run -bpp DEPTH APPLICATION
disable 1
python
import gdb,struct
mem=gdb.selected_inferior();base=int(gdb.parse_and_eval('ROMlib_offset'))
def call(expr):return int(gdb.parse_and_eval(expr))
def action(expr):gdb.execute('call (void)'+expr,to_string=True)
def u32(p):return int.from_bytes(mem.read_memory(p,4),'big')
data=call('(void *)_NewPtr_flags(128,1,1)')
assert data
mem.write_memory(data,struct.pack('>4h',100,100,164,164))
mem.write_memory(data+16,b'\x04Mask')
mem.write_memory(data+24,struct.pack('>3H',65535,0,0))
mem.write_memory(data+40,struct.pack('>4h',4,4,36,36))
items=call('(void *)_NewHandle_flags(18,1,0)')
assert items
mem.write_memory(base+u32(items),struct.pack('>hI4hBBh',0,0,4,4,36,36,32,2,2))
dialog=call('(void *)C_NewDialog(0,(void *)'+str(data)+',(void *)'+str(data+16)+',1,1,(void *)-1,0,0,(void *)'+str(items)+')')
assert dialog
ioff=call('(long long)&((DialogPeek)0)->items')
item=base+u32(base+u32(dialog+ioff))+2
icon=base+u32(item)
assert call('(long)GetHandleSize((void *)'+str(icon)+')')>128,'dialog chose an unmasked ICON'
action('C_SetPort((void *)'+str(dialog)+')')
action('C_RGBForeColor((void *)'+str(data+24)+')')
action('C_PaintRect((void *)'+str(data+40)+')')
# Read the actual screen palette: GetCPixel treats old-style GrafPorts as 1-bit.
gd=base+u32(call('(void *)C_GetMainDevice()'))
pmoff=call('(long long)&((GDPtr)0)->gdPMap')
pm=base+u32(base+u32(gd+pmoff))
table=base+u32(base+u32(pm+42))
framebuffer=call('(void *)vdriver_fbuf')
stride=call('vdriver_row_bytes')
actual_depth=call('vdriver_bpp')
assert actual_depth==DEPTH
def pixel(point):
 x,y=point
 x+=104;y+=104
 sample=bytes(mem.read_memory(framebuffer+y*stride+x*actual_depth//8,1))[0]
 index=(sample>>(8-actual_depth-(x*actual_depth%8)))&((1<<actual_depth)-1)
 return bytes(mem.read_memory(table+10+index*8,6))
transparent=TRANSPARENT
opaque=OPAQUE
expected=struct.pack('>3H',*EXPECTED)
background=pixel(transparent)
assert pixel(opaque)!=expected,'fixture background must differ from icon'
action('dialog_draw_item((void *)'+str(dialog)+',(void *)'+str(item)+',1)')
assert pixel(transparent)==background,'transparent icon pixel overwrote background'
assert pixel(opaque)==expected,'opaque icon pixel was lost'
action('C_DisposDialog((void *)'+str(dialog)+')')
action('DisposPtr((void *)'+str(data)+')')
print('ALERT_ICON_MASK_OK depth=DEPTH',flush=True)
end
quit 0
"""
    script = script.replace("APPLICATION", json.dumps(application.replace("\\", "/")))
    script = (
        script.replace("DEPTH", str(depth))
        .replace("TRANSPARENT", repr(transparent))
        .replace("OPAQUE", repr(opaque))
        .replace("EXPECTED", repr(expected))
    )
    (directory / "probe.gdb").write_text(script.lstrip())


if __name__ == "__main__":
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    generate(directory, sys.argv[2], int(sys.argv[3]))
