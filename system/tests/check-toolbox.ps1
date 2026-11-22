# +===================================================================
#   File:      check-toolbox.ps1
#
#   Summary:   Run toolbox regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-Check-dcmp0-resource {
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
    $fixture = Join-Path $root ('build/dcmp0-resource-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-dcmp0-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate dcmp 0 probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "dcmp 0 probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'DCMP0_RESOURCE_GUEST_OK') {
        throw "dcmp 0 probe failed; see $fixture"
    }
    Write-Output "dcmp 0 Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-get-map {
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
    $fixture = Join-Path $root ('build/get-map-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-get-map-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate GetMap probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "GetMap probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'GET_MAP_PASCAL_OK') {
        throw "GetMap probe failed; see $fixture"
    }
    Write-Output "GetMap Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-itl2-sort {
    param(
        [string]$ROMAW,
        [string]$Application,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path $env:USERPROFILE '.romaw/Apps/Browser' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $Application = (Resolve-Path -LiteralPath $Application).Path
    $fixture = Join-Path $root ('build/it-' + [guid]::NewGuid().ToString('N').Substring(0, 16))
    $profile = Join-Path $fixture '.romaw'
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $profile 'Apps') -Target (Join-Path $env:USERPROFILE '.romaw/Apps') | Out-Null
    $drive = [IO.Path]::GetPathRoot($fixture).Substring(0, 1)
    [IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=$drive`n")
    $linuxFixture = (& wsl.exe -d Ubuntu -- wslpath -u $fixture.Replace('\', '/')).Trim()
    $source = (& wsl.exe -d Ubuntu -- wslpath -u (Join-Path $PSScriptRoot 'itl2-hooks-probe.s').Replace('\', '/')).Trim()
    & wsl.exe -d Ubuntu -- m68k-linux-gnu-as -m68000 $source -o "$linuxFixture/hooks.o"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to assemble original itl2 callbacks' }
    & wsl.exe -d Ubuntu -- m68k-linux-gnu-objcopy -O binary -j .text "$linuxFixture/hooks.o" "$linuxFixture/hooks.bin"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to extract original itl2 callbacks' }
    $template = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'itl2-sort-probe.gdb.in'))
    foreach ($native in @(0, 1)) {
        $options = if ($native) { '' } else { '-notnative' }
        $commands = $template.Replace('@FIXTURE@', $fixture.Replace('\', '/')).Replace('@APPLICATION@', $Application.Replace('\', '/')).Replace('@OPTIONS@', $options).Replace('@NATIVE@', [string]$native)
        $script = Join-Path $fixture "probe-$native.gdb"
        [IO.File]::WriteAllText($script, $commands)
        $log = Join-Path $fixture "probe-$native.log"
        $arguments = '-q -batch -x "' + $script + '" "' + $ROMAW + '"'
        $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture "probe-$native.err")
        $null = $process.Handle
        if (-not $process.WaitForExit(45000)) {
            Stop-Process -Id $process.Id
            throw "itl2 callback probe timed out; see $fixture"
        }
        if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'ITL2_GUEST_HOOKS_OK') {
            throw "itl2 callback probe failed; see $fixture"
        }
        Write-Output "PASS: original 68K itl2 hooks, register/frame ABI and lock restoration; native=$native; $log"
    }
}

function Invoke-Check-resource-create {
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
    if (-not (Test-Path -LiteralPath $Application)) { throw 'The probe application is unavailable.' }
    $fixture = Join-Path $root ('build/resource-create-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-resource-create-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate the resource creation probe.' }
    $output = & $Gdb -batch -x (Join-Path $fixture 'probe.gdb') $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'RESOURCE_CREATE_FLAGS_OK')) {
        throw "Resource creation trap checks failed; see $fixture/probe.log"
    }
    Write-Output "Resource creation trap checks passed: $fixture/probe.log"
}

function Invoke-Check-system-resource-runtime {
    param(
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe',
        [string]$Python = 'python'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $stage = Join-Path $root 'windows-binaries/bin'
    $check = Join-Path $root 'build/system-resource-runtime-check'
    $profile = Join-Path $check '.romaw'
    $system = Join-Path $profile 'Apps/System Folder'
    New-Item -ItemType Directory -Force $system | Out-Null
    if ((Test-Path -LiteralPath "$system/System") -or (Test-Path -LiteralPath "$system/._System")) {
        throw 'The isolated test System Folder must not contain a System file.'
    }
    Copy-Item -LiteralPath "$stage/ROMAW.exe" -Destination $check -Force
    Copy-Item -LiteralPath "$stage/ROMAW.rsrc" -Destination $check -Force
    Copy-Item "$stage/*.dll" -Destination $check -Force
    $sourceApps = Join-Path $env:USERPROFILE '.romaw/Apps'
    foreach ($name in @('ROMAW Finder', '._ROMAW Finder')) {
        Copy-Item -LiteralPath (Join-Path $sourceApps $name) -Destination "$profile/Apps" -Force
    }
    [IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
    Remove-Item -LiteralPath "$system/Fonts/FontFixture", "$system/Fonts/._FontFixture" -Force -ErrorAction SilentlyContinue
    $app = (Join-Path $profile 'Apps/ROMAW Finder').Replace('\', '/')
    $homePath = $check.Replace('\', '/')
    foreach ($phase in @('save', 'restore')) {
        if ($phase -eq 'restore') {
            & $Python (Join-Path $PSScriptRoot 'create-system-font-fixture.py') $system
            if ($LASTEXITCODE -ne 0) { throw 'Unable to create the font suitcase fixture.' }
        }
        $commands = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "system-resource-$phase.gdb.in"))
        $script = Join-Path $check "$phase.gdb"
        $commands = $commands.Replace('run "__APP__"', "set environment USERPROFILE $homePath`nrun `"__APP__`"")
        [IO.File]::WriteAllText($script, $commands.Replace('__APP__', $app))
        $output = & $Gdb -batch -x $script "$check/ROMAW.exe" 2>&1
        $status = $LASTEXITCODE
        $output | Set-Content (Join-Path $check "$phase.log")
        $marker = if ($phase -eq 'save') { 'RESOURCE_PREFERENCES_SAVED_OK' } else { 'RESOURCE_PREFERENCES_RESTORED_OK' }
        if ($status -ne 0 -or -not ($output -match $marker)) {
            throw "System resource $phase failed; see $check/$phase.log"
        }
    }
    Write-Output 'Embedded System startup, external fonts, guest PACK 1 dispatch and preference save/relaunch passed'
}

function Invoke-Check-toolbox-resource {
    param(
        [string]$ROMAW,
        [string]$Application,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path $env:USERPROFILE '.romaw/Apps/Browser' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $Application = (Resolve-Path -LiteralPath $Application).Path
    $fixture = Join-Path $root ('build/tr-' + [guid]::NewGuid().ToString('N').Substring(0, 16))
    $profile = Join-Path $fixture '.romaw'
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fixture 'Folder') | Out-Null
    foreach ($bundle in @('Bundle', 'Copy')) {
        New-Item -ItemType Directory -Path (Join-Path $fixture "$bundle/Links"), (Join-Path $fixture "$bundle/Docs") -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $fixture "$bundle/Links/Alias"), '')
        [IO.File]::WriteAllText((Join-Path $fixture "$bundle/Docs/File"), 'relative alias probe')
    }
    New-Item -ItemType Junction -Path (Join-Path $profile 'Apps') -Target (Join-Path $env:USERPROFILE '.romaw/Apps') | Out-Null
    # All resource-file writes stay inside this disposable fixture. The private
    # profile enables sidecars on its drive without changing the user's settings.
    $drive = [IO.Path]::GetPathRoot($fixture).Substring(0, 1)
    [IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=$drive`n")
    $commands = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'toolbox-resource-persistence.gdb.in'))
    $commands = $commands.Replace('@FIXTURE@', $fixture.Replace('\', '/')).Replace('@APPLICATION@', $Application.Replace('\', '/'))
    $script = Join-Path $fixture 'probe.gdb'
    [IO.File]::WriteAllText($script, $commands)
    $log = Join-Path $fixture 'probe.log'
    $arguments = '-q -batch -x "' + $script + '" "' + $ROMAW + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture 'probe.err')
    $null = $process.Handle
    if (-not $process.WaitForExit(45000)) {
        Stop-Process -Id $process.Id
        throw "Resource persistence probe timed out; see $fixture"
    }
    if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'TOOLBOX_RESOURCE_PERSISTENCE_OK') {
        throw "Resource persistence probe failed; see $fixture"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $fixture 'partial') -PathType Leaf) -or -not (Test-Path -LiteralPath (Join-Path $fixture '._partial') -PathType Leaf)) {
        throw "Resource persistence probe did not write its sandbox file; see $fixture"
    }
    Write-Output "PASS: resource persistence plus real file/folder alias creation and resolution; $log"
}

function Invoke-Check-thread-yield {
    param(
        [string]$Executable = '',
        [string]$Application = '',
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path $env:USERPROFILE '.romaw/Apps/Browser' }
    $fixture = Join-Path $root ('build/thread-yield-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $commands = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'thread-yield.gdb.in'))
    $commands = $commands.Replace('@ROOT@', $root.Replace('\', '/')).Replace('@APP@', $Application.Replace('\', '/'))
    $script = Join-Path $fixture 'probe.gdb'
    [IO.File]::WriteAllText($script, $commands)
    $log = Join-Path $fixture 'probe.log'
    $process = Start-Process -FilePath $Gdb -ArgumentList @('-q', '-batch', '-x', $script, $Executable) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture 'probe.err')
    try {
        if (-not $process.WaitForExit(45000)) { throw "Thread yield probe timed out; see $fixture" }
        if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'THREAD_YIELD_ABI_OK') {
            throw "Thread yield probe failed; see $fixture"
        }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
    Write-Output "PASS: 68K Thread Manager yield stack ABI; $log"
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('dcmp0-resource', 'get-map', 'itl2-sort', 'resource-create', 'system-resource-runtime', 'toolbox-resource', 'thread-yield') -CheckArguments $args
