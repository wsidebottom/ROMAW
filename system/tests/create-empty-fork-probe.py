# +===================================================================
#   File:      create-empty-fork-probe.py
#
#   Summary:   Generate a Windows GDB File Manager probe in a disposable directory.
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

"""Generate a Windows GDB File Manager probe in a disposable directory."""

import json
import sys
from pathlib import Path


def generate(directory, application):
    directory.mkdir(parents=True, exist_ok=True)
    script = [
        "set pagination off",
        "set confirm off",
        "set print thread-events off",
        "break C_WaitNextEvent",
        f"run {json.dumps(application.replace(chr(92), '/'))}",
        "disable 1",
        "python",
        "import gdb, struct",
        "from pathlib import Path",
        f"root = Path({str(directory)!r})",
        "inferior = gdb.selected_inferior()",
        "def ev(text): return int(gdb.parse_and_eval(text))",
        "def call(text, expected=0):",
        "    result = ev(text)",
        "    assert result == expected, (text, result, expected)",
        "def ptr(size): return ev(f'_NewPtr_flags({size},1,1)')",
        "def name(path):",
        "    text = path.drive + ':'.join(path.parts[1:])",
        "    data = text.encode('mac_roman')",
        "    address = ptr(256)",
        "    inferior.write_memory(address, bytes([len(data)]) + data)",
        "    return address",
        "ref = ptr(2)",
        "count = ptr(4)",
        "buffer = ptr(32)",
        "def openfork(path, resource=False):",
        "    call(f\"{'OpenRF' if resource else 'FSOpen'}((unsigned char *){name(path)},0,(short *){ref})\")",
        "    return struct.unpack('<h', inferior.read_memory(ref, 2))[0]",
        "def writefork(rn, data):",
        "    inferior.write_memory(buffer, data)",
        "    inferior.write_memory(count, struct.pack('<i',len(data)))",
        "    call(f'FSWrite({rn},(int *){count},(unsigned char *){buffer})')",
        "def checkempty(rn):",
        "    call(f'GetEOF({rn},(int *){count})')",
        "    assert bytes(inferior.read_memory(count,4)) == bytes(4)",
        "    inferior.write_memory(count, struct.pack('<i',1))",
        "    call(f'FSRead({rn},(int *){count},(unsigned char *){buffer})',-39)",
        "for style in ('osx','netatalk','percent','basilisk'):",
        "    folder = root / style",
        "    folder.mkdir()",
        "    path = folder / 'Only'",
        "    prefixes = {'osx':'._','netatalk':'.AppleDouble/','percent':'%','basilisk':'.finf/'}",
        "    metadata = folder / (prefixes[style]+'Only')",
        "    metadata.parent.mkdir(exist_ok=True)",
        "    if style == 'basilisk':",
        "        metadata.write_bytes(bytes(18))",
        "    else:",
        "        metadata.write_bytes(struct.pack('>II16sHIIIIII',0x51607,0x20000,b'Mac OS X        ',2,9,50,32,2,82,0)+bytes(32))",
        "    call(f'Create((unsigned char *){name(path)},0,0,0)',-48)",
        "    assert not path.exists()",
        "    rn = openfork(path)",
        "    checkempty(rn)",
        "    call(f'FSClose({rn})')",
        "    assert not path.exists()",
        "    rn = openfork(path,True)",
        "    checkempty(rn)",
        "    writefork(rn,b'RESOURCE')",
        "    call(f'FSClose({rn})')",
        "    assert not path.exists()",
        "    renamed = folder / 'Renamed'",
        "    call(f'Rename((unsigned char *){name(path)},0,(unsigned char *){name(renamed)})')",
        "    assert not renamed.exists()",
        "    rn = openfork(renamed)",
        "    checkempty(rn)",
        "    writefork(rn,b'DATA')",
        "    call(f'FSClose({rn})')",
        "    assert renamed.read_bytes() == b'DATA'",
        "    rn = openfork(renamed,True)",
        "    inferior.write_memory(count, struct.pack('<i',8))",
        "    call(f'FSRead({rn},(int *){count},(unsigned char *){buffer})')",
        "    assert bytes(inferior.read_memory(buffer,8)) == b'RESOURCE'",
        "    call(f'FSClose({rn})')",
        "    call(f'FSDelete((unsigned char *){name(renamed)},0)')",
        "    assert not renamed.exists()",
        "    created = folder / 'Created'",
        "    call(f'Create((unsigned char *){name(created)},0,0,0)')",
        "    assert created.read_bytes() == b''",
        "    assert not (folder / (prefixes[style]+'Created')).exists()",
        "    assert not (folder / '._Created').exists()",
        "    rn = openfork(created,True)",
        "    checkempty(rn)",
        "    call(f'FSClose({rn})')",
        "    assert not (folder / (prefixes[style]+'Created')).exists()",
        "    assert not (folder / '._Created').exists()",
        "    call(f'Create((unsigned char *){name(created)},0,0,0)',-48)",
        "    rn = openfork(created)",
        "    writefork(rn,b'CREATED')",
        "    call(f'FSClose({rn})')",
        "    assert created.read_bytes() == b'CREATED'",
        "    call(f'FSDelete((unsigned char *){name(created)},0)')",
        "    typed = folder / 'Typed'",
        "    call(f'Create((unsigned char *){name(typed)},0,0x54455354,0x54455854)')",
        "    assert typed.read_bytes() == b''",
        "    assert any((folder / (prefix+'Typed')).exists() for prefix in prefixes.values())",
        "    call(f'GetFInfo((unsigned char *){name(typed)},0,(FInfo *){buffer})')",
        "    assert bytes(inferior.read_memory(buffer,8)) == b'TEXTTEST'",
        "    call(f'FSDelete((unsigned char *){name(typed)},0)')",
        "    plain = folder / 'Plain'",
        "    plain.write_bytes(b'plain')",
        "    rn = openfork(plain,True)",
        "    checkempty(rn)",
        "    call(f'FSClose({rn})')",
        "    assert not (folder / (prefixes[style]+'Plain')).exists()",
        "    assert not (folder / '._Plain').exists()",
        "    call(f'FSDelete((unsigned char *){name(plain)},0)')",
        "    print('EMPTY_FORK_LIFECYCLE_OK',style)",
        "end",
        "quit 0",
    ]
    (directory / "probe.gdb").write_text("\n".join(script) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]).resolve(), sys.argv[2])
