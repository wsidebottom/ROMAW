# +===================================================================
#   File:      create-file-icon-probe.py
#
#   Summary:   Probe native file icon copies and masked classic file-dialog rows.
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

"""Generate a staged Resource Manager probe for native file icon copies."""

import json
import sys
from pathlib import Path


def generate(directory, browser, macwrite):
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "break C_WaitNextEvent",
        f"run -32bit -bpp 8 -syszone 8m -applzone 64m {json.dumps(str(browser).replace(chr(92), '/'))}",
        "disable 1",
        "set $name=(unsigned char *)_NewPtr_flags(256,0,1)",
        "set $spec=(FSSpec *)_NewPtr_flags(sizeof(FSSpec),0,1)",
        "set $info=(FInfo *)_NewPtr_flags(sizeof(FInfo),0,1)",
        "set $icon=(FinderIcon *)_NewPtr_flags(sizeof(FinderIcon),0,1)",
    ]
    for application, requested in (
        (browser, None),
        (macwrite, "APPL"),
        (macwrite, "WORD"),
    ):
        name = (application.drive + ":".join(application.parts[1:])).encode("mac_roman")
        assert len(name) < 256
        name = bytes([len(name)]) + name
        script += [
            f"set {{unsigned char[{len(name)}]}} $name = " + "{" + ",".join(map(str, name)) + "}",
            "if C_FSMakeFSSpec(0,0,$name,$spec) != 0",
            "quit 1",
            "end",
            "if C_FSpGetFInfo($spec,$info) != 0",
            "quit 1",
            "end",
            "python",
            "info=bytes(gdb.selected_inferior().read_memory(int(gdb.parse_and_eval('$info')),8))",
            f"kind={requested!r}",
            "gdb.execute('set $type='+str(int.from_bytes(kind.encode('mac_roman') if kind else info[:4],'big')))",
            "gdb.execute('set $creator='+str(int.from_bytes(info[4:8],'big')))",
            "end",
            "set $map=CurMap",
            "set $top=TopMapHndl",
            "set $loading=ResLoad",
            "set $error=ResErr",
            "set ResLoad=0",
            "set ResErr=1234",
            "set $found=ROMlib_read_file_icon($spec,$creator,$type,0,$icon)",
            "if !$found || !($icon->present & 3)",
            'printf "FILE_ICON_MISSING type=%x creator=%x\\n",$type,$creator',
            "quit 1",
            "end",
            "if CurMap != $map || TopMapHndl != $top || ResLoad != 0 || ResErr != 1234",
            'printf "FILE_ICON_RESOURCE_STATE_CHANGED\\n"',
            'printf "map=%x/%x top=%x/%x load=%d error=%d\\n",CurMap,$map,TopMapHndl,$top,ResLoad,ResErr',
            "quit 1",
            "end",
            'printf "FILE_ICON_OK type=%x resources=%x\\n",$type,$icon->present',
            "set $found=ROMlib_read_file_icon($spec,$creator,0x5a5a5a5a,0,$icon)",
            "if $found || $icon->present",
            'printf "UNRELATED_FILE_TYPE_GOT_AN_ICON\\n"',
            "quit 1",
            "end",
            "set ResLoad=$loading",
            "set ResErr=$error",
        ]
    script += [
        "python",
        "import struct",
        "mem=gdb.selected_inferior(); base=int(gdb.parse_and_eval('ROMlib_offset'))",
        "def call(expr): return int(gdb.parse_and_eval(expr))",
        "def action(expr): gdb.execute('call (void)'+expr,to_string=True)",
        "def u32(p): return int.from_bytes(mem.read_memory(p,4),'big')",
        "data=call('(void *)_NewPtr_flags(128,1,1)')",
        "action('C_GetWMgrPort((void *)'+str(data+40)+')')",
        "action('C_SetPort((void *)'+str(base+u32(data+40))+')')",
        "mem.write_memory(data,struct.pack('>4h',100,100,12,200))",
        "mem.write_memory(data+16,struct.pack('>4h',100,102,116,118))",
        "mem.write_memory(int(gdb.parse_and_eval('$icon')),bytes(gdb.lookup_type('FinderIcon').sizeof))",
        "gdb.execute('set $icon->present=2',to_string=True)",
        "gdb.execute('set $icon->small[32]=128',to_string=True)",
        "suite=call('(void *)suite_from_finder_icon($icon)')",
        "assert suite",
        "action('C_ForeColor(33)'); action('C_BackColor(30)')",
        "action('C_PaintRect((void *)'+str(data+16)+')')",
        "framebuffer=call('(void *)vdriver_fbuf'); stride=call('vdriver_row_bytes'); depth=call('vdriver_bpp')",
        "assert depth in (8,16,32),depth",
        "def pixel(x,y): return bytes(mem.read_memory(framebuffer+y*stride+x*depth//8,depth//8))",
        "background=pixel(102,100)",
        "action('drawinboxwithicon((void *)'+str(data+32)+',(void *)'+str(data)+',0,0,0,(void *)'+str(suite)+')')",
        "assert pixel(102,100)!=background,'opaque icon pixel was not drawn'",
        "assert all(pixel(x,y)==background for y in range(100,116) for x in range(102,118) if (x,y)!=(102,100)), 'transparent row pixels overwrote the background'",
        "for opacity in (0,255):",
        " mem.write_memory(int(gdb.parse_and_eval('&$icon->small[32]')),bytes([opacity])*32)",
        " family=call('(void *)suite_from_finder_icon($icon)')",
        " assert family",
        " action('C_PaintRect((void *)'+str(data+16)+')')",
        " action('drawinboxwithicon((void *)'+str(data+32)+',(void *)'+str(data)+',0,0,0,(void *)'+str(family)+')')",
        " assert all((pixel(x,y)!=background)==bool(opacity) for y in range(100,116) for x in range(102,118)),('uniform mask ignored',opacity)",
        " action('C_DisposeIconSuite((void *)'+str(family)+',1)')",
        "for kind,rid in enumerate((-4000,-3999,-3998,-3997,-3996,-3995)):",
        " if kind==3:",
        "  assert call('(int)finder_location_icon(0,$icon)')",
        "  mask=bytes(mem.read_memory(int(gdb.parse_and_eval('&$icon->small[32]')),32))",
        " else:",
        "  h=call('(void *)C_GetResource(0x69637323,'+str(rid)+')')",
        "  assert h,('missing built-in icon mask',rid)",
        "  action('C_LoadResource((void *)'+str(h)+')')",
        "  mask=bytes(mem.read_memory(base+u32(h)+32,32))",
        " assert any(mask) and any(b!=255 for b in mask),('fixture needs opaque and transparent pixels',rid)",
        " action('C_PaintRect((void *)'+str(data+16)+')')",
        " action('drawinboxwithicon((void *)'+str(data+32)+',(void *)'+str(data)+','+str(kind)+',0,0,0)')",
        " assert all(pixel(x+102,y+100)==background for y in range(16) for x in range(16) if not mask[y*2+x//8]&(128>>(x%8))),('built-in icon lost transparency',rid)",
        " assert any(pixel(x+102,y+100)!=background for y in range(16) for x in range(16)),('built-in icon not drawn',rid)",
        "action('C_DisposeIconSuite((void *)'+str(suite)+',1)')",
        "action('DisposPtr((void *)'+str(data)+')')",
        "print('FILE_ROW_TRANSPARENCY_OK',flush=True)",
        "end",
        'printf "NATIVE_FILE_ICONS_OK\\n"',
        "quit 0",
    ]
    globals = {
        "CurMap": "(*(short *)(ROMlib_offset+0xa5a))",
        "TopMapHndl": "(*(unsigned int *)(ROMlib_offset+0xa50))",
        "ResLoad": "(*(unsigned char *)(ROMlib_offset+0xa5e))",
        "ResErr": "(*(short *)(ROMlib_offset+0xa60))",
    }
    for name, expression in globals.items():
        script = [line.replace(name, expression) for line in script]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(
        Path(sys.argv[1]).resolve(),
        Path(sys.argv[2]).resolve(),
        Path(sys.argv[3]).resolve(),
    )
