# +===================================================================
#   File:      create-get-map-probe.py
#
#   Summary:   Generate real A822 GetMap/override-attribute probes for staged ROMAW.
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

"""Generate real A822 GetMap/override-attribute probes for staged ROMAW."""

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
        "set $current=(int)C_CurResFile()",
        "set $proc=(unsigned char *)_NewPtr_flags(128,1,1)",
        "set $state=(unsigned char *)_NewPtr_flags(16,1,1)",
        "python",
        "import gdb,struct",
        "current=int(gdb.parse_and_eval('$current'))",
        "inferior=gdb.selected_inferior()",
        "proc=int(gdb.parse_and_eval('$proc'))",
        "offset=int(gdb.parse_and_eval('ROMlib_offset'))",
        "state=int(gdb.parse_and_eval('$state'))",
        "for ref in (current,0,32767,-32768,current,0):",
        " expected=int(gdb.parse_and_eval('(void *)C_GetMap('+str(ref)+')'))",
        " expected_error=int(gdb.parse_and_eval('(int)C_ResError()'))",
        # Pascal Handle result (4 bytes), short refnum, trap, pop result.
        " words=[0x598f,0x3f3c,ref&65535,0x203c,0x1234,0,0x227c,0x2468,0x1350,0x44fc,0x001b,0xa822]",
        " words += [0x42c1,0x23c0,(state-offset)>>16,(state-offset)&65535,0x33c1,(state-offset+4)>>16,(state-offset+4)&65535]",
        " words += [0x23c9,(state-offset+8)>>16,(state-offset+8)&65535,0x201f,0x2f40,4,0x4e75]",
        " code=struct.pack('>'+str(len(words))+'H',*words)",
        " inferior.write_memory(proc,code)",
        " gdb.execute('call destroy_blocks((unsigned int)('+str(proc-offset)+'),128)',to_string=True)",
        " actual=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))&0xffffffff",
        " assert actual == ((expected-offset)&0xffffffff if expected else 0),(ref,hex(actual),hex(expected))",
        " assert int(gdb.parse_and_eval('(int)C_ResError()')) == expected_error",
        " snapshot=bytes(inferior.read_memory(state,12))",
        " assert int.from_bytes(snapshot[:4],'big') == 0x12340000|(expected_error&65535),('D0',snapshot.hex(),expected_error)",
        " expected_cc=0x10|(8 if expected_error<0 else 0)|(4 if expected_error==0 else 0)",
        " assert int.from_bytes(snapshot[4:6],'big')&31 == expected_cc,('CCR',snapshot.hex(),expected_cc)",
        " assert int.from_bytes(snapshot[8:12],'big') == 0x24681350",
        "end",
        "python",
        "map_handle=int(gdb.parse_and_eval('(void *)C_GetMap('+str(current)+')'))",
        "map_address=int.from_bytes(inferior.read_memory(map_handle,4),'big')+offset",
        "saved_attributes=bytes(inferior.read_memory(map_address+22,2))",
        "saved_error=bytes(inferior.read_memory(offset+0xa60,2))",
        "try:",
        " for attributes in (0,1,2,4,8,16,30,255,0xff00,0xffff,None):",
        "  if attributes is not None: inferior.write_memory(map_address+22,struct.pack('>H',attributes))",
        "  inferior.write_memory(offset+0xa60,struct.pack('>h',-36))",
        "  argument=map_handle-offset if attributes is not None else 0",
        # Pascal SignedByte uses the first byte of a two-byte result slot.
        "  words=[0x3f3c,0xa55a,0x2f3c,argument>>16,argument&65535,0x203c,0x1234,8,0x44fc,0x001b,0xa822]",
        "  words += [0x42c1,0x23c0,(state-offset)>>16,(state-offset)&65535,0x33c1,(state-offset+4)>>16,(state-offset+4)&65535]",
        "  words += [0x301f,0x2f40,4,0x4e75]",
        "  inferior.write_memory(proc,struct.pack('>'+str(len(words))+'H',*words))",
        "  gdb.execute('call destroy_blocks((unsigned int)('+str(proc-offset)+'),128)',to_string=True)",
        "  actual=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))&65535",
        "  expected=0xa55a if attributes is None else ((attributes&30)<<8)|0x5a",
        "  assert actual == expected,('override result',attributes,hex(actual),hex(expected))",
        "  error=-109 if attributes is None else -36",
        "  assert int(gdb.parse_and_eval('(int)C_ResError()')) == error",
        "  snapshot=bytes(inferior.read_memory(state,6))",
        "  assert int.from_bytes(snapshot[:4],'big') == 0x12340000|(error&65535),('override D0',snapshot.hex())",
        "  assert int.from_bytes(snapshot[4:6],'big')&31 == 0x18,('override CCR',snapshot.hex())",
        "finally:",
        " inferior.write_memory(map_address+22,saved_attributes)",
        " inferior.write_memory(offset+0xa60,saved_error)",
        "end",
        "call DisposPtr($proc)",
        "call DisposPtr($state)",
        'printf "GET_MAP_PASCAL_OK\\n"',
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(commands) + "\n")


if __name__ == "__main__":
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2])
