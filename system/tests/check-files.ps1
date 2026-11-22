# +===================================================================
#   File:      check-files.ps1
#
#   Summary:   Run files regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-Check-drive-icon {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Python = 'python',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    }
    $fixture = Join-Path $root ('build/drive-icon-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-drive-icon-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Drive icon probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Drive icon probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'DRIVE_ICON_CONTROL_OK') {
        throw "Drive icon probe failed; see $fixture"
    }
    Write-Output "Drive icon Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-drive-volume {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    $fixture = Join-Path $root ('build/drive-volume-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $quotedApp = ConvertTo-Json -Compress $application.Replace('\', '/')
    $quotedFixture = ConvertTo-Json -Compress $fixture.Replace('\', '/')
    $probe = Join-Path $PSScriptRoot 'drive_prefs_probe.py'
    $quotedProbe = ConvertTo-Json -Compress $probe.Replace('\', '/')
    $script = @"
set pagination off
set confirm off
set print thread-events off
break ROMlib_win_drive_preferences
commands
silent
return 0
continue
end
break C_WaitNextEvent
run $quotedApp
disable 1 2
python
FIXTURE = $quotedFixture
exec(compile(open($quotedProbe, 'rb').read(), $quotedProbe, 'exec'))
end
quit 0
"@
    $file = Join-Path $fixture 'probe.gdb'
    $script | Set-Content -LiteralPath $file
    $output = & $Gdb -batch -x $file $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'DOS_VOLUME_AND_APPS_DESKTOP_POLICY_OK') -or -not ($output -match 'HOST_VOLUME_RENAME_AND_DIRECTORY_OVERRIDE_OK')) {
        throw "Drive-volume regression failed; see $fixture/probe.log"
    }
    Write-Output 'DOS desktop suppression, selected-drive desktop support and Apps override passed.'
    Write-Output 'Host volume rename/persistence, Unicode labels, drive aliases and directory overrides passed.'
}

function Invoke-Check-file-icon {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Python = 'python',
        [string]$Executable = '',
        [string]$Applications = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Applications) {
        $Applications = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps'
    }
    $browser = Join-Path $Applications 'Browser'
    $macwrite = Join-Path $Applications 'MacWrite'
    if (-not (Test-Path -LiteralPath $browser) -or -not (Test-Path -LiteralPath $macwrite)) {
        throw 'The Browser and MacWrite probe applications are unavailable.'
    }
    $fixture = Join-Path $root ('build/file-icons-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-file-icon-probe.py') $fixture $browser $macwrite
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate the file icon probe.' }
    $output = & $Gdb -batch -x (Join-Path $fixture 'probe.gdb') $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'NATIVE_FILE_ICONS_OK')) {
        throw "Native file icon checks failed; see $fixture/probe.log"
    }
    Write-Output "Native application/document icons and resource-map preservation passed: $fixture/probe.log"
}

function Invoke-Check-finder-image-volume {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string[]]$Images = @('D:\Temp\Disk Tools.image', 'D:\Temp\RezillaBin1.1.dmg')
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build\finder-image-volumes.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $prefs = (Join-Path $fixture 'mounts.prefs').Replace('\', '/')
    $script = @'
set pagination off
set confirm off
set print thread-events off
set environment _NO_DEBUG_HEAP 1
break ROMlib_ecf_restore_mounts
commands
silent
set preferences = "PREFS"
continue
end
break romaw_main
run
set $volume = (FinderImageVolume*)calloc(1,sizeof(FinderImageVolume))
set $entry = (FinderImageEntry*)calloc(1,sizeof(FinderImageEntry))
set $path = (char**)calloc(1,sizeof(char*))
set $icon = (FinderIcon*)calloc(1,sizeof(FinderIcon))
set $associated = (FinderImageEntry*)calloc(1,sizeof(FinderImageEntry))
set $total_icons=0
if ROMlib_finder_image_volume(0,$volume) != 0
quit 1
end
'@
    $script = $script.Replace('PREFS', $prefs)
    foreach ($image in $Images) {
        $source = (Resolve-Path -LiteralPath $image).Path.Replace('\', '/')
        if ($source.Contains('"')) { throw 'Image path contains a GDB quote' }
        $script += "`nset `$error=ROMlib_mount_image_mode(`"$source`",-1)`n"
        $script += @'
if $error != 0
quit 1
end
if ROMlib_finder_image_volume(0,$volume) != 1
quit 1
end
set $reference=$volume->reference
if $volume->name[0] == 0 || $volume->source[0] == 0
quit 1
end
if ROMlib_finder_image_entry($reference,2,-1,$entry) != 0 || $entry->directory != 1 || $entry->id != 2
quit 1
end
set $index=1
set $files=0
set $directories=0
set $icons=0
while $index < 32768
set $error=ROMlib_finder_image_entry($reference,2,$index,$entry)
if $error == -43
loop_break
end
if $error != 0 || $entry->name[0] == 0 || $entry->parent != 2
quit 1
end
if $entry->directory
set $directories=$directories+1
set $directory=$entry->id
if ROMlib_finder_image_entry($reference,$directory,-1,$entry) != 0 || $entry->id != $directory
quit 1
end
else
set $files=$files+1
if $entry->type == 0x4150504c
if ROMlib_finder_image_find_application($reference,$entry->creator,$associated) != 1
quit 1
end
set $map=(short)C_CurResFile()
set $error=(short)C_ResError()
if ROMlib_finder_image_icon($reference,2,$entry->name,0,$entry->flags,$entry->type,$entry->creator,$icon) != 1 || ($icon->present & 1) == 0
quit 1
end
if (short)C_CurResFile() != $map || (short)C_ResError() != $error
quit 1
end
printf "ICON_OK|%s|planes=%d\n", $entry->name,$icon->present
set $icons=$icons+1
set $total_icons=$total_icons+1
end
if ROMlib_finder_image_path($reference,2,$entry->name,$path) != 0 || *$path == 0
quit 1
end
printf "FILE|%s\n", *$path
call (void)free(*$path)
end
set $index=$index+1
end
if $index == 1
quit 1
end
printf "CATALOG_OK|%s|files=%d|directories=%d|icons=%d\n", $volume->name,$files,$directories,$icons
if ROMlib_finder_image_unmount($reference) != 0
quit 1
end
if ROMlib_finder_image_volume(0,$volume) != 0
quit 1
end
if ROMlib_finder_image_entry($reference,2,1,$entry) != -35
quit 1
end
if ROMlib_finder_image_unmount($reference) != -35
quit 1
end
printf "UNMOUNT_OK\n"
'@
    }
    $script += "`nif `$total_icons == 0`nquit 1`nend`nprintf `"FINDER_IMAGE_VOLUMES_PASS\n`"`nquit 0`n"
    $commands = Join-Path $fixture 'test.gdb'
    [IO.File]::WriteAllText($commands, $script.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
    $log = Join-Path $fixture 'gdb.log'
    & $Gdb -q -batch -x $commands --args $ROMAW *> $log
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'FINDER_IMAGE_VOLUMES_PASS' -Quiet)) {
        Get-Content -LiteralPath $log -Tail 60
        throw "Finder image-volume checks failed: $log"
    }
    Get-Content -LiteralPath $log | Select-String 'ICON_OK|CATALOG_OK|UNMOUNT_OK|FINDER_IMAGE_VOLUMES_PASS'
}

function Invoke-Check-folder-name {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Python = 'python',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    }
    $fixture = Join-Path $root ('build/folder-name-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-folder-name-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Folder name probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Folder name probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'FOLDER_NAME_OK') {
        throw "Folder name probe failed; see $fixture"
    }
    Write-Output "Folder name Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-follow-alias {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Python = 'python',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    }
    $fixture = Join-Path $root ('build/follow-alias-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-follow-alias-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate FollowFinderAlias probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "FollowFinderAlias probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'FOLLOW_FINDER_ALIAS_OK') {
        throw "FollowFinderAlias probe failed; see $fixture"
    }
    Write-Output "FollowFinderAlias Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-image-import {
    param(
        [string]$Image,
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Image) { $Image = Join-Path $root 'build\image-desktop-fixture\DesktopMetadata.hfv' }
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries\bin\ROMAW.exe' }
    $work = Join-Path $root ('build\image-import-runtime.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $copy = Join-Path $work 'Destination.toast'
    Copy-Item -LiteralPath $Image -Destination $copy
(Get-Item -LiteralPath $copy).IsReadOnly = $false
    $template = @'
set pagination off
set confirm off
set print thread-events off
set environment _NO_DEBUG_HEAP 1
break ROMlib_ecf_restore_mounts
commands
silent
set preferences="PREFS"
continue
end
break romaw_main
run
python
import gdb
import struct
from pathlib import Path
def call(expression):
    return int(gdb.parse_and_eval(expression))
def check(expression, expected=0):
    result=call(expression)
    if result != expected:
        raise RuntimeError(expression+": "+str(result)+" != "+str(expected))
v=call('(void*)calloc(1,sizeof(FinderImageVolume))')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check('ROMlib_finder_image_writable(%d)'%ref,-44)
check('ROMlib_finder_image_create(%d,2,"Forbidden",0,(unsigned char*)%d,(unsigned int*)%d)'%(ref,v,v),-44)
check('ROMlib_finder_image_move(%d,2,"Tools",16,2)'%ref,-44)
check('ROMlib_finder_image_unmount(%d)'%ref)
check('ROMlib_mount_image_mode("IMAGE",1)')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check('ROMlib_finder_image_writable(%d)'%ref)
info=call('(void*)calloc(1,32)')
payload=call('(void*)malloc(65536)')
out=call('(void*)calloc(1,1024)')
inferior=gdb.selected_inferior()
metadata=b'APPLTest'+bytes([4])+bytes(22)+bytes([29])
inferior.write_memory(info,metadata)
check('ROMlib_finder_image_create(%d,2,"Copy Folder",1,(unsigned char*)%d,(unsigned int*)%d)'%(ref,info,out))
parent=call('*(unsigned int*)%d'%out)
check('ROMlib_finder_image_create(%d,%d,"Copy Payload",0,(unsigned char*)%d,(unsigned int*)%d)'%(ref,parent,info,out))
for fork in range(2):
    check('ROMlib_finder_image_open_fork(%d,%d,"Copy Payload",%d,(short*)%d)'%(ref,parent,fork,out))
    file=call('*(short*)%d'%out)
    if call('ROMlib_finder_image_unmount(%d)'%ref) == 0:
        raise RuntimeError('Unmount discarded an active copy')
    for count in (65536,19):
        inferior.write_memory(payload,bytes([0x36+fork])*count)
        check('ROMlib_finder_image_write(%d,(void*)%d,%d)'%(file,payload,count))
    check('ROMlib_finder_image_close_fork(%d)'%file)
check('ROMlib_finder_image_rename(%d,%d,"Copy Payload","Published")'%(ref,parent))
check('ROMlib_finder_image_flush(%d)'%ref)
check('ROMlib_finder_image_unmount(%d)'%ref)
reader=call('(void*)romaw_images_open_direct_utf8("IMAGE",(char*)%d,1024)'%payload)
if not reader:
    raise RuntimeError('Cannot reopen the published image directly')
path=call('(void*)calloc(1,32768)')
check('romaw_images_guest_path_utf8((romaw_image_set*)%d,0,":Copy Folder:Published",(char*)%d,32768,(char*)%d,1024)'%(reader,path,payload),1)
for fork in range(2):
    for offset,count in ((0,65536),(65536,19)):
        check('romaw_images_read_fork_utf8((romaw_image_set*)%d,(char*)%d,%d,%d,(void*)%d,%d,(char*)%d,1024)'%(reader,path,fork,offset,payload,count,out),1)
        if bytes(inferior.read_memory(payload,count)) != bytes([0x36+fork])*count:
            raise RuntimeError('Published fork data differs from the source')
gdb.parse_and_eval('romaw_images_close((romaw_image_set*)%d,0)'%reader)
check('ROMlib_mount_image_mode("IMAGE",1)')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check('ROMlib_finder_image_lookup(%d,2,"Copy Folder",(unsigned int*)%d)'%(ref,out))
parent=call('*(unsigned int*)%d'%out)
entry=call('(void*)calloc(1,sizeof(FinderImageEntry))')
check('ROMlib_finder_image_entry(%d,%d,-1,(FinderImageEntry*)%d)'%(ref,parent,entry))
check('((FinderImageEntry*)%d)->children'%entry,1)
check('ROMlib_finder_image_entry(%d,%d,1,(FinderImageEntry*)%d)'%(ref,parent,entry))
check('((FinderImageEntry*)%d)->data_size'%entry,65555)
check('((FinderImageEntry*)%d)->resource_size'%entry,65555)
for field in ('data_allocated','resource_allocated'):
    allocated=call('((FinderImageEntry*)%d)->%s'%(entry,field))
    if allocated <= 65555 or allocated % 512:
        raise RuntimeError("Mounted fork allocation did not reflect the image allocation blocks")
check('((FinderImageEntry*)%d)->type'%entry,0x4150504c)
check('((FinderImageEntry*)%d)->creator'%entry,0x54657374)
file_id=call('((FinderImageEntry*)%d)->id'%entry)
check('ROMlib_finder_image_create(%d,2,"Move Folder",1,(unsigned char*)%d,(unsigned int*)%d)'%(ref,info,out))
destination=call('*(unsigned int*)%d'%out)
check('ROMlib_finder_image_move(%d,%d,"Published",%d,%d)'%(ref,parent,file_id+1,destination),-43)
check('ROMlib_finder_image_move(%d,%d,"Published",%d,%d)'%(ref,parent,file_id,destination))
check('ROMlib_finder_image_lookup(%d,%d,"Published",0)'%(ref,parent),-43)
check('ROMlib_finder_image_lookup(%d,%d,"Published",(unsigned int*)%d)'%(ref,destination,out))
check('*(unsigned int*)%d'%out,file_id)
for fork in range(2):
    check('ROMlib_finder_image_open_source_fork(%d,%d,"Published",%d,(short*)%d)'%(ref,destination,fork,out))
    file=call('*(short*)%d'%out)
    check('ROMlib_finder_image_check_mutable(%d,%d,"Published",%d)'%(ref,destination,file_id),-47)
    for count in (65536,19):
        check('ROMlib_finder_image_read(%d,(void*)%d,%d)'%(file,payload,count))
        if bytes(inferior.read_memory(payload,count)) != bytes([0x36+fork])*count:
            raise RuntimeError('Moving changed fork contents')
    check('ROMlib_finder_image_close_fork(%d)'%file)
check('ROMlib_finder_image_lock(%d,%d,"Published",%d,1)'%(ref,destination,file_id))
check('ROMlib_finder_image_check_mutable(%d,%d,"Published",%d)'%(ref,destination,file_id),-45)
check('ROMlib_finder_image_lock(%d,%d,"Published",%d,0)'%(ref,destination,file_id))
check('ROMlib_finder_image_delete(%d,%d,"Published")'%(ref,destination))
check('ROMlib_finder_image_move(%d,2,"Copy Folder",%d,%d)'%(ref,parent,destination))
check('ROMlib_finder_image_delete(%d,%d,"Copy Folder")'%(ref,destination))
check('ROMlib_finder_image_delete(%d,2,"Move Folder")'%ref)
check('ROMlib_finder_image_create(%d,2,"Memory App",0,(unsigned char*)%d,(unsigned int*)%d)'%(ref,info,out))
app=call('*(unsigned int*)%d'%out)
comment_key=call('(void*)calloc(1,38)')
comment_count=call('(void*)calloc(1,sizeof(size_t))')
comment_tag=call('(void*)calloc(1,4)')
def set_comment(parent,name,id,text,expected=0):
    inferior.write_memory(payload,text)
    check('ROMlib_finder_image_set_comment(%d,%d,"%s",%d,(void*)%d,%d)'%(ref,parent,name,id,payload,len(text)),expected)
def read_comment(id,text):
    inferior.write_memory(comment_key,bytes([5,3])+struct.pack(">I",id)+bytes(32))
    gdb.parse_and_eval('*(size_t*)%d=200'%comment_count)
    check('ROMlib_finder_image_desktop(%d,1,(unsigned char*)%d,0,(void*)%d,(size_t*)%d,(unsigned int*)%d)'%
          (ref,comment_key,payload,comment_count,comment_tag),0 if text is not None else -413)
    if text is not None and bytes(inferior.read_memory(payload,len(text)+1)) != bytes([len(text)])+text:
        raise RuntimeError('Desktop DB comment changed')
check('ROMlib_finder_image_lookup(%d,2,"Document",(unsigned int*)%d)'%(ref,out))
document=call('*(unsigned int*)%d'%out)
read_comment(document,b'source comment')
check('ROMlib_finder_image_lookup(%d,2,"Tools",(unsigned int*)%d)'%(ref,out))
tools=call('*(unsigned int*)%d'%out)
set_comment(2,'Tools',tools,b'Folder notes')
set_comment(2,'Document',document,b'')
read_comment(document,None)
set_comment(2,'Memory App',document,b'Wrong file',-43)
set_comment(2,'Memory App',app,b'A'*200,-50)
set_comment(2,'Memory App',app,b'New comment')
set_comment(2,'Memory App',app,b'Caf\x8e\rSecond line')
read_comment(app,b'Caf\x8e\rSecond line')
records=struct.pack(">I",10)+struct.pack(">HII",0x0080,1050000,512000)+struct.pack(">I",4)+b"keep"
header=struct.pack(">IIII",256,256+len(records),len(records),70)
types=struct.pack(">H4sHH4sHH",1,b"SIZE",0,18,b"vers",0,30)
refs=struct.pack(">hHIIhHII",-1,65535,0,0,1,65535,14,0)
fork=header+bytes(240)+records+header+bytes(8)+struct.pack(">HH",28,70)+types+refs
host=Path("HOST_APP")
host.write_bytes(b"Original data")
host_header=struct.pack(">II",0x00051607,0x00020000)+bytes(16)+struct.pack(">HIIIIII",2,9,50,32,2,82,len(fork))
host.with_name("._"+host.name).write_bytes(host_header+metadata+fork)
inferior.write_memory(payload,fork)
check('ROMlib_finder_image_open_fork(%d,2,"Memory App",1,(short*)%d)'%(ref,out))
file=call('*(short*)%d'%out)
check('ROMlib_finder_image_write(%d,(void*)%d,%d)'%(file,payload,len(fork)))
check('ROMlib_finder_image_close_fork(%d)'%file)
sizes=call('(void*)calloc(2,sizeof(FinderApplicationMemory))')
update=sizes+call('sizeof(FinderApplicationMemory)')
def mem(ptr, field):
    return '((FinderApplicationMemory*)%d)->%s'%(ptr,field)
def memory(write):
    return 'ROMlib_finder_image_memory(%d,2,"Memory App",%d,(FinderApplicationMemory*)%d,(FinderApplicationMemory*)%d)'%(ref,app,sizes,update if write else 0)
check(memory(False))
check(mem(sizes,'suggested'),1050000)
check(mem(sizes,'preferred'),1050000)
check(mem(sizes,'minimum'),512000)
gdb.parse_and_eval(mem(update,'minimum')+'=700000')
gdb.parse_and_eval(mem(update,'preferred')+'=2000000')
check(memory(True))
check(mem(sizes,'suggested'),1050000)
check(mem(sizes,'minimum'),700000)
gdb.parse_and_eval(mem(sizes,'minimum')+'=699999')
check(memory(True),-47)
check(memory(False))
gdb.parse_and_eval(mem(update,'preferred')+'=2300000')
check(memory(True))
check('ROMlib_finder_image_version(%d,2,"Memory App",(void*)%d,16,(size_t*)%d)'%(ref,payload,out),1)
if bytes(inferior.read_memory(payload,4)) != b"keep":
    raise RuntimeError("SIZE change did not preserve another resource")
art=call('(void*)calloc(1,sizeof(FinderIcon))')
rendered=call('(void*)calloc(1,sizeof(FinderIcon))')
inferior.write_memory(art,bytes([0x55])*128+bytes([0xff])*128)
gdb.parse_and_eval('((FinderIcon*)%d)->present=1'%art)
def set_icon(name,id,source,expected=0):
    check('ROMlib_finder_image_set_custom_icon(%d,2,"%s",%d,(FinderIcon*)%d)'%(ref,name,id,source),expected)
def read_icon(name,directory,expected):
    check('ROMlib_finder_image_custom_icon(%d,%d,"%s",%d,(FinderIcon*)%d)'%
          (ref,tools if directory else 2,name,directory,rendered),expected)
    if expected and bytes(inferior.read_memory(rendered,256)) != bytes(inferior.read_memory(art,256)):
        raise RuntimeError("Custom icon artwork changed")
set_icon('Memory App',app,art)
set_icon('Tools',tools,art)
read_icon('Memory App',0,1)
read_icon('Tools',1,1)
check('ROMlib_finder_image_lock(%d,2,"Memory App",%d,1)'%(ref,app))
check(memory(True),-45)
set_comment(2,'Memory App',app,b'Locked',-45)
set_icon('Memory App',app,0,-45)
check('ROMlib_finder_image_unmount(%d)'%ref)
check('ROMlib_mount_image_mode("IMAGE",-1)')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check(memory(False))
check(mem(sizes,'suggested'),1050000)
check(mem(sizes,'preferred'),2300000)
check(mem(sizes,'minimum'),700000)
check(memory(True),-44)
read_comment(app,b'Caf\x8e\rSecond line')
read_comment(tools,b'Folder notes')
read_comment(document,None)
check('ROMlib_finder_image_application(%d,0x4d747374,(FinderImageEntry*)%d)'%(ref,entry),1)
set_comment(2,'Tools',tools,b'Read only',-44)
set_icon('Tools',tools,0,-44)
read_icon('Memory App',0,1)
read_icon('Tools',1,1)
check('ROMlib_finder_image_unmount(%d)'%ref)
check('ROMlib_mount_image_mode("IMAGE",1)')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check('ROMlib_finder_image_lock(%d,2,"Memory App",%d,0)'%(ref,app))
set_icon('Memory App',app,0)
set_icon('Tools',tools,0)
read_icon('Memory App',0,0)
read_icon('Tools',1,0)
for name in ('Desktop DB','Desktop DF'):
    check('ROMlib_finder_image_rename(%d,2,"%s","Saved %s")'%(ref,name,name))
set_comment(2,'Tools',tools,b'First comment')
check('ROMlib_finder_image_rename(%d,2,"Tools","Renamed Tools")'%ref)
check('ROMlib_finder_image_lookup(%d,2,"Tools",0)'%ref,-43)
check('ROMlib_finder_image_lookup(%d,2,"Desktop DF",0)'%ref)
check('ROMlib_finder_image_unmount(%d)'%ref)
check('ROMlib_mount_image_mode("IMAGE",-1)')
check('ROMlib_finder_image_volume(0,(FinderImageVolume*)%d)'%v,1)
ref=call('((FinderImageVolume*)%d)->reference'%v)
check('ROMlib_finder_image_lookup(%d,2,"Renamed Tools",(unsigned int*)%d)'%(ref,out))
check('*(unsigned int*)%d'%out,tools)
read_comment(tools,b'First comment')
read_icon('Memory App',0,0)
read_icon('Renamed Tools',1,0)
check(memory(False))
check(mem(sizes,'preferred'),2300000)
check('ROMlib_finder_image_unmount(%d)'%ref)
host_path=str(host).replace("\\","/").encode("ascii")+bytes([0])
inferior.write_memory(path,host_path)
def host_memory(write):
    return 'ROMlib_finder_host_memory((char*)%d,(FinderApplicationMemory*)%d,(FinderApplicationMemory*)%d)'%(path,sizes,update if write else 0)
check(host_memory(False))
check(mem(sizes,'minimum'),512000)
check(mem(sizes,'suggested'),1050000)
check(host_memory(True))
check(host_memory(False))
check(mem(sizes,'suggested'),1050000)
check(mem(sizes,'preferred'),2300000)
check(mem(sizes,'minimum'),700000)
if host.read_bytes() != b"Original data":
    raise RuntimeError("Editing the host resource fork changed its data fork")
print('MOUNTED_COPY_PASS')
end
quit 0
'@
    $commands = $template.Replace('PREFS', (Join-Path $work 'images.prefs').Replace('\', '/')).Replace('IMAGE', $copy.Replace('\', '/')).Replace('HOST_APP', (Join-Path $work 'Host Memory App').Replace('\', '/'))
    $script = Join-Path $work 'test.gdb'
    $log = Join-Path $work 'test.log'
    [IO.File]::WriteAllText($script, $commands.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
    & $Gdb -q -batch -x $script --args $ROMAW -mount-ro $copy *> $log
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'MOUNTED_COPY_PASS' -Quiet)) {
        Get-Content -LiteralPath $log -Tail 40
        throw "Mounted image import failed ($log)"
    }
    Write-Output 'PASS: mounted file operations, remounted HFS comments and custom icons, and Get Info SIZE overrides'
}

function Invoke-Check-image-session {
    param(
        [string]$Image = 'D:\Temp\importfl-1.2.2.dsk',
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build\image-session.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $copy = Join-Path $fixture 'optical.toast'
    Copy-Item -LiteralPath $Image -Destination $copy
(Get-Item -LiteralPath $copy).IsReadOnly = $false
    $prefs = (Join-Path $fixture 'images.prefs').Replace('\', '/')
    $source = $copy.Replace('\', '/')
    $prefix = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_ecf_restore_mounts
commands
silent
set preferences = "PREFS"
continue
end
break romaw_main
run
'@
    $access = @'
set $result=ROMlib_mount_image_mode("IMAGE",0)
if $result != 0
quit 1
end
set $v=ROMlib_vcbbyvrn(ROMlib_nextvrn)
set $drive=ROMlib_dqbydrive((short)(($v->vcbDrvNum << 8) | ((unsigned short)$v->vcbDrvNum >> 8)))
if (((unsigned int)$drive->flags) & 128) == 0
quit 1
end
set $old_volume=$v
set $result=SetVol(0,ROMlib_nextvrn)
if $result != 0
quit 1
end
set $result=UnmountVol(0,ROMlib_nextvrn)
if $result != 0
quit 1
end
set $v=ROMlib_findvcb(0,0,0,0)
if $v == 0 || $v == $old_volume
quit 1
end
set $result=ROMlib_mount_image_mode("IMAGE",1)
if $result != 0
quit 1
end
set $v=ROMlib_vcbbyvrn(ROMlib_nextvrn)
set $drive=ROMlib_dqbydrive((short)(($v->vcbDrvNum << 8) | ((unsigned short)$v->vcbDrvNum >> 8)))
if (((unsigned int)$drive->flags) & 128) != 0
quit 1
end
printf "OPTICAL_ACCESS_OK\n"
quit 0
'@
    $restore = @'
set $v=ROMlib_vcbbyvrn(ROMlib_nextvrn)
set $drive=ROMlib_dqbydrive((short)(($v->vcbDrvNum << 8) | ((unsigned short)$v->vcbDrvNum >> 8)))
if (((unsigned int)$drive->flags) & 128) == 0
quit 1
end
set $result=UnmountVol(0,ROMlib_nextvrn)
if $result != 0
quit 1
end
set $result=ROMlib_mount_image_mode("IMAGE",-1)
if $result != 0
quit 1
end
set $result=Eject(0,ROMlib_nextvrn)
if $result != 0
quit 1
end
printf "RESTORE_UNLOAD_OK\n"
quit 0
'@
    $script = Join-Path $fixture 'test.gdb'
    foreach ($case in @(@($access, 'OPTICAL_ACCESS_OK', 'access'), @($restore, 'RESTORE_UNLOAD_OK', 'restore'))) {
        if ($case[2] -eq 'restore') {
            if ([IO.File]::ReadAllText($prefs) -notmatch 'Mode0=-mount-rw') { throw 'Explicit write mode was not saved' }
        (Get-Item -LiteralPath $copy).IsReadOnly = $true
        }
        $commands = ($prefix + "`n" + $case[0] + "`n").Replace('PREFS', $prefs).Replace('IMAGE', $source)
        [IO.File]::WriteAllText($script, $commands)
        Push-Location $fixture
        try { $output = & $Gdb -q -batch -x $script $ROMAW 2>&1 | Out-String; $status = $LASTEXITCODE }
        finally { Pop-Location; (Get-Item -LiteralPath $copy).IsReadOnly = $false }
        [IO.File]::WriteAllText((Join-Path $fixture ($case[2] + '.log')), $output)
        if ($status -ne 0 -or $output -notmatch $case[1]) { throw "Image session check failed: $output" }
    }
    if ([IO.File]::ReadAllText($prefs) -notmatch 'Count=0') { throw 'Put Away retained the selection' }
    Write-Output 'PASS: optical access defaults/override, host read-only attributes, cross-process restoration, UnmountVol, remount and Eject'
}

function Invoke-Check-image-startup {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $check = Join-Path $workspace 'build\image-startup-check'
    New-Item -ItemType Directory -Force $check | Out-Null
    $script = Join-Path $check 'test.gdb'
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set $events = 0
break C_GetNextEvent
commands
silent
set $events = $events + 1
continue
end
break ROMlib_mount_image_dialog
commands
silent
if $events == 0 || ROMlib_pending_image_dialog != 0
quit 1
end
printf "IMAGE_STARTUP_OK\n"
quit 0
end
break exit
commands
silent
bt 8
quit 1
end
run -mount-image
printf "IMAGE_STARTUP_ENDED_WITHOUT_PICKER\n"
quit 1
'@
    [IO.File]::WriteAllText($script, $commands)
    Push-Location $check
    try {
        $output = & $Gdb -q -batch -x $script $ROMAW 2>&1 | Out-String
        $status = $LASTEXITCODE
    }
    finally { Pop-Location }
    [IO.File]::WriteAllText((Join-Path $check 'gdb.log'), $output)
    if ($status -ne 0 -or $output -notmatch 'IMAGE_STARTUP_OK') { throw "Image startup failed: $output" }
    Write-Output 'Configured startup app launches from the mapped Apps volume; image picker waits for its event loop.'
}

function Invoke-Check-volume-custom-icon {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries\bin\ROMAW.exe' }
    $work = Join-Path $root ('build\volume-custom-icons.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $cases = @(
        @('build\image-desktop-fixture\DesktopMetadata.hfv', 1, 32),
        @('build\image-desktop-fixture\VolumeDataIcon.hfv', 1, 32),
        @('build\image-desktop-fixture\VolumeDesktopIcon.hfv', 1, 32),
        @('build\image-desktop-fixture\VolumeInvalidIcon.hfv', 0, -1)
    )
    foreach ($image in @('D:\Temp\MacOS9HD.hfv', 'D:\Temp\System_7.1_GM.cdr')) {
        if (Test-Path -LiteralPath $image) { $cases += , @($image, 1, -1) }
    }
    $template = @'
set pagination off
set confirm off
set print thread-events off
set environment _NO_DEBUG_HEAP 1
break ROMlib_ecf_restore_mounts
commands
silent
set preferences="PREFS"
continue
end
break romaw_main
run
set $volume=(FinderImageVolume*)calloc(1,sizeof(FinderImageVolume))
set $icon=(FinderIcon*)calloc(1,sizeof(FinderIcon))
if ROMlib_finder_image_volume(0,$volume) != 1
quit 1
end
set $map=(short)C_CurResFile()
set $error=(short)C_ResError()
if ROMlib_finder_image_volume_icon($volume->reference,$icon) != EXPECTED
quit 1
end
if ($icon->present != 0 && $icon->present != 63) || (EXPECTED == 0 && $icon->present != 0) || (EXPECTED == 1 && $icon->present != 63)
quit 1
end
if MARKER >= 0 && ($icon->mono[0] != MARKER || $icon->color8[1023] != MARKER+3 || $icon->small8[255] != MARKER+5)
quit 1
end
if (short)C_CurResFile() != $map || (short)C_ResError() != $error
quit 1
end
if ROMlib_finder_image_volume_icon(-30000,$icon) != 0 || $icon->present != 0
quit 1
end
if ROMlib_finder_image_unmount($volume->reference) != 0
quit 1
end
printf "VOLUME_CUSTOM_ICON_PASS\n"
quit 0
'@
    $number = 0
    foreach ($case in $cases) {
        $image = $case[0]
        if (-not [IO.Path]::IsPathRooted($image)) { $image = Join-Path $root $image }
        $image = (Resolve-Path -LiteralPath $image).Path
        $before = Get-Item -LiteralPath $image
        $length = $before.Length
        $modified = $before.LastWriteTimeUtc
        $hash = if ($length -le 2097152) { (Get-FileHash -LiteralPath $image).Hash } else { $null }
        $number++
        $commands = Join-Path $work "$number.gdb"
        $log = Join-Path $work "$number.log"
        $script = $template.Replace('PREFS', (Join-Path $work "$number.prefs").Replace('\', '/')).Replace('EXPECTED', [string]$case[1]).Replace('MARKER', [string]$case[2])
        [IO.File]::WriteAllText($commands, $script.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
        & $Gdb -q -batch -x $commands --args $ROMAW -mount-ro $image *> $log
        if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'VOLUME_CUSTOM_ICON_PASS' -Quiet)) {
            Get-Content -LiteralPath $log -Tail 30
            throw "Volume custom icon test failed: $image ($log)"
        }
        $after = Get-Item -LiteralPath $image
        if ($after.Length -ne $length -or $after.LastWriteTimeUtc -ne $modified -or ($hash -and (Get-FileHash -LiteralPath $image).Hash -ne $hash)) {
            throw "Custom icon lookup changed its source: $image"
        }
        Write-Output "PASS: volume custom icon/fallback, preserved Resource Manager state and unload: $image"
    }
}

function Invoke-Check-volume-info {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Python = 'python',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    }
    $fixture = Join-Path $root ('build/volume-info-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-volume-info-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Volume info probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Volume info probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'VOLUME_INFO_OK') {
        throw "Volume info probe failed; see $fixture"
    }
    Write-Output "Volume info Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-volume-startup {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\volume-startup.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $savedDefaultFolder = $env:DEFAULTFOLDER
    try {
        # A relative host path exists, but cannot resolve to a mounted volume.
        # Previously startup dereferenced the null VCB returned for this folder.
        $env:DEFAULTFOLDER = '.'
        $process = Start-Process -FilePath $ROMAW -ArgumentList '-debug trace' -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id
            throw 'An unmounted default folder did not exit promptly'
        }
        if ($process.ExitCode -ne 1) {
            throw "Expected controlled exit 1, received $($process.ExitCode)"
        }
        $errors = [IO.File]::ReadAllText((Join-Path $fixture 'stderr.txt'))
        if (-not $errors.Contains("Unable to find mounted volume for default folder '.'")) {
            throw 'Missing diagnostic for the unmounted default folder'
        }
        if ($errors.Contains('win_exception')) {
            throw 'Startup raised a Windows exception'
        }
        Write-Output 'PASS: unmounted default folder exits cleanly with a diagnostic'
    }
    finally {
        $env:DEFAULTFOLDER = $savedDefaultFolder
    }
}

function Invoke-Check-image-desktop {
    param(
        [string]$Image,
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [switch]$CheckLayout
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Image) { $Image = Join-Path $root 'build\image-desktop-fixture\DesktopMetadata.hfv' }
    if ($CheckLayout -and -not $PSBoundParameters.ContainsKey('Image')) {
        $Image = Join-Path $root 'build\image-desktop-fixture\LayoutMetadata.hfv'
    }
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries\bin\ROMAW.exe' }
    $source = (Resolve-Path -LiteralPath $Image).Path
    $before = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $fixture = Join-Path $root ('build\image-desktop.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $script = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_ecf_restore_mounts
commands
silent
set preferences = "PREFS"
continue
end
break romaw_main
run
set $volume=(FinderImageVolume*)calloc(1,sizeof(FinderImageVolume))
set $entry=(FinderImageEntry*)calloc(1,sizeof(FinderImageEntry))
set $icon=(FinderIcon*)calloc(1,sizeof(FinderIcon))
set $key=(unsigned char*)calloc(1,38)
set $comment=(unsigned char*)calloc(1,200)
set $size=(size_t*)calloc(1,sizeof(size_t))
set $tag=(unsigned int*)calloc(1,4)
if !ROMlib_finder_image_volume(0,$volume)
quit 1
end
set $reference=$volume->reference
if ROMlib_finder_image_application($reference,0x4d747374,$entry) != 1 || $entry->parent == 2 || (int)strcmp($entry->name,"Application")
quit 1
end
if ROMlib_finder_image_icon($reference,2,"Document",0,0,0x54455854,0x4d747374,$icon) != 1 || $icon->present != 1 || $icon->mono[0] != 0x55 || $icon->mono[255] != 0x55
quit 1
end
set $index=1
while $index < 50
if ROMlib_finder_image_entry($reference,2,$index,$entry) != 0
quit 1
end
if $entry->type == 0x54455854
loop_break
end
set $index=$index+1
end
if $index == 50
quit 1
end
set $key[0]=5
set $key[1]=3
set $key[2]=$entry->id>>24
set $key[3]=$entry->id>>16
set $key[4]=$entry->id>>8
set $key[5]=$entry->id
set *$size=200
if ROMlib_finder_image_desktop($reference,1,$key,0,$comment,$size,$tag) != 0 || $comment[0] != 14 || (int)memcmp($comment+1,"source comment",14)
quit 1
end
set $dt=(DTPBPtr)_NewPtr_flags(sizeof(*(DTPBPtr)0),1,1)
set $name=(unsigned char*)_NewPtr_flags(256,1,1)
set $raw=(unsigned short)$reference
set $dt->ioVRefNum=(short)(($raw>>8)|($raw<<8))
if PBDTOpenInform($dt) != 0
quit 1
end
set $guest=(unsigned int)((unsigned long long)$name-ROMlib_offset)
set $dt->ioNamePtr.pp=($guest>>24)|(($guest>>8)&0xff00)|(($guest<<8)&0xff0000)|($guest<<24)
set $dt->ioFileCreator=0x7473744d
if PBDTGetAPPL($dt,0) != 0 || $name[0] != 11 || (int)memcmp($name+1,"Application",11)
quit 1
end
if PBDTCloseDown($dt) != 0
quit 1
end
printf "IMAGE_DESKTOP_METADATA_PASS\n"
quit 0
'@
    if ($CheckLayout) {
        $layout = @'
if ROMlib_finder_image_entry($reference,2,-1,$entry) != 0 || $entry->finder_info[1] != 80 || $entry->finder_info[3] != 100 || $entry->finder_info[4] != 1 || $entry->finder_info[5] != 24 || $entry->finder_info[6] != 2 || $entry->finder_info[7] != 28
quit 1
end
set $index=1
set $found=0
while ROMlib_finder_image_entry($reference,2,$index,$entry) == 0
if (int)strcmp($entry->name,"Document") == 0
if ($entry->flags & 256) == 0 || $entry->finder_info[11] != 60 || $entry->finder_info[13] != 200
quit 1
end
set $found=$found+1
end
if (int)strcmp($entry->name,"Tools") == 0
if $entry->finder_info[1] != 120 || $entry->finder_info[3] != 150 || $entry->finder_info[4] != 1 || $entry->finder_info[5] != 24 || $entry->finder_info[6] != 1 || $entry->finder_info[7] != 174 || $entry->finder_info[11] != 60 || $entry->finder_info[12] != 1 || $entry->finder_info[13] != 64
quit 1
end
set $found=$found+1
end
set $index=$index+1
end
if $found != 2
quit 1
end
printf "IMAGE_LAYOUT_PASS\n"
'@
        $script = $script.Replace('printf "IMAGE_DESKTOP_METADATA_PASS', $layout + "`n" + 'printf "IMAGE_DESKTOP_METADATA_PASS')
    }
    $script = $script.Replace('PREFS', (Join-Path $fixture 'empty.prefs').Replace('\', '/'))
    $commands = Join-Path $fixture 'desktop.gdb'
    [IO.File]::WriteAllText($commands, $script.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
    $log = Join-Path $fixture 'gdb.log'
    & $Gdb -q -batch -x $commands --args $ROMAW -mount-ro $source *> $log
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'IMAGE_DESKTOP_METADATA_PASS' -Quiet)) {
        Get-Content -LiteralPath $log -Tail 35
        throw "Mounted Desktop metadata check failed: $log"
    }
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $before) { throw 'The source image changed' }
    if ($CheckLayout -and -not (Select-String -LiteralPath $log -SimpleMatch 'IMAGE_LAYOUT_PASS' -Quiet)) {
        throw 'Mounted folder layout was not checked'
    }
    Write-Output 'PASS: source Desktop DB/DF icon, nested application, comment, Toolbox lookup and unchanged image'
}

function Invoke-Check-image-lock {
    param(
        [string]$Image,
        [string]$ROMAW,
        [string]$Python = 'python'
    )
    $ErrorActionPreference = 'Stop'
    $parameters = @((Join-Path $PSScriptRoot 'image_lock_test.py'))
    if ($Image) { $parameters += @('--image', $Image) }
    if ($ROMAW) { $parameters += @('--romaw', $ROMAW) }
    & $Python @parameters
    if ($LASTEXITCODE -ne 0) { throw 'Mounted-image lock regression failed.' }
}

function Invoke-Check-catalog-search {
    param(
        [string]$ROMAW,
        [string]$Python = 'python'
    )
    $parameters = @((Join-Path $PSScriptRoot 'catalog_search_test.py'))
    if ($ROMAW) { $parameters += @('--romaw', $ROMAW) }
    & $Python @parameters
    if ($LASTEXITCODE -ne 0) { throw 'Catalog search regression failed.' }
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('catalog-search', 'drive-icon', 'drive-volume', 'file-icon', 'finder-image-volume', 'folder-name', 'follow-alias', 'image-import', 'image-session', 'image-startup', 'volume-custom-icon', 'volume-info', 'volume-startup', 'image-desktop', 'image-lock') -CheckArguments $args
