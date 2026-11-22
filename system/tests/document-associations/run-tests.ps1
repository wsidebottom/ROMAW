# +===================================================================
#   File:      run-tests.ps1
#
#   Summary:   Implements run tests support for regression tests and test fixtures.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/tests/document-associations; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

param([string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$fixture = Join-Path $root 'build\document-association-check'
$bin = Join-Path $root 'windows-binaries\bin'
$profile = Join-Path $fixture '.romaw'
New-Item -ItemType Directory -Force -Path $profile | Out-Null
$apps = Join-Path $profile 'Apps'
if (-not (Test-Path -LiteralPath $apps)) {
    New-Item -ItemType Junction -Path $apps -Target (Join-Path $fixture 'Apps') | Out-Null
}
$fixtureApps = (Join-Path $fixture 'Apps').Replace('\', '/')
[IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`nMacDirectoryCount=1`nMacDirectory1=$fixtureApps`n")
Copy-Item -LiteralPath (Join-Path $fixture 'ROMAW.ftm') -Destination (Join-Path $profile 'ROMAW.ftm') -Force

Copy-Item -LiteralPath (Join-Path $bin 'ROMAW.exe') -Destination $fixture
Get-ChildItem -LiteralPath $bin -Filter '*.dll' | Copy-Item -Destination $fixture
Copy-Item -LiteralPath (Join-Path $bin 'ROMAW.rsrc') -Destination $fixture
$exe = Join-Path $fixture 'ROMAW.exe'
$base = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_win_drive_preferences
commands
silent
printf "UNEXPECTED_DRIVE_SETUP\n"
quit 3
end
set environment USERPROFILE @PROFILE@
break C_SysBeep
commands
silent
if i < 0
printf "DOCUMENT_ASSOCIATION_FAILURE_%d\n",-i
quit 1
end
if i == 777
printf "DOCUMENT_ASSOCIATION_OK\n"
quit 0
end
return
continue
end
run "APP"
quit 1
'@
foreach ($app in @('DocumentProbe', 'Apps\Sample.ftst')) {
    $name = [IO.Path]::GetFileNameWithoutExtension($app)
    $script = Join-Path $fixture ($name + '.gdb')
    $arguments = (Join-Path $fixture $app).Replace('\', '/')
    [IO.File]::WriteAllText($script, $base.Replace('APP', $arguments).Replace('@PROFILE@', $fixture.Replace('\', '/')))
    $log = Join-Path $fixture ($name + '.log')
    $process = Start-Process -FilePath $Gdb -ArgumentList '-q', '-batch', '-x', $script, $exe -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture ($name + '.err'))
    if (-not $process.WaitForExit(30000)) { Stop-Process -Id $process.Id; throw 'Association test timed out' }
    if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'DOCUMENT_ASSOCIATION_OK') {
        throw "Document association failed: $([IO.File]::ReadAllText($log))"
    }
}
Write-Output 'Document associations: inferred/explicit metadata, document icon, guest Toolbox launch and direct document launch passed.'
