# +===================================================================
#   File:      catalog_search_test.py
#
#   Summary:   Check catalog searches and Studio free-space arithmetic on Windows.
#
#   Classes:   None.
#
#   Functions: main (script entry point).
#
#   Origin:    Regression fixture for Kid Pix Studio's PBCatSearch startup loop;
#              parameter layout follows Inside Macintosh: Files, CSParam.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

"""Run real HFSDispatch catalog searches over an isolated host volume."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


BODY = r"""
import gdb, struct, runpy, sys
mem=gdb.selected_inferior()
offset=int(gdb.parse_and_eval('ROMlib_offset'))
base=int(gdb.parse_and_eval('$state'))
proc=int(gdb.parse_and_eval('$proc'))
def call(expr):
 return int(gdb.parse_and_eval('(int)'+expr))
def put(relative,data):
 mem.write_memory(base+relative,bytes(data))
def get(relative,size):
 return bytes(mem.read_memory(base+relative,size))
path=VOLUME.encode()+b'\0'
put(3072,path)
call('config_drive_prefs_load("",(char *)'+str(base+3072)+')')
saved=int(gdb.parse_and_eval('ROMlib_volumename'))
gdb.execute('set ROMlib_volumename=(char *)'+str(base+3072))
raw=bytearray(128)
struct.pack_into('>h',raw,22,127)
put(0,raw)
assert call('ufsPBMountVol((void *)'+str(base)+')')==0
gdb.execute('set ROMlib_volumename=(char *)'+str(saved))
vref=struct.unpack_from('>h',get(0,128),22)[0]
def catalog(directory,name,index=0):
 raw=bytearray(128)
 encoded=name.encode()
 put(2048,bytes([len(encoded)])+encoded)
 struct.pack_into('>Ih',raw,18,base+2048-offset,vref)
 struct.pack_into('>h',raw,28,index)
 struct.pack_into('>I',raw,48,directory)
 put(0,raw)
 assert call('PBGetCatInfo((void *)'+str(base)+',0)')==0
 return bytearray(get(0,128))
nested=struct.unpack_from('>I',catalog(2,'Nested'),48)[0]
deep=struct.unpack_from('>I',catalog(nested,'Deep'),48)[0]
expected={(2,b'Alpha'),(nested,b'Beta'),(deep,b'Gamma')}
for parent,name in expected:
 raw=catalog(parent,name.decode())
 raw[32:40]=b'TESTABCD'
 struct.pack_into('>I',raw,48,parent)
 put(0,raw)
 assert call('PBSetCatInfo((void *)'+str(base)+',0)')==0
def search(bits=8,limit=1,lower=None,upper=None,milliseconds=0):
 raw=bytearray(76)
 struct.pack_into('>h',raw,22,vref)
 struct.pack_into('>IiiIIIi',raw,24,base+512-offset,limit,0,bits,base+1024-offset,base+1280-offset,milliseconds)
 lo=bytearray(108) if lower is None else bytearray(lower)
 hi=bytearray(108) if upper is None else bytearray(upper)
 if bits&8 and lower is None:
  lo[32:40]=b'TESTABCD'; hi[32:40]=b'\xff'*8
 put(1024,lo);put(1280,hi)
 found=[]
 for turn in range(1000):
  put(0,raw)
  put(512,b'\xa5'*140)
  words=[0x207c,(base-offset)>>16,(base-offset)&65535,0x7018,0xa260,0x2f40,4,0x4e75]
  mem.write_memory(proc,struct.pack('>8H',*words))
  gdb.execute('call destroy_blocks((unsigned int)'+str(proc-offset)+',128)',to_string=True)
  result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))&65535
  assert result in (0,65536-39),result
  raw[:]=get(0,76)
  assert struct.unpack_from('>h',raw,16)[0]==(0 if result==0 else -39)
  actual=struct.unpack_from('>i',raw,32)[0]
  assert 0<=actual<=limit
  data=get(512,140)
  assert data[actual*70:]==b'\xa5'*(140-actual*70),('output overwrite',actual)
  for i in range(actual):
   record=data[i*70:(i+1)*70]
   assert struct.unpack_from('>h',record)[0]==vref
   found.append((struct.unpack_from('>I',record,2)[0],record[7:7+record[6]]))
  if result:
   break
 else:
  raise AssertionError('search failed to terminate')
 assert len(found)==len(set(found)),('duplicate matches',found)
 return set(found),raw
found,raw=search()
assert found==expected,(found,expected)
found,_=search(limit=2,milliseconds=1)
assert found==expected
assert search(milliseconds=-1)[0]==expected
lo=bytearray(108);hi=bytearray(108)
lo[32:40]=b'NONEABCD';hi[32:40]=b'\xff'*8
assert search(lower=lo,upper=hi)[0]==set()
put(1800,b'\x05alpha')
lo=bytearray(108);struct.pack_into('>I',lo,18,base+1800-offset)
assert search(bits=2,lower=lo)[0]=={(2,b'Alpha')}
put(1800,b'\x02am')
assert search(bits=1,lower=lo)[0]=={(deep,b'Gamma')}
lo=bytearray(108);hi=bytearray(108);lo[30]=hi[30]=16
directories=search(bits=4,lower=lo,upper=hi)[0]
assert (2,b'Nested') in directories and (nested,b'Deep') in directories
assert not (directories&expected)
assert search(bits=0x4008)[0].isdisjoint(expected)
# Reusing a completed cursor remains EOF and does not overwrite a sentinel.
put(0,raw);put(512,b'\xa5'*140)
assert call('PBCatSearch((void *)'+str(base)+',0)')==-39
assert get(512,140)==b'\xa5'*140
# An asynchronous completion receives the result in D0 and the block in A0.
flag=base+1900-offset
callback=struct.pack('>HI I HI HI H',0x23fc,1,flag,0x23c0,flag+4,0x23c8,flag+8,0x4e75)
mem.write_memory(proc,callback)
gdb.execute('call destroy_blocks((unsigned int)'+str(proc-offset)+',128)',to_string=True)
put(1900,bytes(12))
completed=bytearray(raw)
struct.pack_into('>I',completed,12,proc-offset)
put(0,completed)
assert call('PBCatSearch((void *)'+str(base)+',1)')==-39
assert get(1900,12)==struct.pack('>III',1,0xffffffd9,base-offset)
# Null pointers, wrapped extents, unsupported flags and forged continuation.
for field,value in ((24,0),(24,0xfffffff0),(28,0),(28,0xffffffff),(36,0x8000),(36,3),(40,0),(44,0),(40,0xfffffff0),(52,0x11223344)):
 broken=bytearray(raw)
 struct.pack_into('>I',broken,field,value)
 put(0,broken);put(512,b'\xa5'*140)
 assert call('PBCatSearch((void *)'+str(base)+',0)')==-50,(field,value)
 assert get(32,4)==bytes(4) and get(512,140)==b'\xa5'*140
print('CATALOG_SEARCH_OK nested=3 continuation=1 timed=1 unlimited=1 missing=1 names=2 directories=1 negate=1 async=1 malformed=10',flush=True)
# Execute the app-local large-volume helper with real 68K arithmetic. Preserve
# genuine low-space results; only byte counts above signed-long range saturate.
sys.path.insert(0,TOOLS)
helper=runpy.run_path(TOOLS+'/patch-kidpix-studio.py')['FREE_SPACE_HELPER']
for blocks,size in ((0,512),(100,512),(976,512),(977,512),(32768,65536),(65535,65536),(65535,0x7ffffe00)):
 code=bytes.fromhex('4e56ffb6 2d7c')+struct.pack('>I',size)+bytes.fromhex('ffb6 203c')+struct.pack('>I',blocks)+bytes.fromhex('4eba000a 4e5e 2f400004 4e75')+helper
 mem.write_memory(proc,code)
 gdb.execute('call destroy_blocks((unsigned int)'+str(proc-offset)+',128)',to_string=True)
 result=int(gdb.parse_and_eval('(long long)CToPascalCall($proc,4)'))&0xffffffff
 assert result==min(blocks*size,0x7fffffff),(blocks,size,result)
print('STUDIO_FREE_SPACE_OK low_space=4 large_volumes=3',flush=True)
"""


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romaw", type=Path, default=root / "windows-binaries/bin/ROMAW.exe")
    parser.add_argument("--gdb", type=Path, default=Path("C:/msys64/mingw64/bin/gdb.exe"))
    parser.add_argument("--application", type=Path, default=Path.home() / ".romaw/Apps/Browser")
    args = parser.parse_args()
    fixture = Path(tempfile.mkdtemp(prefix="catalog-search-", dir=root / "build"))
    volume = fixture / "Volume"
    (volume / "Nested/Deep").mkdir(parents=True)
    for name in ("Alpha", "Nested/Beta", "Nested/Deep/Gamma", "Other"):
        (volume / name).write_bytes(b"catalog fixture")
    body = BODY.replace("VOLUME", repr(volume.as_posix())).replace("TOOLS", repr((root / "tools").as_posix()))
    script = "\n".join(
        [
            "set pagination off",
            "set python print-stack full",
            "set confirm off",
            "set print thread-events off",
            "break ROMlib_desktop_run",
            f"run {json.dumps(args.application.as_posix())}",
            "disable 1",
            "call trap_install_handler(10,alinehandler,0)",
            "set $state=(unsigned char *)_NewPtr_flags(4096,1,1)",
            "set $proc=(unsigned char *)_NewPtr_flags(128,1,1)",
            "python",
            body,
            "end",
            "quit 0",
        ]
    )
    (fixture / "probe.gdb").write_text(script + "\n", encoding="utf-8")
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    result = subprocess.run(
        [str(args.gdb), "-q", "-batch", "-x", str(fixture / "probe.gdb"), str(args.romaw.resolve())],
        cwd=fixture,
        capture_output=True,
        text=True,
        timeout=60,
        **options,
    )
    output = result.stdout + result.stderr
    (fixture / "probe.log").write_text(output, encoding="utf-8")
    if result.returncode or "CATALOG_SEARCH_OK" not in output:
        raise SystemExit(f"Catalog search failed: {fixture / 'probe.log'}\n{output[-3000:]}")
    print(f"PASS: catalog search; {fixture / 'probe.log'}")


if __name__ == "__main__":
    main()
