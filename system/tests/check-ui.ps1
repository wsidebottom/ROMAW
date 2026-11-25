# +===================================================================
#   File:      check-ui.ps1
#
#   Summary:   Run ui regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-Check-print-cancel {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$ROMAW
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/print-cancel.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & python (Join-Path $root 'synppc/tests/make_ppc_print_cancel_probe.py') $fixture
    if ($LASTEXITCODE) { throw 'Cannot generate the print cancellation probe' }
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set substitute-path /mnt/d/Source/ROMAW D:/Source/ROMAW
set $print_dialog_calls = 0
break host_print_dialog
commands
silent
set $print_dialog_calls = $print_dialog_calls + 1
if $print_dialog_calls > 4
printf "UNEXPECTED_PRINT_DIALOG\n"
quit 1
end
if $print_dialog_calls == 4
return (int)-1
else
return (int)0
end
continue
end
break host_dialog_error
commands
silent
return
continue
end
break host_raster_begin
commands
silent
printf "CANCEL_STARTED_RASTER_OUTPUT\n"
quit 1
end
break ROMlib_win_ppc_error
commands
silent
bt 20
quit 1
end
run
if $print_dialog_calls != 4
printf "PRINT_DIALOG_COUNT_MISMATCH\n"
quit 1
end
printf "PRINT_CANCEL_HOST_OK\n"
quit 0
'@
    $script = Join-Path $fixture 'cancel.gdb'
    [IO.File]::WriteAllText($script, $commands)
    foreach ($engine in @('-no-ppc-jit', '-ppc-jit')) {
        $name = $engine.TrimStart('-')
        $stdout = Join-Path $fixture "$name.gdb.log"
        $stderr = Join-Path $fixture "$name.gdb-error.log"
        $app = Join-Path $fixture 'PPC Print Cancel Probe'
        $arguments = '-q -batch --return-child-result -x "' + $script + '" --args "' + $ROMAW + '" ' +
        $engine + ' -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $app + '"'
        $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture `
            -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        try {
            if (-not $process.WaitForExit(20000)) { throw "Print cancellation timed out: $fixture" }
            $guest = Join-Path $fixture 'stderr.txt'
            $log = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
            if (Test-Path -LiteralPath $guest) {
                $log += [IO.File]::ReadAllText($guest)
                Copy-Item -LiteralPath $guest -Destination (Join-Path $fixture "$name.guest.log")
            }
            if ($process.ExitCode -ne 0 -or $log -notmatch 'PRINT_CANCEL_HOST_OK' -or
                $log -notmatch 'debugstr: PRINT_CANCEL_OK' -or $log -notmatch 'PowerPC fragment completed' -or
                $log -match 'PowerPC launch failed|win_exception|Illegal instruction|CANCEL_STARTED_RASTER_OUTPUT') {
                throw "Print cancellation failed: $fixture ($engine)"
            }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
    Write-Output "PASS: print cancellation, retry, host errors and port preservation; $fixture"
}

function Invoke-Check-control-panel-host {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$ROMAW,
        [string]$AppsDirectory
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $AppsDirectory) { $AppsDirectory = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps' }
    $check = Join-Path $root 'build/control-panel-host-check'
    New-Item -ItemType Directory -Force $check | Out-Null
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set $closed = 0
set $ready = 0
break panel_call
commands
silent
if message == 0
call sdl_hold_presentation(1)
end
if message == 3 && $ready == 0
if ROMlib_desk_host_active != 1 || screen == 0
printf "PANEL_HOST_INACTIVE\n"
quit 1
end
if (screen->flags & 32) == 0 || screen->w >= vdriver_width || screen->h >= vdriver_height
printf "PANEL_HOST_NOT_CROPPED\n"
quit 1
end
if desk_shape_key == 0 || title_bottom <= title_top
printf "PANEL_HOST_NOT_SHAPED\n"
quit 1
end
set $ready = 1
set ROMlib_desk_host_close = 1
end
if message == 2
if $ready == 0
printf "PANEL_CLOSED_BEFORE_READY\n"
quit 1
end
set $closed = 1
end
continue
end
break C_UseResFile
commands
silent
if $closed == 1
if ROMlib_desk_host_active != 0 || ROMlib_desk_host_close != 0
printf "PANEL_HOST_STATE_NOT_RESTORED\n"
quit 1
end
printf "PANEL_HOST_OK\n"
quit 0
end
continue
end
run "APP"
bt
printf "PANEL_ENDED_WITHOUT_SUCCESS\n"
quit 1
'@
    foreach ($name in @('Sound', 'General')) {
        $app = (Join-Path $AppsDirectory "System Folder/Control Panels/$name").Replace('\', '/')
        $script = Join-Path $check "$name.gdb"
        [IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
        Push-Location $check
        try {
            $output = & $Gdb -q -batch -x $script $romaw 2>&1 | Out-String
            $status = $LASTEXITCODE
        }
        finally { Pop-Location }
        [IO.File]::WriteAllText((Join-Path $check "$name.log"), $output)
        if ($status -ne 0 -or $output -notmatch 'PANEL_HOST_OK') {
            throw "Control panel $name failed: $output"
        }
    }
    Write-Output 'Control panel host presentation, close callbacks and state restoration passed.'
}

function Invoke-Check-display-scale-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/display-scale-options.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $oldDriver = $env:SDL_VIDEODRIVER
    try {
        $env:SDL_VIDEODRIVER = 'romaw-scale-test-invalid'
        foreach ($value in @('0', '24', '1601', '999999999999999999999999', '150%', '1.5', 'abc', '+150', '-1')) {
            $process = Start-Process -FilePath $ROMAW -ArgumentList ('-display-scale ' + $value) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            $null = $process.Handle
            try {
                if (-not $process.WaitForExit(10000)) { throw "Scale $value did not exit promptly" }
                if ($process.ExitCode -ne -10) { throw "Scale $value exited with $($process.ExitCode), expected -10" }
            }
            finally {
                if (-not $process.HasExited) { Stop-Process -Id $process.Id }
                $process.Dispose()
            }
            Write-Output "PASS: invalid scale $value rejected before video initialization"
        }
    }
    finally { $env:SDL_VIDEODRIVER = $oldDriver }
}

function Invoke-Check-display-scaling {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/display-scaling.' + [guid]::NewGuid().ToString('N'))
    $profile = Join-Path $fixture '.romaw'
    New-Item -ItemType Directory -Force $profile | Out-Null
    # Reuse installed Macintosh assets without copying or modifying their forks.
    New-Item -ItemType Junction -Path (Join-Path $profile 'Apps') -Target (Join-Path $env:USERPROFILE '.romaw/Apps') | Out-Null
    $probe = Join-Path $workspace 'system/obj/tests/startup-display/StartupDisplayProbe.ad'
    Copy-Item -LiteralPath $probe -Destination (Join-Path $fixture 'Probe')
    Copy-Item -LiteralPath (Join-Path (Split-Path $probe) '%StartupDisplayProbe.ad') -Destination (Join-Path $fixture '._Probe')
    $commands = @'
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
set $shown = 0
break ROMlib_sdl_startup_ready
commands
silent
if vdriver_width != WIDTH || vdriver_height != HEIGHT
quit 1
end
if screen->w != HOSTWIDTH || screen->h != HOSTHEIGHT || display_scale != @SCALE@
printf "SCALED_SURFACE_FAILED\n"
quit 1
end
if screen->format->palette->colors[0].r != 255 || screen->format->palette->colors[0].g != 255 || screen->format->palette->colors[0].b != 255
printf "STARTUP_PALETTE_FAILED\n"
quit 1
end
printf "FIRST_WINDOW_OK\n"
set $shown = 1
continue
end
break C_InitGraf
commands
silent
if $shown
if vdriver_width != WIDTH || vdriver_height != HEIGHT
quit 1
end
if screen->w != HOSTWIDTH || screen->h != HOSTHEIGHT || display_scale != @SCALE@
printf "SCALED_SURFACE_FAILED\n"
quit 1
end
printf "INIT_GRAF_SIZE_OK\n"
set ROMlib_display_scale = 200
call (void) sdl_queue_window_size (WIDTH, HEIGHT)
call (void) sdl_apply_pending_resize ()
if vdriver_width != WIDTH || vdriver_height != HEIGHT || screen->w != 1024 || screen->h != 684
printf "LIVE_SCALE_FAILED\n"
quit 1
end
if screen->format->palette->colors[0].r != 255
printf "LIVE_PALETTE_FAILED\n"
quit 1
end
printf "LIVE_SCALE_OK\n"
quit 0
end
continue
end
run OPTIONS "APPLICATION"
quit 2
'@
    $cases = @(100, 150, 200, 400, 800, 125, 25) | ForEach-Object {
        @{Options = ''; Scale = $_; SavedScale = $_; Width = 512; Height = 342; Name = "prefs-$_" }
    }
    foreach ($scale in @(125, 150, 200)) {
        $cases += @{Options = "-display-scale $scale"; Scale = $scale; SavedScale = 400; Width = 512; Height = 342; Name = "cli-$scale" }
    }
    foreach ($case in $cases) {
        $scale = $case.Scale
        [IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n[APPL.FwPr]`nSize=512x342`nDisplayScale=$($case.SavedScale)`n[APPL.OTHR]`nDisplayScale=400`n")
        $script = $commands.Replace('HOSTWIDTH', [Math]::Ceiling(512 * $scale / 100)).Replace('HOSTHEIGHT', [Math]::Ceiling(342 * $scale / 100)).Replace('@SCALE@', $scale).Replace('@PROFILE@', $fixture.Replace('\', '/')).Replace('WIDTH', $case.Width).Replace('HEIGHT', $case.Height).Replace('OPTIONS', $case.Options).Replace('APPLICATION', (Join-Path $fixture 'Probe').Replace('\', '/'))
        $scriptPath = Join-Path $fixture 'test.gdb'
        [IO.File]::WriteAllText($scriptPath, $script)
        Push-Location $fixture
        try {
            $output = & $Gdb -q -batch -x $scriptPath $ROMAW 2>&1 | Out-String
            $status = $LASTEXITCODE
        }
        finally { Pop-Location }
        [IO.File]::WriteAllText((Join-Path $fixture ('result-' + $case.Name + '.log')), $output)
        if ($status -ne 0 -or $output -notmatch 'FIRST_WINDOW_OK' -or $output -notmatch 'INIT_GRAF_SIZE_OK' -or $output -notmatch 'LIVE_SCALE_OK') { throw "Startup window test failed: $output" }
    }
    Write-Output 'Preference and command-line scaling applied before first display and InitGraf; guest dimensions unchanged.'
}

function Invoke-Check-document-open {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = '',
        [string]$Application = '',
        [string]$Document = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $apps = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps'
    if (-not $Application) { $Application = Join-Path $apps 'Browser' }
    if (-not $Document) { $Document = Join-Path $apps 't6' }
    $fixture = Join-Path $root ('build/document-open-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $probe = Join-Path $PSScriptRoot 'document_open_probe.py'
    $quotedApp = ConvertTo-Json -Compress $Application.Replace('\', '/')
    $quotedDocument = ConvertTo-Json -Compress $Document.Replace('\', '/')
    $quotedProbe = ConvertTo-Json -Compress $probe.Replace('\', '/')
    $script = @"
set pagination off
set confirm off
set print thread-events off
break C_WaitNextEvent
run $quotedApp
disable 1
python
DOCUMENT = $quotedDocument
exec(compile(open($quotedProbe, 'rb').read(), $quotedProbe, 'exec'))
end
quit 0
"@
    $file = Join-Path $fixture 'probe.gdb'
    $script | Set-Content -LiteralPath $file
    $output = & $Gdb -batch -x $file $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'DOCUMENT_COERCION_AND_X64_OUTPUTS_OK')) {
        throw "Document-open regression failed; see $fixture/probe.log"
    }
    Write-Output "Document alias coercion, file payloads and x64 output guards passed: $fixture/probe.log"
}

function Invoke-Check-document-transition {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = '',
        [string]$Application = '',
        [string]$Document = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $apps = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps'
    if (-not $Application) { $Application = Join-Path $apps 'MacWrite' }
    if (-not $Document) { $Document = Join-Path $apps 't6' }
    $arguments = 'set args "' + $Application.Replace('\', '/') + '" "' + $Document.Replace('\', '/') + '"'
    $output = & $Gdb -batch -ex $arguments -x (Join-Path $PSScriptRoot 'document_transition_probe.gdb') $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/document-transition-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'FINDER_TO_24BIT_DOCUMENT_TRANSITION_OK')) {
        throw "Finder-to-24-bit document transition failed; see $log"
    }
    Write-Output 'Large Finder display to 24-bit MacWrite document transition passed.'
}

function Invoke-Check-macwrite-quit {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/MacWrite'
    }
    if (-not (Test-Path -LiteralPath $Application)) { throw 'MacWrite 2.2 is unavailable.' }
    $fixture = Join-Path $root ('build/macwrite-quit-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $script = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'macwrite-quit-probe.gdb')
    $quotedApplication = ConvertTo-Json -Compress $Application.Replace('\', '/')
    $script.Replace('@APPLICATION@', $quotedApplication) | Set-Content -LiteralPath (Join-Path $fixture 'probe.gdb')
    $output = & $Gdb -batch -x (Join-Path $fixture 'probe.gdb') $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'MACWRITE_GUEST_QUIT_OK')) {
        throw "MacWrite guest quit checks failed; see $fixture/probe.log"
    }
    Write-Output "MacWrite guest quit checks passed: $fixture/probe.log"
}

function Invoke-Check-menu-banner {
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
    $fixture = Join-Path $root ('build/menu-banner-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-menu-banner-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Menu banner probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Menu banner probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    $marker = 'MENU_BANNER_PASCAL_OK'
    if ($process.ExitCode -ne 0 -or $output -notmatch $marker) {
        throw "Menu banner probe failed; see $fixture"
    }
    $kind = 'Pascal dispatch'
    Write-Output "Menu banner $kind passed: $fixture/probe.log"
}

function Invoke-Check-mouse-input {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace 'build\mouse-input-check'
    New-Item -ItemType Directory -Force $fixture | Out-Null
    Copy-Item -LiteralPath (Join-Path $workspace 'build\mouse-input-probe\MouseInputProbe.ad') -Destination (Join-Path $fixture 'MouseInputProbe')
    Copy-Item -LiteralPath (Join-Path $workspace 'build\mouse-input-probe\%MouseInputProbe.ad') -Destination (Join-Path $fixture '._MouseInputProbe')
    $script = Join-Path $fixture 'test.gdb'
    $app = (Join-Path $fixture 'MouseInputProbe').Replace('\', '/')
    # Inject SDL events only into this owned diagnostic process. The guest reports
    # success explicitly: a normal classic ExitToShell alone is not a test result.
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set $phase=0
define button
set $e->button.type=6-$arg1
set $e->button.state=$arg1
set $e->button.button=$arg0
set $e->button.x=$arg2
set $e->button.y=$arg3
set $result=(int)SDL_PushEvent($e)
if $result < 0
quit 1
end
end
break C_SysBeep
commands
silent
if i < 0
printf "MOUSE_FAILURE_%d\n", -i
quit 1
end
if i == 777
if $phase != 7
quit 1
end
printf "MOUSE_INPUT_OK\n"
quit 0
end
set $phase=$phase+1
printf "MOUSE_PHASE_%d\n", $phase
set $e=(SDL_Event*)calloc(1,sizeof(SDL_Event))
if $e == 0
quit 1
end
if $phase == 1
button 3 1 100 100
button 3 0 100 100
end
if $phase == 2 || $phase == 3 || $phase == 4 || $phase == 7
if $phase == 4
set $e->key.type=2
set $e->key.state=1
set $e->key.keysym.sym=304
set $result=(int)SDL_PushEvent($e)
end
if $phase == 3
button 4 1 100 100
button 4 0 100 100
else
button 5 1 100 100
button 5 0 100 100
end
if $phase == 7
button 5 1 100 100
button 5 0 100 100
end
end
if $phase == 5
set $e->key.type=3
set $e->key.state=0
set $e->key.keysym.sym=304
set $result=(int)SDL_PushEvent($e)
button 2 1 200 200
button 2 0 200 200
button 1 1 100 100
button 1 0 100 100
end
if $phase == 6
set ROMlib_right_button_modifier=2048
button 3 1 100 100
button 3 0 100 100
end
call (void)free($e)
return
continue
end
run "APP"
printf "MOUSE_INPUT_ENDED_WITHOUT_SUCCESS\n"
quit 1
'@
    [IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
    Push-Location $fixture
    try {
        $output = & $Gdb -q -batch -x $script $ROMAW 2>&1 | Out-String
        $status = $LASTEXITCODE
    }
    finally { Pop-Location }
    [IO.File]::WriteAllText((Join-Path $fixture 'gdb.log'), $output)
    if ($status -ne 0 -or $output -notmatch 'MOUSE_INPUT_OK') { throw "Mouse input probe failed: $output" }
    Write-Output 'Mouse input probe passed.'
}

function Invoke-Check-puzzle {
    param(
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $romaw = Join-Path $root 'windows-binaries/bin/ROMAW.exe'
    $app = (Join-Path $root 'windows-binaries/bin/Apps/System Folder/Apple Menu Items/Puzzle').Replace('\', '/')
    if (-not (Test-Path -LiteralPath $app)) { throw "Installed Puzzle accessory required: $app" }
    $check = Join-Path $root 'build/puzzle-check'
    New-Item -ItemType Directory -Force $check | Out-Null
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set $restored = 0
set $saved = 0
set $events = 0
break _LayerDispatch
commands
silent
if cpu_state.regs[0].ub.n == 11
set $restored = $restored + 1
end
if cpu_state.regs[0].ub.n == 10
set $saved = $saved + 1
end
continue
end
break _Unimplemented
commands
silent
printf "PUZZLE_UNIMPLEMENTED_%x\n",mostrecenttrap
quit 1
end
break C_WaitNextEvent
commands
silent
if ROMlib_desk_host_active == 1
set $events = $events + 1
if $events == 2
if screen == 0 || desk_shape_key == 0
printf "PUZZLE_HOST_NOT_READY\n"
quit 1
end
set ROMlib_desk_host_close = 1
end
end
continue
end
break C_ExitToShell
commands
silent
if $restored != 1 || $saved != 1 || $events < 2
printf "PUZZLE_INCOMPLETE_%d_%d_%d\n",$restored,$saved,$events
quit 1
end
printf "PUZZLE_STATE_AND_CLOSE_OK\n"
quit 0
end
run "__PUZZLE_APP__"
bt
quit 1
'@
    $script = Join-Path $check 'state-close.gdb'
    [IO.File]::WriteAllText($script, $commands.Replace('__PUZZLE_APP__', $app))
    Push-Location $check
    try {
        $output = & $Gdb -q -batch -x $script $romaw 2>&1 | Out-String
        $status = $LASTEXITCODE
    }
    finally { Pop-Location }
    [IO.File]::WriteAllText((Join-Path $check 'state-close.log'), $output)
    if ($status -ne 0 -or $output -notmatch 'PUZZLE_STATE_AND_CLOSE_OK') { throw "Puzzle state/close probe failed: $output" }
    Write-Output 'Puzzle saved-state restore, host presentation, event loop and close passed.'
}

function Invoke-Check-startup-window {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/startup-window.' + [guid]::NewGuid().ToString('N'))
    $profile = Join-Path $fixture '.romaw'
    New-Item -ItemType Directory -Force $profile | Out-Null
    # Reuse installed Macintosh assets without copying or modifying their forks.
    New-Item -ItemType Junction -Path (Join-Path $profile 'Apps') -Target (Join-Path $env:USERPROFILE '.romaw/Apps') | Out-Null
    $probe = Join-Path $workspace 'system/obj/tests/startup-display/StartupDisplayProbe.ad'
    Copy-Item -LiteralPath $probe -Destination (Join-Path $fixture 'Probe')
    Copy-Item -LiteralPath (Join-Path (Split-Path $probe) '%StartupDisplayProbe.ad') -Destination (Join-Path $fixture '._Probe')
    $commands = @'
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
set $shown = 0
break ROMlib_sdl_startup_ready
commands
silent
if vdriver_width != WIDTH || vdriver_height != HEIGHT
quit 1
end
if screen->format->palette->colors[0].r != 255 || screen->format->palette->colors[0].g != 255 || screen->format->palette->colors[0].b != 255
printf "STARTUP_PALETTE_FAILED\n"
quit 1
end
printf "FIRST_WINDOW_OK\n"
set $shown = 1
continue
end
break C_InitGraf
commands
silent
if $shown
if vdriver_width != WIDTH || vdriver_height != HEIGHT
quit 1
end
printf "INIT_GRAF_SIZE_OK\n"
quit 0
end
continue
end
run OPTIONS "APPLICATION"
quit 2
'@
    foreach ($case in @(@{Options = ''; Width = 1024; Height = 768 }, @{Options = '-window-size 800x600'; Width = 800; Height = 600 })) {
        [IO.File]::WriteAllText((Join-Path $profile 'ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n[APPL.FwPr]`nSize=1024x768`n")
        $script = $commands.Replace('@PROFILE@', $fixture.Replace('\', '/')).Replace('WIDTH', $case.Width).Replace('HEIGHT', $case.Height).Replace('OPTIONS', $case.Options).Replace('APPLICATION', (Join-Path $fixture 'Probe').Replace('\', '/'))
        $scriptPath = Join-Path $fixture 'test.gdb'
        [IO.File]::WriteAllText($scriptPath, $script)
        Push-Location $fixture
        try {
            $output = & $Gdb -q -batch -x $scriptPath $ROMAW 2>&1 | Out-String
            $status = $LASTEXITCODE
        }
        finally { Pop-Location }
        [IO.File]::WriteAllText((Join-Path $fixture ('result-' + $case.Width + '.log')), $output)
        if ($status -ne 0 -or $output -notmatch 'FIRST_WINDOW_OK' -or $output -notmatch 'INIT_GRAF_SIZE_OK') { throw "Startup window test failed: $output" }
    }
    Write-Output 'Saved startup dimensions and command-line override applied before first display and InitGraf.'
}

function Invoke-Check-window-resize {
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
    $fixture = Join-Path $root ('build/window-resize-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-window-resize-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Window resize probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Window resize probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'WINDOW_RESIZE_OK') {
        throw "Window resize probe failed; see $fixture"
    }
    Write-Output "Window resize damage checks passed: $fixture/probe.log"
}

function Invoke-Check-window-size-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\window-size-options.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $savedVideoDriver = $env:SDL_VIDEODRIVER
    try {
        # Invalid dimensions must fail before trying to initialize a video driver.
        $env:SDL_VIDEODRIVER = 'romaw-size-test-invalid'
        foreach ($arguments in @('-window-size 511x342', '-window-size 512x341',
                '-window-size 999999999999999999999x480',
                '-window-size 4097x480', '-size 800x600junk',
                '-fixed-window-size 511x342',
                '-fixed-window-size 800x341',
                '-fixed-window-size 4097x600',
                '-fixed-window-size 800x600junk',
                '-fullscreen -fixed-window-size 800x600')) {
            $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            $null = $process.Handle
            if (-not $process.WaitForExit(10000)) {
                Stop-Process -Id $process.Id
                throw "$arguments did not exit promptly"
            }
            if ($process.ExitCode -ne -10) {
                throw "$arguments exited with $($process.ExitCode), expected size-validation failure (-10)"
            }
            Write-Output "PASS: $arguments rejected before video initialization"
        }
    }
    finally {
        $env:SDL_VIDEODRIVER = $savedVideoDriver
    }
}

function Invoke-Check-window-startup {
    param(
        [string]$Executable = '',
        [string]$Application = '',
        [string]$SystemFolder = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path $env:USERPROFILE '.romaw/Apps/Browser' }
    if (-not $SystemFolder) { $SystemFolder = Join-Path $env:USERPROFILE '.romaw/Apps/System Folder' }
    foreach ($path in @($Executable, $Application, $SystemFolder)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing fixture input: $path" }
    }
    $fixture = Join-Path $root ('build/window-startup-' + [guid]::NewGuid().ToString('N'))
    $fixtureProfile = Join-Path $fixture 'user'
    $apps = Join-Path $fixtureProfile '.romaw/Apps'
    New-Item -ItemType Directory -Path $apps -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $apps 'System Folder') -Target $SystemFolder | Out-Null
    # Browser's creator is GOGO. The real user preferences and desktop are never used.
    @'
[HostDrives]
SidecarDrives=none
[APPL.GOGO]
Size=900x650
Location=100,120
DisplayScale=100
'@ | Set-Content -LiteralPath (Join-Path $fixtureProfile '.romaw/ROMAW.prefs') -Encoding utf8NoBOM
    $source = (& wsl.exe -d Ubuntu -- wslpath -u ((Join-Path $PSScriptRoot 'window_startup_test.cpp').Replace('\', '/'))).Trim()
    $test = Join-Path $fixture 'window-startup-test.exe'
    $output = (& wsl.exe -d Ubuntu -- wslpath -u ($test.Replace('\', '/'))).Trim()
    & wsl.exe -d Ubuntu -- x86_64-w64-mingw32-g++ -std=c++17 -municode -Wall -Wextra -Werror -static $source -o $output
    if ($LASTEXITCODE -ne 0) { throw 'Unable to compile the Windows startup fixture.' }
    Push-Location $fixture
    try {
        & $test $Executable $Application $fixtureProfile
        if ($LASTEXITCODE -ne 0) { throw "Window startup checks failed: $fixture" }
    }
    finally {
        Pop-Location
    }
    Write-Output "Application geometry and memory restart checks passed: $fixture"
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('control-panel-host', 'display-scale-options', 'display-scaling', 'document-open', 'document-transition', 'macwrite-quit', 'menu-banner', 'mouse-input', 'print-cancel', 'puzzle', 'startup-window', 'window-resize', 'window-size-options', 'window-startup') -CheckArguments $args
