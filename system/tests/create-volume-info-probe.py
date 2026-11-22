# +===================================================================
#   File:      create-volume-info-probe.py
#
#   Summary:   Generate staged GetVolInfo checks against the live host catalog.
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

"""Generate staged GetVolInfo checks against the live host catalog."""

import json
from pathlib import Path
import sys


def generate(directory, application):
    script = r"""
set pagination off
set confirm off
set print thread-events off
set environment DEFAULTFOLDER D:/Source/ROMAW
break C_WaitNextEvent
run APPLICATION
set $pb=(unsigned char *)_NewPtr_flags(1024,1,1)
python
import gdb,struct,os,tempfile
mem=gdb.selected_inferior()
offset=int(gdb.parse_and_eval('ROMlib_offset'))
pb=int(gdb.parse_and_eval('$pb'))
checked=0
romaw=None
for drive in range(1,128):
 volume=int(gdb.parse_and_eval('(void *)ROMlib_vcbbydrive('+str(drive)+')'))
 if not volume or int(gdb.parse_and_eval('((VCB *)'+str(volume)+')->vcbCTRef')): continue
 record=bytes(mem.read_memory(volume,82))
 ref=int.from_bytes(record[78:80],'big',signed=True)
 name=record[45:45+record[44]]
 raw=bytearray(1024)
 struct.pack_into('>Ih',raw,18,pb+256-offset,ref)
 struct.pack_into('>h',raw,28,-1)
 struct.pack_into('>I',raw,48,2)
 mem.write_memory(pb,bytes(raw))
 assert int(gdb.parse_and_eval('(int)PBGetCatInfo($pb,0)'))==0
 catalog=bytes(mem.read_memory(pb,108))
 if name==b'C':
  # Write and restore a root icon position through the actual File Manager.
  # The bytes must land in the profile record, not a drive-root sidecar.
  mem.write_memory(pb+512,b'C:/\0')
  metadata=int(gdb.parse_and_eval('(char *)romaw_root_metadata_path((char *)$pb+512)'))
  assert metadata
  path=gdb.Value(metadata).cast(gdb.lookup_type('char').pointer()).string()
  gdb.execute('call (void)free((void *)'+str(metadata)+')',to_string=True)
  expected=os.path.join(os.environ['USERPROFILE'],'.romaw','volume-metadata')
  assert os.path.normcase(os.path.commonpath((path,expected)))==os.path.normcase(expected)
  original=bytearray(catalog)
  struct.pack_into('>I',original,18,0)
  struct.pack_into('>h',original,28,0)
  struct.pack_into('>I',original,48,2)
  try:
   changed=bytearray(original);changed[42:46]=bytes.fromhex('12345678')
   mem.write_memory(pb,bytes(changed))
   assert int(gdb.parse_and_eval('(int)PBSetCatInfo($pb,0)'))==0
   raw=bytearray(1024)
   struct.pack_into('>h',raw,22,ref)
   struct.pack_into('>h',raw,28,-1)
   struct.pack_into('>I',raw,48,2)
   mem.write_memory(pb,bytes(raw))
   assert int(gdb.parse_and_eval('(int)PBGetCatInfo($pb,0)'))==0
   assert bytes(mem.read_memory(pb+42,4))==bytes.fromhex('12345678')
   with open(path,'rb') as stream: stored=stream.read()
   entries=int.from_bytes(stored[24:26],'big')
   finder=[struct.unpack_from('>III',stored,26+12*i) for i in range(entries) if int.from_bytes(stored[26+12*i:30+12*i],'big')==9]
   assert len(finder)==1 and stored[finder[0][1]+10:finder[0][1]+14]==bytes.fromhex('12345678')
  finally:
   mem.write_memory(pb,bytes(original))
   assert int(gdb.parse_and_eval('(int)PBSetCatInfo($pb,0)'))==0,'restore root metadata'
 backup=int(gdb.parse_and_eval('((VCB *)'+str(volume)+')->vcbVolBkUp'))
 gdb.execute('set ((VCB *)'+str(volume)+')->vcbVolBkUp=0x44332211',to_string=True)
 for function in ('PBGetVInfo','PBHGetVInfo'):
  raw=bytearray(1024)
  struct.pack_into('>Ih',raw,18,pb+256-offset,ref)
  mem.write_memory(pb,bytes(raw))
  assert int(gdb.parse_and_eval('(int)'+function+'($pb,0)'))==0
  info=bytes(mem.read_memory(pb,122))
  assert info[40:42]==catalog[52:54],(name,'root valence',info.hex(),catalog.hex())
  assert info[30:34]!=b'\0'*4,(name,'creation date')
  assert info[34:38]!=b'\0'*4 and info[34:38]!=bytes.fromhex('11223344'),(name,'modification date')
  if function=='PBHGetVInfo': assert info[72:76]==bytes.fromhex('11223344'),(name,'backup date',info.hex())
 gdb.execute('set ((VCB *)'+str(volume)+')->vcbVolBkUp='+str(backup),to_string=True)
 if name==b'ROMAW': romaw=ref
 checked+=1
assert checked and romaw is not None
# GetVolInfo with a WD refnum returns that directory's valence, preserving
# the WD reference. HGetVolInfo resolves the reference to the real volume.
raw=bytearray(1024)
struct.pack_into('>Ih',raw,18,pb+256-offset,romaw)
struct.pack_into('>I',raw,48,2)
raw[256:270]=b'\x0dSystem Folder'
mem.write_memory(pb,bytes(raw))
assert int(gdb.parse_and_eval('(int)PBGetCatInfo($pb,0)'))==0
catalog=bytes(mem.read_memory(pb,108))
directory=int.from_bytes(catalog[48:52],'big')
raw=bytearray(1024)
struct.pack_into('>h',raw,22,romaw)
struct.pack_into('>I',raw,28,0x74657374)
struct.pack_into('>I',raw,48,directory)
mem.write_memory(pb,bytes(raw))
assert int(gdb.parse_and_eval('(int)PBOpenWD($pb,0)'))==0
wd=int.from_bytes(bytes(mem.read_memory(pb+22,2)),'big',signed=True)
assert wd!=romaw
for function in ('PBGetVInfo','PBHGetVInfo'):
 raw=bytearray(1024)
 struct.pack_into('>Ih',raw,18,pb+256-offset,wd)
 mem.write_memory(pb,bytes(raw))
 assert int(gdb.parse_and_eval('(int)'+function+'($pb,0)'))==0
 info=bytes(mem.read_memory(pb,122))
 assert info[40:42]==catalog[52:54],('WD valence',function,info.hex(),catalog.hex())
 assert int.from_bytes(info[22:24],'big',signed=True)==(wd if function=='PBGetVInfo' else romaw)
raw=bytearray(1024)
struct.pack_into('>h',raw,22,wd)
mem.write_memory(pb,bytes(raw))
assert int(gdb.parse_and_eval('(int)PBCloseWD($pb,0)'))==0
# GetVolParms returns a contiguous caller-sized prefix of its version-2
# record, even for odd lengths or requests exceeding a signed 16-bit word.
full=None
for length in (20,*range(0,25),65536,2147483647):
 raw=bytearray(b'\xa5'*1024)
 raw[:128]=bytes(128)
 struct.pack_into('>h',raw,22,romaw)
 struct.pack_into('>Ii',raw,32,pb+256-offset,length)
 mem.write_memory(pb,bytes(raw))
 assert int(gdb.parse_and_eval('(int)PBHGetVolParms($pb,0)'))==0
 info=bytes(mem.read_memory(pb,1024))
 count=min(length,20)
 assert int.from_bytes(info[40:44],'big')==count,('GetVolParms actual count',length,info.hex())
 if full is None:
  full=info[256:276]
  assert full[:2]==b'\0\2'
  # bNoDeskItems (Files.h bit 20) makes Finder ignore Desktop Folder.
  assert not int.from_bytes(full[2:6],'big')&((1<<7)|(1<<12)|(1<<20)),('desktop items unsupported',full.hex())
  assert full[6:]==bytes(14),('version-2 defaults',full.hex())
 assert info[256:256+count]==full[:count],('GetVolParms prefix',length,info.hex())
 assert info[255]==0xa5 and info[256+count:280]==b'\xa5'*(24-count)
masked_null=(~int(gdb.parse_and_eval('syn68k_address_mask')))&0xffffffff
for length,buffer in ((-1,pb+256-offset),(-2147483648,pb+256-offset),(20,0),(0,0),(20,masked_null)):
 raw=bytearray(b'\xa5'*1024)
 raw[:128]=bytes(128)
 struct.pack_into('>h',raw,22,romaw)
 struct.pack_into('>Ii',raw,32,buffer,length)
 mem.write_memory(pb,bytes(raw))
 result=int(gdb.parse_and_eval('(int)PBHGetVolParms($pb,0)'))
 assert (result==0)==(length==0),(length,buffer,result)
 info=bytes(mem.read_memory(pb,1024))
 assert info[40:44]==bytes(4) and info[256:280]==b'\xa5'*24

# Mount an isolated host folder and change it outside ROMAW. Subsequent
# volume queries must refresh valence and modification time without Get Info.
with tempfile.TemporaryDirectory(prefix='volume-info-',dir=FIXTURE) as folder:
 path=folder.replace('\\','/').encode()
 mem.write_memory(pb+512,path+b'\0')
 mem.write_memory(pb+900,b'VolumeInfoProbe\0')
 gdb.execute('call ROMlib_volume_alias((char *)$pb+512,(char *)$pb+900)',to_string=True)
 volume=int(gdb.parse_and_eval('(void *)ROMlib_vcbbyunixname((char *)$pb+512)'))
 assert volume
 ref=int.from_bytes(bytes(mem.read_memory(volume+78,2)),'big',signed=True)
 for count in (0,3,2):
  if count==3:
   open(os.path.join(folder,'First'),'wb').close()
   open(os.path.join(folder,'Second'),'wb').close()
   os.mkdir(os.path.join(folder,'Child'))
   open(os.path.join(folder,'._First'),'wb').close()
   os.mkdir(os.path.join(folder,'.AppleDouble'))
  elif count==2: os.remove(os.path.join(folder,'First'))
  os.utime(folder,(1000000000+count,1000000000+count))
  raw=bytearray(1024)
  struct.pack_into('>h',raw,22,ref)
  mem.write_memory(pb,bytes(raw))
  assert int(gdb.parse_and_eval('(int)PBHGetVInfo($pb,0)'))==0
  info=bytes(mem.read_memory(pb,122))
  assert int.from_bytes(info[40:42],'big')==count,('external count',count,info.hex())
  raw=bytearray(1024)
  struct.pack_into('>Ih',raw,18,pb+256-offset,ref)
  struct.pack_into('>h',raw,28,-1)
  struct.pack_into('>I',raw,48,2)
  mem.write_memory(pb,bytes(raw))
  assert int(gdb.parse_and_eval('(int)PBGetCatInfo($pb,0)'))==0
  catalog=bytes(mem.read_memory(pb,108))
  assert info[34:38]==catalog[76:80],('external modification date',info.hex(),catalog.hex())
print('VOLUME_INFO_OK volumes=%d working_directory=1 external_changes=3 volparams_prefixes=28'%checked,flush=True)
end
call DisposPtr($pb)
quit 0
"""
    (directory / "probe.gdb").write_text(
        script.replace("APPLICATION", json.dumps(application.replace("\\", "/"))).replace(
            "FIXTURE", repr(str(directory.resolve()))
        )
    )


if __name__ == "__main__":
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    generate(directory, sys.argv[2])
