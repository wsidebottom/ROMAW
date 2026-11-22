# +===================================================================
#   File:      default_button_outline_probe.gdb
#
#   Summary:   Supply the application with GDB's set args before sourcing this probe.
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

# Supply the application with GDB's set args before sourcing this probe.
set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run
delete breakpoints
python
import gdb, struct
mem=gdb.selected_inferior()
offset=int(gdb.parse_and_eval('ROMlib_offset'))
buf=int(gdb.parse_and_eval('(void *)_NewPtr_flags(64,0,1)'))
rect=buf+16
mem.write_memory(rect,struct.pack('>hhhh',0,0,40,80))
error=int(gdb.parse_and_eval('(int)C_NewGWorld((void *)'+str(buf)+',32,(void *)'+str(rect)+',0,0,0)'))
assert not error,error
world=int.from_bytes(mem.read_memory(buf,4),'big')+offset
gdb.execute('call C_SetGWorld((void *)'+str(world)+',0)',to_string=True)
gdb.execute('call C_PenNormal()',to_string=True)
gdb.execute('call C_RGBForeColor(&ROMlib_black_cspec.rgb)',to_string=True)
gdb.execute('call C_RGBBackColor(&ROMlib_white_cspec.rgb)',to_string=True)

pm=int(gdb.parse_and_eval('(void *)C_GetGWorldPixMap((void *)'+str(world)+')'))
pmdata=int.from_bytes(mem.read_memory(pm,4),'big')+offset
stride=int.from_bytes(mem.read_memory(pmdata+4,2),'big')&0x3fff
base=int(gdb.parse_and_eval('(void *)C_GetPixBaseAddr((void *)'+str(pm)+')'))
corner=[
 '.....########',
 '...##########',
 '..###########',
 '.#####.......',
 '.###.........',
 '####.........',
 '###..........',
]
for operation in ('ROMlib_frame_default_button','C_FrameRoundRect'):
 mem.write_memory(rect,struct.pack('>hhhh',0,0,40,80))
 gdb.execute('call C_EraseRect((void *)'+str(rect)+')',to_string=True)
 mem.write_memory(rect,struct.pack('>hhhh',0,0,28,67))
 gdb.execute('call C_PenSize(3,3)',to_string=True)
 arguments=',16,16' if operation=='C_FrameRoundRect' else ''
 gdb.execute('call '+operation+'((void *)'+str(rect)+arguments+')',to_string=True)
 raw=bytes(mem.read_memory(base,stride*40))
 for y in range(28):
  edge=min(y,27-y,6)
  for x in range(67):
   if x>=4 and x<63 and y>=4 and y<24: continue
   expected=corner[edge][min(x,66-x,12)]=='#'
   pixel=raw[y*stride+x*4:y*stride+x*4+4]
   assert (pixel==bytes(4))==expected,(operation,x,y,pixel.hex(),expected)
 print('QUICKDRAW_DEFAULT_BUTTON_PIXELS_OK',operation)
end
quit 0
