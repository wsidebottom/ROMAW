# +===================================================================
#   File:      create-drive-icon-probe.py
#
#   Summary:   Probe embedded Classic drive icons and host disk-driver icon controls.
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

"""Probe embedded Classic drive icons and host disk-driver icon controls."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    commands = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "set environment DEFAULTFOLDER D:/Source/ROMAW",
        "break C_WaitNextEvent",
        f"run {json.dumps(application.replace(chr(92), '/'))}",
        "set $pb=(unsigned char *)_NewPtr_flags(512,1,1)",
        "set $saved=(int)C_CurResFile()",
        "set $sysraw=*(unsigned short *)((char *)ROMlib_offset+0xa58)",
        "set $system=(short)($sysraw>>8 | $sysraw<<8)",
        "call C_UseResFile($system)",
        "python",
        "import gdb,struct",
        "mem=gdb.selected_inferior()",
        "offset=int(gdb.parse_and_eval('ROMlib_offset'))",
        "pb=int(gdb.parse_and_eval('$pb'))",
        "for rid in (-3995,-3972,-16455):",
        " for kind,size in ((0x49434e23,256),(0x69636c34,512),(0x69636c38,1024),(0x69637323,64),(0x69637334,128),(0x69637338,256)):",
        "  h=int(gdb.parse_and_eval('(void *)C_Get1Resource('+str(kind)+','+str(rid)+')'))",
        "  assert h and int(gdb.parse_and_eval('(int)GetHandleSize((void *)'+str(h)+')'))==size,(rid,kind)",
        "volume=int(gdb.parse_and_eval('(void *)ROMlib_vcbbydrive(3)'))",
        "tested=shared=romaw=0",
        "for drive in range(1,128):",
        " volume=int(gdb.parse_and_eval('(void *)ROMlib_vcbbydrive('+str(drive)+')'))",
        " if not volume or int(gdb.parse_and_eval('((VCB *)'+str(volume)+')->vcbCTRef')): continue",
        " kind=int(gdb.parse_and_eval('(int)romaw_host_volume_kind(((VCBExtra *)'+str(volume)+')->unixname)'))",
        " name_record=bytes(mem.read_memory(volume+44,28))",
        " name=name_record[1:1+name_record[0]]",
        " rid=-16455 if name==b'ROMAW' else -3972 if kind==2 else -3998 if kind==3 else -3995",
        " romaw+=name==b'ROMAW'",
        " for index in (-1,0):",
        "  raw=bytearray(512)",
        "  struct.pack_into('>Ih',raw,18,pb+128-offset,int.from_bytes(bytes(mem.read_memory(volume+78,2)),'big',signed=True))",
        "  struct.pack_into('>h',raw,28,index)",
        "  struct.pack_into('>I',raw,48,2)",
        "  mem.write_memory(pb,bytes(raw))",
        "  result=int(gdb.parse_and_eval('(int)PBGetCatInfo($pb,0)'))",
        "  assert result==0,('root catalog',drive,index,result)",
        "  returned=bytes(mem.read_memory(pb+128,28))",
        "  assert returned[1:1+returned[0]]==name,('volume alias',name,returned)",
        " h=int(gdb.parse_and_eval('(void *)C_Get1Resource(0x49434e23,'+str(rid)+')'))",
        " gdb.execute('call C_LoadResource((void *)'+str(h)+')',to_string=True)",
        " image=int.from_bytes(mem.read_memory(h,4),'big')+offset",
        " expected=bytes(mem.read_memory(image,256))",
        " for selector in (21,22):",
        "  raw=bytearray(128)",
        "  struct.pack_into('>hhh',raw,22,drive,0,selector)",
        "  raw[28:32]=b'ZZZZ'",
        "  mem.write_memory(pb,bytes(raw))",
        "  result=int(gdb.parse_and_eval('(int)PBControl($pb,0)'))",
        "  assert result==0,(drive,selector,result)",
        "  result_pb=bytes(mem.read_memory(pb,128))",
        "  pointer=int.from_bytes(result_pb[28:32],'big')",
        "  assert pointer and not pointer&1",
        "  assert bytes(mem.read_memory(pointer+offset,256))==expected,(drive,kind,rid)",
        "  assert result_pb[16:18]==b'\\0\\0'",
        " # Finder copies the legacy driver image into a mono-only suite.",
        " # Color companions must follow the same embedded classic ID.",
        " mono=int(gdb.parse_and_eval('(void *)_NewHandle_flags(256,1,0)'))",
        " image=int.from_bytes(mem.read_memory(mono,4),'big')+offset",
        " mem.write_memory(image,expected)",
        " for depth in (1,2,4,8,16,32):",
        "  gdb.execute('set *(int *)($pb+400)='+str(depth),to_string=True)",
        "  state=tuple(bytes(mem.read_memory(offset+a,n)) for a,n in ((0x118,4),(0xa50,4),(0xa5a,2),(0xa5e,1),(0xa60,2)))",
        "  color=int(gdb.parse_and_eval('(void *)ROMlib_host_disk_color_icon((void *)'+str(mono)+',(int *)($pb+400))'))",
        "  assert state==tuple(bytes(mem.read_memory(offset+a,n)) for a,n in ((0x118,4),(0xa50,4),(0xa5a,2),(0xa5e,1),(0xa60,2))),'color lookup changed resource state'",
        "  assert bool(color)==(depth>=4),(name,depth,color)",
        "  if color:",
        "   bits=8 if depth>=8 else 4",
        "   expected_color=int(gdb.parse_and_eval('(void *)C_Get1Resource('+str(0x69636c38 if bits==8 else 0x69636c34)+','+str(rid)+')'))",
        "   assert color==expected_color,(name,depth,rid)",
        "   assert int(gdb.parse_and_eval('*(int *)($pb+400)'))==bits",
        " # Exercise the actual suite selection used by PlotIconSuite.",
        " assert int(gdb.parse_and_eval('(int)C_NewIconSuite((void *)($pb+384))'))==0",
        " suite=int.from_bytes(mem.read_memory(pb+384,4),'big')+offset",
        " assert int(gdb.parse_and_eval('(int)C_AddIconToSuite((void *)'+str(mono)+',(void *)'+str(suite)+',0x49434e23)'))==0",
        " for explicit in (False,True):",
        "  if explicit:",
        "   color4=int(gdb.parse_and_eval('(void *)C_Get1Resource(0x69636c34,'+str(rid)+')'))",
        "   assert int(gdb.parse_and_eval('(int)C_AddIconToSuite((void *)'+str(color4)+',(void *)'+str(suite)+',0x69636c34)'))==0",
        "  result=int(gdb.parse_and_eval('(int)find_best_icon(0,8,(void *)'+str(suite)+',(void *)($pb+256),(void *)($pb+272),(void *)($pb+288),(void *)($pb+304))'))",
        "  assert result==0 and int(gdb.parse_and_eval('*(int *)($pb+304)'))==(4 if explicit else 8)",
        "  selected=int(gdb.parse_and_eval('*(void **)($pb+256)'))",
        "  expected_color=int(gdb.parse_and_eval('(void *)C_Get1Resource('+str(0x69636c34 if explicit else 0x69636c38)+','+str(rid)+')'))",
        "  assert selected==expected_color,(name,'suite color selection',explicit)",
        " gdb.execute('call C_DisposeIconSuite((void *)'+str(suite)+',0)',to_string=True)",
        " # A custom mask or image must not acquire a built-in color icon.",
        " for changed in (0,128,255):",
        "  custom=bytearray(expected);custom[changed]^=1",
        "  mem.write_memory(image,bytes(custom))",
        "  gdb.execute('set *(int *)($pb+400)=8',to_string=True)",
        "  assert not int(gdb.parse_and_eval('(void *)ROMlib_host_disk_color_icon((void *)'+str(mono)+',(int *)($pb+400))'))",
        " gdb.execute('call DisposHandle((void *)'+str(mono)+')',to_string=True)",
        " tested+=1",
        " shared+=kind==2",
        "assert tested and romaw==1, 'Expected one ROMAW host volume'",
        "for drive,reference,selector in ((32767,0,21),(3,0,20),(3,-32768,21)):",
        " raw=bytearray(128)",
        " struct.pack_into('>hhh',raw,22,drive,reference,selector)",
        " raw[28:32]=b'ZZZZ'",
        " mem.write_memory(pb,bytes(raw))",
        " result=int(gdb.parse_and_eval('(int)PBControl($pb,0)'))",
        " assert result!=0 and bytes(mem.read_memory(pb+28,4))==b'ZZZZ'",
        "print('DRIVE_ICON_CONTROL_OK volumes=%d shared=%d romaw=%d'%(tested,shared,romaw),flush=True)",
        "end",
        "call C_UseResFile($saved)",
        "call DisposPtr($pb)",
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(commands) + "\n")


if __name__ == "__main__":
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2])
