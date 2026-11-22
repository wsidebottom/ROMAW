# +===================================================================
#   File:      create-dcmp0-probe.py
#
#   Summary:   Generate a staged guest-call probe for the embedded dcmp 0 resource.
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

"""Generate a staged guest-call probe for the embedded dcmp 0 resource."""

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
        "set $handle=(void *)C_GetResource(0x64636d70,0)",
        "python",
        "import gdb",
        "assert int(gdb.parse_and_eval('$handle')) != 0, 'Missing embedded dcmp 0'",
        "end",
        "call C_LoadResource($handle)",
        "set $buffer=(unsigned char *)_NewPtr_flags(128,1,1)",
        "python",
        "mem=gdb.selected_inferior()",
        "offset=int(gdb.parse_and_eval('ROMlib_offset'))",
        "handle=int(gdb.parse_and_eval('$handle'))",
        "code=int.from_bytes(mem.read_memory(handle,4),'big')+offset",
        "stub=bytes(mem.read_memory(code,10))",
        "assert stub == bytes.fromhex('20780058206800284ed0'),stub.hex()",
        "buffer=int(gdb.parse_and_eval('$buffer'))",
        # Copy and execute the guest stub: resource code must survive relocation.
        "mem.write_memory(buffer+96,stub)",
        "for stream,expected in [(bytes.fromhex('014142ff'),b'AB'),(bytes.fromhex('11414223ff'),b'ABAB'),(bytes.fromhex('fe0120080110ff'),bytes.fromhex('610000204eed0010610000184eed0018'))]:",
        " for overlap in (False,True):",
        "  mem.write_memory(buffer,b'Z'*96)",
        "  mem.write_memory(buffer+16,stream)",
        "  dest=buffer+16 if overlap else buffer+48",
        "  gdb.execute('call CToPascalCall((void *)'+str(buffer+96)+',0x4b68,(void *)'+str(buffer+16)+',(void *)'+str(dest)+',(void *)0,'+str(len(stream))+')',to_string=True)",
        "  assert bytes(mem.read_memory(dest,len(expected))) == expected",
        "  assert bytes(mem.read_memory(dest-1,1)) == b'Z'",
        "  if not overlap: assert bytes(mem.read_memory(dest+len(expected),1)) == b'Z'",
        "  assert int(gdb.parse_and_eval('(int)C_ResError()')) == 0",
        "mem.write_memory(buffer,b'Z'*96)",
        "mem.write_memory(buffer+16,bytes.fromhex('23ff'))",
        "gdb.execute('call CToPascalCall((void *)'+str(buffer+96)+',0x4b68,(void *)'+str(buffer+16)+',(void *)'+str(buffer+48)+',(void *)0,2)',to_string=True)",
        "assert bytes(mem.read_memory(buffer+48,16)) == b'Z'*16",
        "assert int(gdb.parse_and_eval('(int)C_ResError()')) != 0",
        "end",
        "call DisposPtr($buffer)",
        'printf "DCMP0_RESOURCE_GUEST_OK\\n"',
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(commands) + "\n")


if __name__ == "__main__":
    destination = Path(sys.argv[1])
    destination.mkdir(parents=True, exist_ok=True)
    generate(destination, sys.argv[2])
