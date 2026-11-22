# +===================================================================
#   File:      check-hardware.ps1
#
#   Summary:   Run hardware regression checks with isolated function scopes.
#
#   Classes:   None.
#
#   Functions: Script entry point and named regression checks; use --list.
#
#   Origin:    Consolidated repository regression checks; original file-specific notices remain below.
#
#   Copyright and Legal notices: Applicable repository license: syn68k/COPYING.
# ===================================================================+

function Invoke-Check-mac-branch {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-branch.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-branch-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate Branch comparison' }
    $result = Join-Path $fixture '68K Branch Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K Relative Branch Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'Branch comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "Branch comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'Branch comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-bsr {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-bsr.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-bsr-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate BSR comparison' }
    $result = Join-Path $fixture '68K BSR Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K BSR Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'BSR comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "BSR comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'BSR comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-control-transfer {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-control-transfer.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-control-transfer-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate JMP-JSR comparison' }
    $result = Join-Path $fixture '68K JMP-JSR Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K JMP-JSR Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'JMP-JSR comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "JMP-JSR comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'JMP-JSR comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-dbcc {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-dbcc.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-dbcc-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate DBcc comparison' }
    $result = Join-Path $fixture '68K DBcc Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K DBcc Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'DBcc comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "DBcc comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'DBcc comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-divide {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-divide.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-divide-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate Divide comparison' }
    $result = Join-Path $fixture '68K Divide Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K Divide Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'Divide comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "Divide comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'Divide comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-divide-word {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-divide-word.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-divide-word-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate Divide comparison' }
    $result = Join-Path $fixture '68K Word Divide Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K Word Divide Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'Divide comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "Divide comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'Divide comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-lea {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-lea.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-lea-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate LEA comparison' }
    $result = Join-Path $fixture '68K LEA Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K LEA Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'LEA comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "LEA comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'LEA comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-link {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-link.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-link-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate LINK comparison' }
    $result = Join-Path $fixture '68K LINK Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K LINK Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'LINK comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "LINK comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'LINK comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-movem {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-movem.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-movem-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate MOVEM comparison' }
    $result = Join-Path $fixture '68K MOVEM Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K MOVEM Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'MOVEM comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "MOVEM comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'MOVEM comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-movep {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-movep.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-movep-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate MOVEP comparison' }
    $result = Join-Path $fixture '68K MOVEP Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K MOVEP Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'MOVEP comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "MOVEP comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'MOVEP comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-multiply {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-multiply.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-multiply-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate Multiply comparison' }
    $result = Join-Path $fixture '68K Multiply Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K Multiply Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'Multiply comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "Multiply comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'Multiply comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-multiply-word {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-multiply-word.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-multiply-word-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate Multiply comparison' }
    $result = Join-Path $fixture '68K Word Multiply Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K Word Multiply Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'Multiply comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "Multiply comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'Multiply comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

function Invoke-Check-mac-pea {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-pea.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-pea-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate PEA comparison' }
    $result = Join-Path $fixture '68K PEA Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-notnative')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture '68K PEA Comparison') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'PEA comparison timed out' }
            if ($process.ExitCode -ne 0) { throw "PEA comparison exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result
            if ($LASTEXITCODE -ne 0) { throw 'PEA comparison results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('mac-branch', 'mac-bsr', 'mac-control-transfer', 'mac-dbcc', 'mac-divide', 'mac-divide-word', 'mac-lea', 'mac-link', 'mac-movem', 'mac-movep', 'mac-multiply', 'mac-multiply-word', 'mac-pea') -CheckArguments $args
