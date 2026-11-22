# +===================================================================
#   File:      create-folder-name-probe.py
#
#   Summary:   Exercise AliasDispatch GetFolderName through original 68K Pascal calls.
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

"""Exercise AliasDispatch GetFolderName through original 68K Pascal calls."""

import json
import struct
import sys
from pathlib import Path


def generate(directory, application):
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "break C_WaitNextEvent",
        "run " + json.dumps(application.replace("\\", "/")),
        "disable 1",
        "set $data=(unsigned char *)_NewPtr_flags(512,1,1)",
        "set $proc=(unsigned char *)_NewPtr_flags(128,1,1)",
        "python",
        "import gdb,struct",
        "mem=gdb.selected_inferior()",
        "data=int(gdb.parse_and_eval('$data'))",
        "proc=int(gdb.parse_and_eval('$proc'))",
        "offset=int(gdb.parse_and_eval('ROMlib_offset'))",
        "cases=((b'desk',b'Desktop Folder'),(b'trsh',b'Trash'),(b'empt',b'Network Trash Folder'),(b'temp',b'Temporary Items'),(b'xxxx',None))",
        "for volume in (-32768,-7,0,123):",
        " for kind,name in cases:",
        "  mem.write_memory(data,b'\\xa5'*512)",
        "  code=struct.pack('>HHHHIHIHIHHHHHHHH',0x554f,0x3f3c,volume&65535,0x2f3c,int.from_bytes(kind,'big'),0x4879,data-offset,0x4879,data+16-offset,0x700e,0xa823,0x301f,0x48c0,0x2f40,4,0x4e75,0)",
        "  gdb.execute('call destroy_blocks('+str(proc-offset)+',128)',to_string=True)",
        "  mem.write_memory(proc,code)",
        "  stack=int(gdb.parse_and_eval('cpu_state.regs[15].ul.n'))",
        "  result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))",
        "  assert int(gdb.parse_and_eval('cpu_state.regs[15].ul.n'))==stack,'stack imbalance'",
        "  raw=bytes(mem.read_memory(data,512))",
        "  assert result==(0 if name else -50),(volume,kind,result)",
        "  if name:",
        "   assert raw[:2]==struct.pack('>h',volume)",
        "   expected=bytes([len(name)])+name",
        "   assert raw[16:16+len(expected)]==expected,(kind,raw.hex())",
        "   assert raw[2:16]==b'\\xa5'*14 and raw[16+len(expected):]==b'\\xa5'*(496-len(expected))",
        "  else: assert raw==b'\\xa5'*512,'failed call changed outputs'",
        "print('FOLDER_NAME_OK trap_calls=20',flush=True)",
        "end",
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n")


if __name__ == "__main__":
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    generate(directory, sys.argv[2])
