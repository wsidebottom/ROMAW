# +===================================================================
#   File:      check-runtime.ps1
#
#   Summary:   Run runtime regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-Check-desk-accessory {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$ROMAW,
        [string]$AppsDirectory
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $AppsDirectory) { $AppsDirectory = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps' }
    $check = Join-Path $root 'build/desk-accessory-check'
    New-Item -ItemType Directory -Force $check | Out-Null
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set $watch = 0
set $shown = 0
set $selected = 0
set $events = 0
break run_accessory
commands
silent
set $watch = 1
call sdl_hold_presentation(1)
continue
end
break C_ShowWindow
commands
silent
if $watch && w != 0
set $shown = w
end
continue
end
break C_SelectWindow
commands
silent
if $watch && w == $shown
set $selected = w
end
continue
end
break ROMlib_sdl_startup_ready if startup_pending
commands
silent
if ROMlib_desk_host_active != 1 || desk_shape_key == 0 || SDL_Window == 0 || (int)IsWindowVisible(SDL_Window) != 0
printf "ACCESSORY_STARTUP_VISIBLE_TOO_EARLY\n"
quit 1
end
printf "ACCESSORY_STARTUP_HIDDEN_UNTIL_PAINTED\n"
continue
end
break _Unimplemented
commands
silent
printf "ACCESSORY_UNIMPLEMENTED_%x\n",mostrecenttrap
quit 1
end
break C_WaitNextEvent
commands
silent
if ROMlib_desk_host_active == 1
set $events = $events + 1
if $events == 2
if $shown == 0 || $selected != $shown || ((WindowPeek)$shown)->visible == 0 || screen == 0 || desk_shape_key == 0 || (screen->flags & 32) == 0
printf "ACCESSORY_HOST_NOT_READY\n"
quit 1
end
if startup_pending || (int)IsWindowVisible(SDL_Window) == 0
printf "ACCESSORY_STARTUP_NOT_REVEALED\n"
quit 1
end
set $handle = (unsigned int)((WindowPeek)$shown)->strucRgn
set $handle = (($handle & 255) << 24) | (($handle & 65280) << 8) | (($handle >> 8) & 65280) | (($handle >> 24) & 255)
set $data = *(unsigned int *)(ROMlib_offset + $handle)
set $data = (($data & 255) << 24) | (($data & 65280) << 8) | (($data >> 8) & 65280) | (($data >> 24) & 255)
set $r = (Region *)(ROMlib_offset + $data)
set $left = (short)((((unsigned short)$r->rgnBBox.left & 255) << 8) | ((unsigned short)$r->rgnBBox.left >> 8))
set $top = (short)((((unsigned short)$r->rgnBBox.top & 255) << 8) | ((unsigned short)$r->rgnBBox.top >> 8))
set $right = (short)((((unsigned short)$r->rgnBBox.right & 255) << 8) | ((unsigned short)$r->rgnBBox.right >> 8))
set $bottom = (short)((((unsigned short)$r->rgnBBox.bottom & 255) << 8) | ((unsigned short)$r->rgnBBox.bottom >> 8))
if $left < 0 || $top < 20 || $right > vdriver_width || $bottom > vdriver_height || screen->w < $right-$left || screen->h < $bottom-$top
printf "ACCESSORY_HOST_CLIPPED bounds=%d,%d,%d,%d surface=%dx%d\n",$left,$top,$right,$bottom,screen->w,screen->h
quit 1
end
printf "ACCESSORY_FULL_WINDOW_VISIBLE %dx%d\n",screen->w,screen->h
set ROMlib_desk_host_close = 1
end
end
continue
end
break C_ExitToShell
commands
silent
if $events < 2
printf "ACCESSORY_EXITED_EARLY\n"
quit 1
end
printf "ACCESSORY_SHOWN_SELECTED_AND_CLOSED\n"
quit 0
end
run "__ACCESSORY_APP__"
bt
quit 1
'@
    foreach ($name in @('Calculator', 'Puzzle', 'Note Pad', 'Scrapbook')) {
        $app = (Join-Path $AppsDirectory "System Folder/Apple Menu Items/$name").Replace('\', '/')
        if (-not (Test-Path -LiteralPath $app)) { throw "Installed accessory required: $app" }
        $script = Join-Path $check "$name.gdb"
        [IO.File]::WriteAllText($script, $commands.Replace('__ACCESSORY_APP__', $app))
        Push-Location $check
        try {
            $output = & $Gdb -q -batch -x $script $romaw 2>&1 | Out-String
            $status = $LASTEXITCODE
        }
        finally { Pop-Location }
        [IO.File]::WriteAllText((Join-Path $check "$name.log"), $output)
        if ($status -ne 0 -or $output -notmatch 'ACCESSORY_SHOWN_SELECTED_AND_CLOSED') { throw "$name failed: $output" }
        Write-Output "$name presentation, activation and close passed."
    }
}

function Invoke-Check-device-install {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser' }
    $fixture = Join-Path $root ('build/device-install-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $probe = (Join-Path $PSScriptRoot 'device_install_probe.py').Replace('\', '/')
    $app = ConvertTo-Json -Compress $Application.Replace('\', '/')
    $quotedProbe = ConvertTo-Json -Compress $probe
    $script = @"
set pagination off
set python print-stack full
set confirm off
set print thread-events off
break ROMlib_win_drive_preferences
commands
silent
return 0
continue
end
break C_WaitNextEvent
run $app
disable 2
python
exec(compile(open($quotedProbe, 'rb').read(), $quotedProbe, 'exec'))
end
quit 0
"@
    $file = Join-Path $fixture 'probe.gdb'
    $script | Set-Content -LiteralPath $file
    $output = & $Gdb -batch -x $file $Executable 2>&1
    $status = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $fixture 'probe.log')
    if ($status -ne 0 -or -not ($output -match 'DRIVER_INSTALL_OPEN_CLOSE_REMOVE_AND_HOST_MAPPING_OK')) {
        throw "Driver regression failed; see $fixture/probe.log"
    }
    Write-Output "Driver installation, dispatch, removal and host mapping passed: $fixture/probe.log"
}

function Invoke-Check-preferences-parser {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $fixture = Join-Path $root ('build/preferences-parser.' + [guid]::NewGuid().ToString('N'))
    & python (Join-Path $root 'synppc/tests/make_ppc_appearance_probe.py') $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Could not create parser launch fixture' }
    $prefs = Join-Path $fixture 'parser.prefs'
    [IO.File]::WriteAllText($prefs, "[APPL.TEST]`nStickyMenus=0`nForwardDelete=0`nAutoRefresh=1`nSubstituteFonts=1`n")
    foreach ($mask in @(0, 15)) {
        $commands = @'
set pagination off
set confirm off
set print thread-events off
break launchchain
commands
silent
set ROMlib_options=0x8000000
set ROMlib_sticky_menus_p=1
set ROMlib_forward_del_p=1
set do_autorefresh_p=0
set substitute_fonts_p=0
call ROMlib_preferences_set_cli_overrides(MASK)
set configfile=config_prefs_open("PREFS", "APPL.TEST")
if configfile == 0
quit 1
end
set validblock=1
set $parsed=yyparse()
call (int)fclose(configfile)
if $parsed != 0 || ROMlib_options != 0x8000000
quit 1
end
if ROMlib_sticky_menus_p != EXPECTED || ROMlib_forward_del_p != EXPECTED || do_autorefresh_p != INVERSE || substitute_fonts_p != INVERSE
quit 1
end
printf "PREFERENCES_PARSER_OK\n"
quit 0
end
run "APPFILE"
'@
        $expected = if ($mask) { '1' } else { '0' }
        $inverse = if ($mask) { '0' } else { '1' }
        $commands = $commands.Replace('MASK', [string]$mask).Replace('EXPECTED', $expected).Replace('INVERSE', $inverse)
        $commands = $commands.Replace('PREFS', $prefs.Replace('\', '/')).Replace('APPFILE', (Join-Path $fixture 'PPC Appearance Probe').Replace('\', '/'))
        $script = Join-Path $fixture "parser-$mask.gdb"
        [IO.File]::WriteAllText($script, $commands)
        $output = & $Gdb -q -batch -x $script $ROMAW 2>&1 | Out-String
        [IO.File]::WriteAllText((Join-Path $fixture "parser-$mask.log"), $output)
        if ($LASTEXITCODE -ne 0 -or $output -notmatch 'PREFERENCES_PARSER_OK') {
            throw "Preferences parser failed: $output"
        }
    }
    Write-Output 'PASS: real startup parser loads boolean preferences, preserves option flags and honours command-line overrides'
}

function Invoke-Check-ecf {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $Gdb = (Resolve-Path -LiteralPath $Gdb).Path
    $fixture = Join-Path $workspace ('build\ecf-test.' + [guid]::NewGuid().ToString('N'))
    $profileHome = Join-Path $fixture 'profile'
    $profileRoot = Join-Path $profileHome '.romaw'
    $apps = Join-Path $profileRoot 'Apps'
    $local = Join-Path $profileHome 'AppData/Local'
    $roaming = Join-Path $profileHome 'AppData/Roaming'
    foreach ($directory in @($apps, $local, $roaming, (Join-Path $apps 'System Folder'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    & python (Join-Path $workspace 'system/tests/make_ecf_probe.py') $apps
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate ECF fixture' }
    $privatePrefs = Join-Path $profileRoot 'ROMAW.prefs'
    $hostSettings = "[HostDrives]`nSidecarDrives=none`n`n"
    [IO.File]::WriteAllText($privatePrefs, $hostSettings)
    $app = (Join-Path $apps 'ECF Launch Probe').Replace('\', '/')
    if (-not (Test-Path -LiteralPath $app)) { throw 'ECF application fixture is missing' }
    $folder = $fixture.Replace('\', '/')
    $saved = Join-Path $fixture 'saved.prefs'
    $script = Join-Path $fixture 'test.gdb'
    $environment = @{
        USERPROFILE = $profileHome; LOCALAPPDATA = $local; APPDATA = $roaming
        ROMAW_ECF_SYSZONE = $null; ROMAW_ECF_APPLZONE = $null; ROMAW_ECF_BROWSER = $null
    }
    $previousEnvironment = @{}
    foreach ($name in $environment.Keys) {
        $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $runProbe = {
        $log = $script + '.log'
        $errors = $script + '.err'
        $process = Start-Process -FilePath $Gdb -ArgumentList @('-q', '-batch', '-x', ('"' + $script + '"'), ('"' + $ROMAW + '"')) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $errors
        try {
            if (-not $process.WaitForExit(60000)) { throw "ECF probe timed out; see $log and $errors" }
            [pscustomobject]@{ ExitCode = $process.ExitCode; Output = ([IO.File]::ReadAllText($log) + [IO.File]::ReadAllText($errors)) }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
    Push-Location $fixture
    try {
        foreach ($name in $environment.Keys) {
            $ecfValue = $environment[$name]
            if ($null -eq $ecfValue) { $ecfValue = [NullString]::Value }
            [Environment]::SetEnvironmentVariable($name, $ecfValue, 'Process')
        }
        $commands = @'
set pagination off
set confirm off
set print thread-events off
break C_DisposeWindow
commands
silent
set ROMlib_options=0x1fe00000
set ROMlib_nowarn32=1
set ROMlib_delay=17
set ROMlib_AppleChar=20
set ROMlib_right_button_modifier=256
set ROMlib_config_syszone_size=8388608
set ROMlib_config_applzone_size=16777216
set $saved=saveprefvalues("FOLDER/saved.prefs",12,34)
if $saved != 1
quit 1
end
printf "ECF_SAVE_OK\n"
quit 0
end
run -syszone 8m -applzone 64m -system 9.2.2 "APP"
'@
        [IO.File]::WriteAllText($script, $commands.Replace('FOLDER', $folder).Replace('APP', $app))
        $probe = & $runProbe
        $output = $probe.Output
        if ($probe.ExitCode -ne 0 -or $output -notmatch 'ECF_SAVE_OK') { throw "ECF save failed: $output" }
        $text = [IO.File]::ReadAllText($saved)
        foreach ($option in @('NoWarn32', 'TextDisableHack', 'NoPrefPanel', 'NoLower', 'PrintingHack', 'DisposHandleHack', 'NoSuspend', 'CloseIsQuit', 'RectScreen')) {
            if ($text -notmatch $option) { throw "Saved ECF lost $option" }
        }
        if ($text -notmatch 'AppleChar=20' -or $text -notmatch 'RightButtonModifier=256') { throw 'Saved preferences lost scalar settings' }
        if ($text -notmatch 'SysZone=8388608' -or $text -notmatch 'ApplZone=16777216') { throw 'Saved preferences lost heap sizes' }
        if ($text -notmatch '\[APPL.PPRB\]' -or $text -match 'Config000000=') { throw 'Saved preferences did not use readable type/creator settings' }

        # Reload the dialog snapshot through the real startup parser.
        [IO.File]::WriteAllText($privatePrefs, $hostSettings + $text)
        $commands = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_launch_ppc_fragment
commands
silent
if ROMlib_delay != 17 || ROMlib_AppleChar != 20 || ROMlib_right_button_modifier != 256 || ROMlib_nowarn32 != 1 || (ROMlib_options & 0x1fe00000) != 0x1fe00000
quit 1
end
printf "PREFS_ROUNDTRIP_OK\n"
quit 0
end
run -syszone 8m -applzone 16m "APP"
'@
        [IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
        $probe = & $runProbe
        $output = $probe.Output
        if ($probe.ExitCode -ne 0 -or $output -notmatch 'PREFS_ROUNDTRIP_OK') { throw "Saved preferences reload failed: $output" }

        # Original probe files use APPL/PPRB Finder information.
        foreach ($case in @(
                @{Version = '0x860'; Flags = '0xc000000'; Settings = "[Defaults]`nSystemVersion=8.6.0`nCloseIsQuit=1`nNoSuspend=1`n" },
                @{Version = '0x910'; Flags = '0'; Settings = "[Defaults]`nSystemVersion=8.6.0`nCloseIsQuit=1`nNoSuspend=1`n`n[APPL.PPRB]`nSystemVersion=9.1.0`nOptions=0`n" }
            )) {
            [IO.File]::WriteAllText($privatePrefs, $hostSettings + $case.Settings)
            $commands = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_launch_ppc_fragment
commands
silent
if system_version != VERSION || ROMlib_options != FLAGS
printf "ECF mismatch system=%x flags=%x\n",system_version,ROMlib_options
quit 1
end
printf "ECF_LOOKUP_OK\n"
quit 0
end
run -syszone 8m -applzone 64m "APP"
'@
            [IO.File]::WriteAllText($script, $commands.Replace('FOLDER', $folder).Replace('APP', $app).Replace('VERSION', $case.Version).Replace('FLAGS', $case.Flags))
            $probe = & $runProbe
            $output = $probe.Output
            if ($probe.ExitCode -ne 0 -or $output -notmatch 'ECF_LOOKUP_OK') { throw "Application preferences lookup failed: $output" }
        }
        Write-Output 'Preferences save, scalar/flag persistence, defaults and application overrides passed'

        # Direct statements retain spaces in FourCCs, embedded equals and hex selectors.
        [IO.File]::WriteAllText($privatePrefs, $hostSettings + "[APPL.PPRB]`nGestalt('tst ')={0,0x12345678};`nGestalt('a=b ')={-5551,0};`nGestalt(0x00747374)={0,7};`n")
        $commands = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_launch_ppc_fragment
commands
silent
set $response=(LONGINT *)malloc(4)
set $result=Gestalt(0x74737420,$response)
if $result != 0 || *(unsigned int *)$response != 0x78563412
quit 1
end
set $result=Gestalt(0x613d6220,$response)
if $result != -5551
quit 1
end
set $result=Gestalt(0x00747374,$response)
if $result != 0 || *(unsigned int *)$response != 0x07000000
quit 1
end
call (void)free($response)
printf "DIRECT_GESTALT_OK\n"
quit 0
end
run -syszone 8m -applzone 64m "APP"
'@
        [IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
        $probe = & $runProbe
        $output = $probe.Output
        if ($probe.ExitCode -ne 0 -or $output -notmatch 'DIRECT_GESTALT_OK') { throw "Direct Gestalt preferences failed: $output" }
        Write-Output 'Direct Gestalt statements: FourCC spaces, equals, hex selectors, errors and responses passed'

        $cases = @(
            @{ Config = 'SysZone = 8m; ApplZone = 16m;'; Args = ''; System = 8388608; App = 16777216; Restart = $true },
            @{ Config = 'SysZone = "8m"; ApplZone = "16m";'; Args = ''; System = 8388608; App = 16777216; Restart = $true },
            @{ Config = 'SysZone = 131073; ApplZone = 512k;'; Args = ''; System = 139264; App = 524288; Restart = $true },
            @{ Config = 'SysZone = 4m; ApplZone = 8m; SysZone = 1; ApplZone = 384k;'; Args = ''; System = 4194304; App = 8388608; Restart = $true },
            @{ Config = 'SysZone = 8m; ApplZone = 16m;'; Args = '-syszone 12m'; System = 12582912; App = 16777216; Restart = $true },
            @{ Config = 'SysZone = 8m; ApplZone = 16m;'; Args = '-applzone 20m'; System = 8388608; App = 20971520; Restart = $true },
            @{ Config = 'SysZone = 8m; ApplZone = 16m;'; Args = '-syszone 12m -applzone 20m'; System = 12582912; App = 20971520; Restart = $false },
            @{ Config = ('SysZone = 4m; ApplZone = 8m; SysZone = ' + ('9' * 1000000) + '; // no final newline'); Args = ''; System = 4194304; App = 8388608; Restart = $true }
        )
        foreach ($case in $cases) {
            [IO.File]::WriteAllText($privatePrefs, $hostSettings + "[APPL.PPRB]`n" + $case.Config.Replace("; ", "`n").Replace(";", ""))
            $stop = if ($case.Restart) { 'ROMlib_win_relaunch' } else { 'ROMlib_launch_ppc_fragment' }
            $sysvar = if ($case.Restart) { 'system' } else { 'ROMlib_syszone_size' }
            $appvar = if ($case.Restart) { 'application' } else { 'ROMlib_applzone_size' }
            $commands = @'
set pagination off
set confirm off
set print thread-events off
break STOP
commands
silent
if SYSVAR != SYSTEM || APPVAR != APPLICATION
printf "ECF heap mismatch sys=%d app=%d\n",SYSVAR,APPVAR
quit 1
end
printf "ECF_MEMORY_OK\n"
quit 0
end
run ARGS "APP"
'@
            $commands = $commands.Replace('FOLDER', $folder).Replace('STOP', $stop).Replace('SYSVAR', $sysvar).Replace('APPVAR', $appvar).Replace('SYSTEM', [string]$case.System).Replace('APPLICATION', [string]$case.App).Replace('ARGS', $case.Args).Replace('APP', $app)
            [IO.File]::WriteAllText($script, $commands)
            $probe = & $runProbe
            $output = $probe.Output
            if ($probe.ExitCode -ne 0 -or $output -notmatch 'ECF_MEMORY_OK') { throw "ECF heap case failed: $output" }
        }
        Write-Output 'ECF heap suffixes, quoted values, alignment, malformed/oversized values and CLI precedence passed'
        $commands = @'
set pagination off
set confirm off
set print thread-events off
set $disposals=0
break C_DisposeWindow
commands
silent
set $disposals=$disposals+1
continue
end
run -syszone 8m -applzone 64m -system 9.2.2 "APP"
if $_exitcode != 0 || $disposals != 1
quit 1
end
printf "ECF_WINDOW_LIFECYCLE_OK\n"
quit 0
'@
        [IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
        $probe = & $runProbe
        $output = $probe.Output
        if ($probe.ExitCode -ne 0 -or $output -notmatch 'ECF_WINDOW_LIFECYCLE_OK') {
            throw "ECF window lifecycle failed: $output"
        }
        Write-Output 'ECF fixture: real PPC window creation, completed disposal and clean process exit passed'
    }
    finally {
        Pop-Location
        try {
            # Heap restarts can outlive the debugger. Only stop processes using this fixture's app.
            Get-CimInstance Win32_Process -Filter "Name='ROMAW.exe'" | Where-Object {
                $_.ExecutablePath -eq $ROMAW -and $_.CommandLine -and
                $_.CommandLine.Replace('\', '/').IndexOf($fixture.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0
            } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
        }
        finally {
            foreach ($name in $environment.Keys) {
                $ecfValue = $previousEnvironment[$name]
                if ($null -eq $ecfValue) { $ecfValue = [NullString]::Value }
                [Environment]::SetEnvironmentVariable($name, $ecfValue, 'Process')
                if ([Environment]::GetEnvironmentVariable($name, 'Process') -cne $previousEnvironment[$name]) {
                    throw "ECF probe did not restore the caller's environment: $name"
                }
            }
        }
    }

}

function Invoke-Check-build-version {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $stamp = Join-Path (Split-Path $ROMAW -Parent) 'build-version.txt'
    $expected = if (Test-Path -LiteralPath $stamp) { (Get-Content -Raw -LiteralPath $stamp).Trim() } else {
        'ROMAW version ' + (& git -C $workspace rev-parse --short=12 HEAD)
    }
    if ($expected -notmatch '^ROMAW version ([0-9a-f]{12,40})$') { throw 'Invalid build version stamp' }
    $revision = $Matches[1]
    $kind = & git -C $workspace cat-file -t $revision
    if ($LASTEXITCODE -ne 0 -or $kind -ne 'commit') { throw 'Build version does not identify a Git commit' }
    $properties = [Diagnostics.FileVersionInfo]::GetVersionInfo($ROMAW)
    if ($properties.FileVersion -cne $expected -or $properties.ProductVersion -cne $expected) {
        throw 'Windows version properties do not match the build version'
    }
    $savedVideoDriver = $env:SDL_VIDEODRIVER
    try {
        $env:SDL_VIDEODRIVER = 'romaw-version-test-invalid'
        foreach ($arguments in @('-version', '--version', '-debug trace --version')) {
            $fixture = Join-Path $workspace ('build/version-string.' + [guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Path $fixture | Out-Null
            $output = Join-Path $fixture 'version.txt'
            $errors = Join-Path $fixture 'errors.txt'
            $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors
            $null = $process.Handle
            if (-not $process.WaitForExit(15000)) { $process.Kill(); throw 'Version output did not exit promptly' }
            if ($process.ExitCode -ne 0 -or (Get-Content -Raw -LiteralPath $output).Trim() -cne $expected) {
                throw 'Command-line version does not match the build version'
            }
            if ((Get-Item -LiteralPath $errors).Length -ne 0) { throw 'Version output wrote unexpected diagnostics' }
            foreach ($name in @('stdout.txt', 'stderr.txt', 'ROMAW.prefs')) {
                if (Test-Path -LiteralPath (Join-Path $fixture $name)) { throw 'Version request initialized the runtime' }
            }
            Write-Output ('PASS: ' + $arguments + ', CLI and Windows properties identify ' + $expected)
        }
    }
    finally { $env:SDL_VIDEODRIVER = $savedVideoDriver }
}


function Invoke-Check-help {
    param([string]$ROMAW)

    $ErrorActionPreference = 'Stop'
    $repo = Split-Path $PSScriptRoot -Parent
    $workspace = Split-Path $repo -Parent
    if (-not $ROMAW) {
        $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe'
    }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\help-test.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $cases = @(
        @{Name = 'long'; Arguments = '--help' },
        @{Name = 'legacy'; Arguments = '-help' },
        @{Name = 'debug'; Arguments = '-debug trace --help' }
    )
    $savedVideoDriver = $env:SDL_VIDEODRIVER
    try {
        # Help must work without a usable video driver or runtime working files.
        $env:SDL_VIDEODRIVER = 'romaw-help-test-invalid'
        foreach ($case in $cases) {
            $output = Join-Path $fixture ($case.Name + '.stdout')
            $errors = Join-Path $fixture ($case.Name + '.stderr')
            $process = Start-Process -FilePath $ROMAW -ArgumentList $case.Arguments `
                -WorkingDirectory $fixture -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $output -RedirectStandardError $errors
            $null = $process.Handle
            if (-not $process.WaitForExit(10000)) {
                Stop-Process -Id $process.Id
                throw "$($case.Arguments) did not exit before initializing the emulator"
            }
            if ($process.ExitCode -ne 0) {
                throw "$($case.Arguments) exited with $($process.ExitCode)"
            }
            $text = [IO.File]::ReadAllText($output)
            foreach ($option in @('-help', '-debug', '-size', '-window-size', '-fixed-window-size', '-display-scale', '-bpp', '-system', '-ramlog', '-netatalk', '-afpd', '-basiliskii', '-executor')) {
                if (-not $text.Contains($option)) {
                    throw "$($case.Arguments) omitted $option from help"
                }
            }
            foreach ($removed in @('-browser', '-nobrowser')) {
                if ($text.Contains($removed)) { throw "Help still advertises retired option $removed" }
            }
            if ((Get-Item -LiteralPath $errors).Length -ne 0) {
                throw "$($case.Arguments) wrote unexpected startup diagnostics"
            }
            foreach ($name in @('stdout.txt', 'stderr.txt', 'failure.txt', 'ROMAW.prefs')) {
                if (Test-Path -LiteralPath (Join-Path $fixture $name)) {
                    throw "$($case.Arguments) created $name"
                }
            }
            Write-Output "$($case.Arguments): complete help, exit 0, no startup or diagnostic files"
        }
    }
    finally {
        $env:SDL_VIDEODRIVER = $savedVideoDriver
    }
    Write-Output 'Command-line help regression tests passed'
}

function Invoke-Check-sidecar-options {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/sidecar-options-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    # Inspect actual parsed startup state before any graphical initialization.
    $cases = @(
        @{ Arguments = ''; Prefix = '._' },
        @{ Arguments = '-netatalk'; Prefix = '.AppleDouble/' },
        @{ Arguments = '-afpd'; Prefix = '.AppleDouble/' },
        @{ Arguments = '-netatalk -afpd'; Prefix = '.AppleDouble/' },
        @{ Arguments = '-basiliskii'; Prefix = '.finf/' },
        @{ Arguments = '-executor'; Prefix = '%' }
    )
    $commands = @('set pagination off', 'set confirm off', 'set print thread-events off')
    foreach ($case in $cases) {
        $commands += @(
            'tbreak ROMlib_ecf_startup_memory',
            ('run ' + $case.Arguments),
            ('python assert gdb.parse_and_eval("apple_double_fork_prefix").string() == "' + $case.Prefix + '"'),
            ('python assert int(gdb.parse_and_eval("apple_double_fork_prefix_length")) == ' + $case.Prefix.Length),
            'python assert int(gdb.parse_and_eval("apple_double_quote_char")) == 37',
            'python assert int(gdb.parse_and_eval("netatalk_conventions_p")) == 1',
            'kill'
        )
    }
    $commands += @('echo SIDECAR_OPTIONS_OK\n', 'quit 0')
    $script = Join-Path $fixture 'options.gdb'
    [IO.File]::WriteAllText($script, ($commands -join "`n") + "`n")
    $log = Join-Path $fixture 'options.log'
    $process = Start-Process -FilePath $Gdb -ArgumentList @('-q', '-batch', '-x', ('"' + $script + '"'), ('"' + $ROMAW + '"')) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture 'options.err')
    try {
        if (-not $process.WaitForExit(60000)) { throw "Sidecar option probe timed out; see $fixture" }
        if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'SIDECAR_OPTIONS_OK') {
            throw "Sidecar option probe failed; see $fixture"
        }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
    $conflicts = @('-basiliskii -executor', '-executor -basiliskii', '-netatalk -basiliskii',
        '-afpd -basiliskii', '-executor -netatalk', '-executor -afpd')
    foreach ($arguments in $conflicts) {
        $errors = Join-Path $fixture 'stderr.txt'
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace ' + $arguments) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(10000)) { throw "Conflicting sidecar options did not exit: $arguments" }
            if ($process.ExitCode -ne -10 -or [IO.File]::ReadAllText($errors) -notmatch 'Choose only one sidecar format') {
                throw "Conflicting sidecar options were not rejected: $arguments; see $fixture"
            }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
    Write-Output "PASS: six sidecar startup settings and six conflicting selections; $fixture"
}

function Invoke-Check-launch-desk-accessory {
    param(
        [string]$ROMAW,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    # Build system/tests/startup-display first, using its README instructions.
    $fixture = Join-Path $root ('build/launch-desk-accessory.' + [guid]::NewGuid().ToString('N'))
    & python (Join-Path $PSScriptRoot 'create-launch-desk-accessory-probe.py') $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Accessory fixture generation failed' }
    foreach ($test in @('parent', 'child')) {
        $log = Join-Path $fixture "$test.log"
        $script = Join-Path $fixture "$test.gdb"
        $process = Start-Process -FilePath $Gdb -ArgumentList @('-q', '-batch', '-x', "`"$script`"", "`"$ROMAW`"") -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture "$test.stderr.log")
        if (-not $process.WaitForExit(60000)) {
            Stop-Process -Id $process.Id -Force
            throw "Accessory $test test timed out; see $fixture"
        }
        if ($process.ExitCode -ne 0) { throw "Accessory $test test failed; see $fixture" }
        $output = Get-Content -LiteralPath $log -Raw
        $markers = if ($test -eq 'parent') {
            @('LAUNCH_DESK_ACCESSORY_STACK_AND_RESOURCE_OK', 'LAUNCH_DESK_ACCESSORY_NAMED_CHILD_OK')
        }
        else {
            @('LAUNCH_DESK_ACCESSORY_SELECTED_DRIVER_OK')
        }
        foreach ($marker in $markers) {
            if (-not $output.Contains($marker)) { throw "Missing $marker; see $fixture" }
            Write-Output $marker
        }
    }
}

function Invoke-Check-launch-parameters {
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
    $fixture = Join-Path $root ('build/launch-parameters-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    & $Python (Join-Path $PSScriptRoot 'create-launch-parameters-probe.py') $fixture $Application
    if ($LASTEXITCODE -ne 0) { throw 'Unable to generate Launch parameters probe.' }
    $arguments = '-q -batch -x "' + (Join-Path $fixture 'probe.gdb') + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $fixture 'probe.log') -RedirectStandardError (Join-Path $fixture 'probe.err')
    $null = $process.Handle
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id
        throw "Launch parameters probe timed out; see $fixture"
    }
    $output = Get-Content -LiteralPath (Join-Path $fixture 'probe.log') -Raw
    if ($process.ExitCode -ne 0 -or $output -notmatch 'LAUNCH_PARAMETERS_OK') {
        throw "Launch parameters probe failed; see $fixture"
    }
    Write-Output "Launch parameters Pascal dispatch passed: $fixture/probe.log"
}

function Invoke-Check-number-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    foreach ($case in @('-memory +', '-memory -', '-memory .', '-memory 999999999999999999999M',
            '-bpp 2147483648', '-bpp 9223372036854775808K')) {
        $fixture = Join-Path $workspace ('build\numeric-options.' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixture | Out-Null
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace ' + $case) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id
            throw "$case did not exit promptly"
        }
        if ($process.ExitCode -ne -10) {
            throw "$case exited with $($process.ExitCode), expected invalid-option exit -10"
        }
        $errors = Get-Content -LiteralPath (Join-Path $fixture 'stderr.txt') -Raw
        if ($errors.Contains('win_exception')) {
            throw "$case raised a Windows exception"
        }
        Write-Output "PASS: $case rejected gracefully"
    }
}

function Invoke-Check-printer-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    foreach ($case in @('-prres 90000x300', '-prres 300x90000', '-prres 32768x300',
            '-prres 300x32768', '-prres 0x300', '-prres 300x',
            '-prres 300x300junk')) {
        $fixture = Join-Path $workspace ('build\printer-options.' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixture | Out-Null
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace ' + $case) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id
            throw "$case did not exit promptly"
        }
        if ($process.ExitCode -ne -10) {
            throw "$case exited with $($process.ExitCode), expected invalid-option exit -10"
        }
        $errors = Get-Content -LiteralPath (Join-Path $fixture 'stderr.txt') -Raw
        if ($errors.Contains('win_exception')) {
            throw "$case raised a Windows exception"
        }
        Write-Output "PASS: $case rejected gracefully"
    }
}

function Invoke-Check-startup-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    foreach ($case in @('-browser "ROMAW Finder"', '-nobrowser')) {
        $fixture = Join-Path $workspace ('build\startup-options.' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixture | Out-Null
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace ' + $case) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id
            throw "$case did not exit promptly"
        }
        if ($process.ExitCode -ne -10) {
            throw "$case exited with $($process.ExitCode), expected invalid-option exit -10"
        }
        $errors = Get-Content -LiteralPath (Join-Path $fixture 'stderr.txt') -Raw
        if ($errors.Contains('win_exception')) {
            throw "$case raised a Windows exception"
        }
        Write-Output "PASS: $case rejected gracefully"
    }
}

function Invoke-Check-version-options {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    foreach ($case in @('-system 7junk', '-system 7..1', '-system 7.0.1.2',
            '-system 4294967303', '-system 7.0.4294967296',
            '-prvers 4294967303', '-prvers 7.0junk')) {
        $fixture = Join-Path $workspace ('build\version-options.' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixture | Out-Null
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace ' + $case) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(15000)) {
            Stop-Process -Id $process.Id
            throw "$case did not exit promptly"
        }
        if ($process.ExitCode -ne -10) {
            throw "$case exited with $($process.ExitCode), expected invalid-option exit -10"
        }
        $errors = Get-Content -LiteralPath (Join-Path $fixture 'stderr.txt') -Raw
        if ($errors.Contains('win_exception')) {
            throw "$case raised a Windows exception"
        }
        if ($case.StartsWith('-system ') -and -not $errors.Contains('invalid version')) {
            throw "$case did not report its invalid system version"
        }
        Write-Output "PASS: $case rejected gracefully"
    }
}

function Invoke-Check-clipboard-live {
    param(
        [switch]$AllowClipboardChanges,
        [string]$ROMAW,
        [string]$Application,
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe'
    )
    $ErrorActionPreference = 'Stop'
    if (-not $AllowClipboardChanges) {
        throw 'This probe replaces the real Windows clipboard. Pass -AllowClipboardChanges to run it.'
    }
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) { $Application = Join-Path $env:USERPROFILE '.romaw/Apps/SimpleText' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $Application = (Resolve-Path -LiteralPath $Application).Path
    $fixture = Join-Path $root ('build/clipboard-live-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $fixture | Out-Null
    $source = (Join-Path $PSScriptRoot 'clipboard_live_probe.py').Replace('\', '/')
    $commands = @(
        'set pagination off',
        'set confirm off',
        'set print thread-events off',
        'set python print-stack full',
        ('set substitute-path /mnt/d/Source/ROMAW "' + $root.Replace('\', '/') + '"'),
        'tbreak C_WaitNextEvent',
        ('run -syszone 8m -applzone 64m -sdlaudio test "' + $Application.Replace('\', '/') + '"'),
        ('python exec(compile(open(r"' + $source + '", encoding="utf-8").read(), r"' + $source + '", "exec"))'),
        'quit 0'
    ) -join "`n"
    $script = Join-Path $fixture 'probe.gdb'
    [IO.File]::WriteAllText($script, $commands + "`n")
    $log = Join-Path $fixture 'probe.log'
    $process = Start-Process -FilePath $Gdb -ArgumentList @('-q', '-batch', '-x', $script, $ROMAW) -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture 'probe.err')
    try {
        if (-not $process.WaitForExit(90000)) { throw "Live clipboard probe timed out; see $fixture" }
        if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch 'CLIPBOARD_LIVE_OK') {
            throw "Live clipboard probe failed; see $fixture"
        }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
    Write-Output "PASS: real Windows text, custom scrap, PICT and DIB transfers; $log"
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('build-version', 'clipboard-live', 'desk-accessory', 'device-install', 'ecf', 'help', 'launch-desk-accessory', 'launch-parameters', 'number-options', 'printer-options', 'preferences-parser', 'sidecar-options', 'startup-options', 'version-options') -CheckArguments $args
