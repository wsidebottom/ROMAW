# +===================================================================
#   File:      create-window-resize-probe.py
#
#   Summary:   Check actual Window Manager resize damage and retained pending updates.
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

"""Check actual Window Manager resize damage and retained pending updates."""

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
set $data=(unsigned char *)_NewPtr_flags(512,1,1)
python
import gdb,struct
mem=gdb.selected_inferior()
data=int(gdb.parse_and_eval('$data'))
offset=int(gdb.parse_and_eval('ROMlib_offset'))
def call(expression):
 return int(gdb.parse_and_eval(expression))
def action(expression):
 gdb.execute('call '+expression,to_string=True)
def rect(values):
 mem.write_memory(data,struct.pack('>hhhh',*values))
def contains(region,v,h):
 mem.write_memory(data+64,struct.pack('<hh',v,h))
 return bool(call('(int)C_PtInRgn(*(Point *)($data+64),(void *)'+str(region)+')'))
for function in ('C_NewWindow','C_NewCWindow'):
 rect((100,100,220,300))
 mem.write_memory(data+32,b'\x06Resize')
 window=call('(void *)'+function+'(0,(void *)$data,(void *)($data+32),1,8,(void *)-1,1,0)')
 assert window
 update=int.from_bytes(mem.read_memory(window+122,4),'big')+offset
 action('C_SetPort((void *)'+str(window)+')')
 action('C_SetOrigin(40,20)')
 for width,height,flag in ((260,180,1),(300,220,0),(200,120,1)):
  action('C_SetEmptyRgn((void *)'+str(update)+')')
  rect((110,110,120,120))
  action('C_RectRgn((void *)'+str(update)+',(void *)$data)')
  action('C_SizeWindow((void *)'+str(window)+','+str(width)+','+str(height)+','+str(flag)+')')
  assert contains(update,115,115),(function,'lost pending damage',width,height,flag)
  assert not contains(update,150,150),(function,'invalidated unchanged content',width,height,flag)
  if width==260:
   assert contains(update,150,340) and contains(update,250,150),(function,'missing exposed strip')
  if not flag:
   assert not contains(update,150,380) and not contains(update,300,150),(function,'ignored update flag')
  bounds=struct.unpack('>hhhh',bytes(mem.read_memory(window+16,8)))
  assert bounds==(20,40,20+height,40+width),(function,'changed local origin',bounds)
 # A click inside the grow box must not add its corner inset to the size.
 # Queue just a release event to cover quick drags with no intermediate poll.
 for width,height in ((150,240),(208,190)):
  action('C_SizeWindow((void *)'+str(window)+','+str(width)+','+str(height)+',1)')
  origin=struct.unpack('>hhhh',bytes(mem.read_memory(window+16,8)))
  # These fixture windows are mono or color; use the real local-to-global API.
  mem.write_memory(data+96,struct.pack('>hh',origin[0]+height-7,origin[1]+width-5))
  action('C_LocalToGlobal((void *)($data+96))')
  start=struct.unpack('>hh',bytes(mem.read_memory(data+96,4)))
  mem.write_memory(data+64,struct.pack('<hh',*start))
  for delta_v,delta_h,limits,expected in (
   (-80,0,(80,width,500,width+1),(height-80,width)),
   (70,0,(80,width,500,width+1),(height+70,width)),
   (0,0,(80,width,500,width+1),None),
   (-1000,-1000,(80,100,500,400),(80,100)),
   (1000,1000,(80,100,500,400),(499,399)),
  ):
   mem.write_memory(data+80,struct.pack('>4h',*limits))
   mem.write_memory(data+104,struct.pack('<hh',start[0]+delta_v,start[1]+delta_h))
   action('FlushEvents(6,0)')
   assert call('(int)ROMlib_PPostEvent(2,0,0,0,*(Point *)($data+104),128)')==0
   result=call('(long)C_GrowWindow((void *)'+str(window)+',*(Point *)($data+64),(void *)($data+80))')
   wanted=0 if expected is None else (expected[0]<<16)|expected[1]
   assert result==wanted,(function,'grow limits/click offset',width,height,delta_v,delta_h,hex(result),hex(wanted))
 if function=='C_NewCWindow':
  assert call('(long)ppc_set_theme_window_background((void *)'+str(window)+',1,1)')==0
  # Showing the window and repainting its content must use the same theme
  # brush as EraseRect in its color port, rather than the old white wctb.
  action('C_ShowHide((void *)'+str(window)+',0)')
  action('C_ShowHide((void *)'+str(window)+',1)')
  action('C_SetPort((void *)'+str(window)+')')
  action('C_GetCPixel(60,40,(void *)($data+112))')
  assert bytes(mem.read_memory(data+112,6))==struct.pack('>3H',0xdddd,0xdddd,0xdddd),'theme background after ShowHide'
  content=call('(void *)validate_colors_for_window((void *)'+str(window)+')')
  assert bytes(mem.read_memory(content,6))==struct.pack('>3H',0xdddd,0xdddd,0xdddd),'theme background for PaintOne'
 action('C_DisposeWindow((void *)'+str(window)+')')
print('WINDOW_RESIZE_OK mono=3 color=3 pending_damage=6 grow_offsets=20',flush=True)
end
call DisposPtr($data)
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
