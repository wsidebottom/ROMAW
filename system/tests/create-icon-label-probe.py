# +===================================================================
#   File:      create-icon-label-probe.py
#
#   Summary:   Probe label colors through real icon-suite drawing and guest color tables.
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

"""Probe label colors through real icon-suite drawing and guest color tables."""

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
python
import gdb,struct
mem=gdb.selected_inferior();base=int(gdb.parse_and_eval('ROMlib_offset'))
def call(expr):return int(gdb.parse_and_eval(expr))
def action(expr):gdb.execute('call (void)'+expr,to_string=True)
def u(p,n=4):return int.from_bytes(mem.read_memory(p,n),'big')
def handle(data):
 h=call('(void *)_NewHandle_flags('+str(len(data))+',1,0)');assert h
 mem.write_memory(base+u(h),data)
 return h
scratch=call('(void *)_NewPtr_flags(512,1,1)');assert scratch
action('C_GetGWorld((void *)'+str(scratch+392)+',(void *)'+str(scratch+396)+')')
saved_port=base+u(scratch+392);saved_device=base+u(scratch+396)
mem.write_memory(scratch,struct.pack('>4h',0,0,32,32))
mem.write_memory(scratch+16,struct.pack('>4h',0,0,64,64))
for label,name in enumerate(['None','Project 2','Project 1','Personal','Cool','In Progress','Hot','Essential']):
 assert call('(int)C_GetLabel('+str(label)+',(void *)'+str(scratch+32)+',(void *)'+str(scratch+64)+')')==0
 assert bytes(mem.read_memory(scratch+65,u(scratch+64,1))).decode()==name
for bad in (-1,8):assert call('(int)C_GetLabel('+str(bad)+',0,0)')==-50
for depth in (1,4,8):
 assert call('(int)C_NewGWorld((void *)'+str(scratch+384)+','+str(depth)+',(void *)'+str(scratch+16)+',0,0,0)')==0
 world=base+u(scratch+384)
 pixmap=call('(void *)C_GetGWorldPixMap((void *)'+str(world)+')')
 assert call('(int)C_LockPixels((void *)'+str(pixmap)+')')
 action('C_SetGWorld((void *)'+str(world)+',0)')
 for size in (16,32):
  mem.write_memory(scratch,struct.pack('>4h',0,0,size,size))
  for source_depth in (1,4,8):
   pixels=[0 if x<size//4 else (7 if source_depth==4 else 248) if x<size//2 else (15 if source_depth==4 else 255) if x<3*size//4 else 2 for y in range(size) for x in range(size)]
   mono=(bytes(size//16)+bytes([255])*(size//16))*size
   mask=bytes(size//8)+bytes([255])*(size*(size-1)//8)
   mono_handle=handle(mono+mask)
   assert call('(int)C_NewIconSuite((void *)'+str(scratch+388)+')')==0
   suite=base+u(scratch+388)
   assert call('(int)C_AddIconToSuite((void *)'+str(mono_handle)+',(void *)'+str(suite)+','+str(0x49434e23 if size==32 else 0x69637323)+')')==0
   color_handle=0
   if source_depth!=1:
    packed=bytes(pixels) if source_depth==8 else bytes((pixels[i]<<4)|pixels[i+1] for i in range(0,len(pixels),2))
    color_handle=handle(packed)
    kind=(0x69636c34 if source_depth==4 else 0x69636c38) if size==32 else (0x69637334 if source_depth==4 else 0x69637338)
    assert call('(int)C_AddIconToSuite((void *)'+str(color_handle)+',(void *)'+str(suite)+','+str(kind)+')')==0
   def draw(transform,offset=0):
    action('C_RGBBackColor((void *)'+str(scratch+400)+')')
    action('C_EraseRect((void *)'+str(scratch+16)+')')
    assert call('(int)C_PlotIconSuite((void *)'+str(scratch)+',0,'+str(transform)+',(void *)'+str(suite)+')')==0
    result=[]
    for x,y in [(2,0),(2,4),(size//4+1,4),(size//2+1,4),(3*size//4+1,4)]:
     action('C_GetCPixel('+str(x+offset)+','+str(y+offset)+',(void *)'+str(scratch+32)+')')
     result.append(bytes(mem.read_memory(scratch+32,6)))
    return result
   mem.write_memory(scratch+400,struct.pack('>3H',0,65535,0))
   original=draw(0);labeled=draw(0x300)
   assert labeled[0]==original[0],'label changed transparent mask pixel'
   mem.write_memory(scratch,struct.pack('>4h',1,1,size+1,size+1))
   assert draw(0x300,1)==labeled,'odd-position pixels differ'
   action('C_GetCPixel(0,4,(void *)'+str(scratch+32)+')')
   assert bytes(mem.read_memory(scratch+32,6))==original[0],'neighbor pixel changed'
   mem.write_memory(scratch,struct.pack('>4h',0,0,size,size))
   if depth>=4:
    assert labeled!=original,(depth,size,source_depth,'missing label')
    colored=labeled[1] if depth==8 and source_depth==8 else labeled[3]
    red,green,blue=struct.unpack('>3H',colored)
    assert blue>red and blue>green,(depth,size,source_depth,'blue label rendered gray',colored.hex())
    for number in range(1,8):assert draw(number<<8)!=original,(depth,size,source_depth,number)
    if source_depth==8 and depth==8:assert labeled[4]==original[4],'label altered protected detail color'
    assert call('(int)C_SetSuiteLabel((void *)'+str(suite)+',3)')==0
    assert draw(0)==labeled,'default suite label not applied'
    assert draw(0x600)!=labeled,'explicit label did not override suite label'
    assert call('(int)C_SetSuiteLabel((void *)'+str(suite)+',0)')==0
   else:assert labeled==original,'color labels changed a monochrome destination'
   assert bytes(mem.read_memory(base+u(mono_handle),len(mono+mask)))==mono+mask
   if color_handle:assert bytes(mem.read_memory(base+u(color_handle),len(packed)))==packed
   action('C_DisposeIconSuite((void *)'+str(suite)+',1)')
   print('ICON_LABEL_PIXELS_OK',depth,size,source_depth,flush=True)
 action('C_SetGWorld((void *)'+str(world)+',0)')
 # Restore a screen port before disposing the offscreen world.
 action('C_SetGWorld((void *)'+str(saved_port)+',(void *)'+str(saved_device)+')')
 action('C_DisposeGWorld((void *)'+str(world)+')')
action('DisposPtr((void *)'+str(scratch)+')')
end
quit 0
"""
    script = script.replace("APPLICATION", json.dumps(application.replace("\\", "/")))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "probe.gdb").write_text(script.lstrip(), encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), sys.argv[2])
