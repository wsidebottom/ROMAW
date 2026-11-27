# +===================================================================
#   File:      check-graphics.ps1
#
#   Summary:   Run graphics regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-Check-pixpat-lifetime {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $probe = (Join-Path $PSScriptRoot 'pixpat_lifetime_probe.py').Replace('\', '/')
    $output = & $Gdb -batch -ex 'set pagination off' -ex 'set confirm off' `
        -ex 'break romaw_finder_main' -ex 'run -sdlaudio test' `
        -ex "source $probe" -ex 'quit' $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/pixpat-lifetime-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'PIXPAT_LIFETIME_OK:')) {
        throw "Pixel-pattern lifetime checks failed; see $log"
    }
    Write-Output 'Installed PixPat disposal, shared ports, replacement patterns and pixels passed on Windows.'
}

function Invoke-Check-window-exposure {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $probe = (Join-Path $PSScriptRoot 'window_exposure_probe.py').Replace('\', '/')
    $output = & $Gdb -batch -ex 'set pagination off' -ex 'set confirm off' `
        -ex 'break romaw_finder_main' -ex 'run -sdlaudio test' `
        -ex "source $probe" -ex 'quit' $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/window-exposure-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'WINDOW_EXPOSURE_OK:')) {
        throw "Window exposure repaint failed; see $log"
    }
    Write-Output 'Window exposure pixels, visibility variants and update regions passed on Windows.'
}

function Invoke-Check-copybits-colorize {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $probe = (Join-Path $PSScriptRoot 'copybits_colorize_probe.py').Replace('\', '/')
    $output = & $Gdb -batch -ex 'set pagination off' -ex 'set confirm off' `
        -ex 'break romaw_finder_main' -ex 'run -sdlaudio test' `
        -ex "source $probe" -ex 'quit' $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/copybits-colorize-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'COPYBITS_COLORIZE_OK:')) {
        throw "Indexed CopyBits colorization failed; see $log"
    }
    Write-Output 'Indexed CopyBits RGB colorization and source/port preservation passed on Windows.'
}

function Invoke-Check-monochrome-mask {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $probe = (Join-Path $PSScriptRoot 'monochrome_mask_probe.py').Replace('\', '/')
    $output = & $Gdb -batch -ex 'set pagination off' -ex 'set confirm off' `
        -ex 'break romaw_finder_main' -ex 'run -sdlaudio test' `
        -ex "source $probe" -ex 'quit' $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/monochrome-mask-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'MONOCHROME_MASK_TRANSPARENCY_OK')) {
        throw "Monochrome mask transparency failed; see $log"
    }
    Write-Output 'One-bit mask/paint and CopyMask coverage passed for 8/32-bit destinations and reversed port colors.'
}

function Invoke-Check-gworld-staged {
    param(
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [string]$Executable = '',
        [string]$Application = ''
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Application) {
        $Application = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.romaw/Apps/Browser'
    }
    if (-not (Test-Path -LiteralPath $Application)) { throw 'The Browser probe application is unavailable.' }
    $arguments = 'set args -syszone 8m -applzone 64m "' + $Application.Replace('\', '/') + '"'
    $output = & $Gdb -batch -ex $arguments -x (Join-Path $PSScriptRoot 'gworld_output_probe.gdb') $Executable 2>&1
    $status = $LASTEXITCODE
    $log = Join-Path $root 'build/gworld-staged-test.log'
    $output | Set-Content -LiteralPath $log
    if ($status -ne 0 -or -not ($output -match 'LIVE_NEW_UPDATE_GWORLD_GUARDS_OK')) {
        throw "Staged GWorld output checks failed; see $log"
    }
    Write-Output 'Staged NewGWorld and UpdateGWorld preserve adjacent guest memory.'
}

function Invoke-Check-quickdraw-host {
    param(
        [string]$Executable = '',
        [string]$Module = '',
        [string]$Marker = 'QUICKDRAW_HOST_PROBE_OK',
        [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe',
        [switch]$NoTrace
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $Executable) { $Executable = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    if (-not $Module) { $Module = Join-Path $root 'build/quickdraw-host-probe/quickdraw-host-probe.dll' }
    $Executable = (Resolve-Path -LiteralPath $Executable).Path
    $Module = (Resolve-Path -LiteralPath $Module).Path
    $fixture = Join-Path $root ('build/quickdraw-host-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
    $preferences = [string]::Join([Environment]::NewLine, @('[HostDrives]', 'SidecarDrives=none', ''))
    [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), $preferences)
    $commands = @'
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set environment USERPROFILE @PROFILE@
break romaw_finder_main
run -sdlaudio test @DEBUG@
python
import gdb
assert int(gdb.parse_and_eval('(int)ROMlib_desktop_is_ui_thread()')) == 1
path = @MODULE@
encoded = path.encode('utf-16le') + b'\0\0'
name = b'ROMAWGraphicsProbe\0'
address = int(gdb.parse_and_eval('(void *)malloc(%d)' % (len(encoded) + len(name))))
assert address, 'probe path allocation failed'
gdb.selected_inferior().write_memory(address, encoded + name)
module = int(gdb.parse_and_eval('(unsigned long long)LoadLibraryW((void *)%d)' % address))
assert module, 'cannot load graphics probe DLL'
entry = int(gdb.parse_and_eval('(unsigned long long)GetProcAddress((void *)%d,(char *)%d)' % (module, address + len(encoded))))
gdb.execute('call (void)free((void *)%d)' % address, to_string=True)
assert entry, 'graphics probe entry is missing'
result = int(gdb.parse_and_eval('((int (*)(void))%d)()' % entry))
assert result == 0, 'graphics probe failed: %d' % result
print(@MARKER@, flush=True)
end
quit 0
'@
    $commands = $commands.Replace('@PROFILE@', $fixture.Replace('\', '/'))
    $commands = $commands.Replace('@DEBUG@', $(if ($NoTrace) { '' } else { '-debug trace' }))
    $commands = $commands.Replace('@MODULE@', (ConvertTo-Json -InputObject $Module.Replace('\', '/') -Compress))
    $commands = $commands.Replace('@MARKER@', (ConvertTo-Json -InputObject $Marker -Compress))
    $script = Join-Path $fixture 'probe.gdb'
    [IO.File]::WriteAllText($script, $commands)
    $log = Join-Path $fixture 'probe.log'
    $arguments = '-q -batch -x "' + $script + '" "' + $Executable + '"'
    $process = Start-Process -FilePath $Gdb -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $fixture 'probe.err')
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(60000)) { throw "Graphics probe timed out; see $fixture" }
        if ($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($log) -notmatch [regex]::Escape($Marker)) {
            throw "Graphics probe failed; see $fixture"
        }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
    Write-Output "$Marker passed through the initialized Finder desktop: $fixture"
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('copybits-colorize', 'monochrome-mask', 'gworld-staged', 'pixpat-lifetime', 'quickdraw-host', 'window-exposure') -CheckArguments $args
