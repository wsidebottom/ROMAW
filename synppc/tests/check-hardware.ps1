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
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

function Invoke-Check-classic-branch {
    param([string]$ROMAW, [switch]$Interpreter, [switch]$Conditional, [switch]$Indirect)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-branch.' + [guid]::NewGuid().ToString('N'))
    if ($Conditional -and $Indirect) { throw 'Choose conditional or indirect branch probes' }
    $generatorName = if ($Indirect) { 'mac-indirect-branch-reference.py' } elseif ($Conditional) { 'mac-conditional-branch-reference.py' } else { 'mac-branch-reference.py' }
    $applicationName = if ($Indirect) { 'PPC Indirect Branch Probe' } elseif ($Conditional) { 'PPC Conditional Branch Probe' } else { 'PPC Branch Hardware Probe' }
    $resultName = if ($Indirect) { 'PPC Indirect Results' } elseif ($Conditional) { 'PPC Conditional Results' } else { 'PPC Branch Results' }
    $generator = Join-Path $PSScriptRoot $generatorName
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic branch application' }
    $app = Join-Path $fixture $applicationName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic branch probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic branch probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Classic branch capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-compare {
    param([string]$ROMAW, [switch]$Interpreter)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-compare.' + [guid]::NewGuid().ToString('N'))
    $generatorName = 'mac-compare-reference.py'
    $appName = 'PPC Compare Probe'
    $resultName = 'PPC Compare Results'
    $generator = Join-Path $PSScriptRoot $generatorName
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic comparison application' }
    $app = Join-Path $fixture $appName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic comparison probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic comparison probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode-binary (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Classic comparison capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-conversion {
    param([string]$ROMAW, [switch]$Interpreter)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-conversion.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-conversion-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic conversion application' }
    $app = Join-Path $fixture 'PPC Conversion Probe'
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic conversion probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic conversion probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode-binary (Join-Path $fixture 'PPC Conversion Results') `
            --compare (Join-Path $PSScriptRoot 'data\ppc-conversion-603-normal.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Classic conversion capture differs from the physical Mac' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-cr {
    param([string]$ROMAW, [switch]$Interpreter, [switch]$Transfers)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-cr.' + [guid]::NewGuid().ToString('N'))
    $generatorName = if ($Transfers) { 'mac-cr-transfer-reference.py' } else { 'mac-cr-reference.py' }
    $applicationName = if ($Transfers) { 'PPC CR Transfer Probe' } else { 'PPC CR Hardware Probe' }
    $resultName = if ($Transfers) { 'PPC CR Transfer Results' } else { 'PPC CR Results' }
    $generator = Join-Path $PSScriptRoot $generatorName
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic CR application' }
    $app = Join-Path $fixture $applicationName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic CR probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic CR probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Classic CR capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-fpr {
    param([string]$ROMAW, [switch]$Interpreter, [switch]$Select, [switch]$Fpscr)
    $ErrorActionPreference = 'Stop'
    if ($Select -and $Fpscr) { throw 'Select only one FPR probe kind' }
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-fpr.' + [guid]::NewGuid().ToString('N'))
    $generatorName = if ($Select) { 'mac-fsel-reference.py' } else { 'mac-fpr-reference.py' }
    $appName = if ($Select) { 'PPC FSEL Probe' } elseif ($Fpscr) { 'PPC FPSCR Move Probe' } else { 'PPC FPR Move Probe' }
    $resultName = if ($Select) { 'PPC FSEL Results' } elseif ($Fpscr) { 'PPC FPSCR Move Results' } else { 'PPC FPR Move Results' }
    $generator = Join-Path $PSScriptRoot $generatorName
    [string[]]$generatorOptions = if ($Fpscr) { @('--fpscr') } else { @() }
    & python $generator $fixture @generatorOptions
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic FPR application' }
    $app = Join-Path $fixture $appName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic FPR probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic FPR probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode-binary (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Classic FPR capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-mcrfs {
    param([string]$ROMAW, [switch]$Interpreter)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-mcrfs.' + [guid]::NewGuid().ToString('N'))
    $generatorName = 'mac-mcrfs-reference.py'
    $appName = 'PPC FPSCR Clear Probe'
    $resultName = 'PPC FPSCR Clear Results'
    $generator = Join-Path $PSScriptRoot $generatorName
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic MCRFS application' }
    $app = Join-Path $fixture $appName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic MCRFS probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic MCRFS probe exited with $($process.ExitCode)" }
        $native = Select-String -LiteralPath (Join-Path $fixture 'stderr.txt') `
            -SimpleMatch 'trace: PPC JIT executed' -Quiet
        if (-not $Interpreter -and -not $native) { throw 'MCRFS probe did not execute native blocks' }
        if ($Interpreter -and $native) { throw 'Interpreter MCRFS probe executed native blocks' }
        & python $generator $fixture --decode-binary (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Classic MCRFS capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-ni {
    param([string]$ROMAW, [switch]$Interpreter, [switch]$UnderflowEnabled)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-ni.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-ni-reference.py'
    [string[]]$generatorOptions = if ($UnderflowEnabled) { @('--underflow-enabled') } else { @() }
    & python $generator $fixture @generatorOptions
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic NI application' }
    $appName = if ($UnderflowEnabled) { 'PPC NI Enabled Probe' } else { 'PPC NI Hardware Probe' }
    $resultName = if ($UnderflowEnabled) { 'PPC NI Enabled Results' } else { 'PPC NI Results' }
    $referenceName = if ($UnderflowEnabled) { 'ppc-ni-603-underflow-enabled.txt' } else { 'ppc-ni-603-normal.txt' }
    $app = Join-Path $fixture $appName
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic NI probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic NI probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode-binary (Join-Path $fixture $resultName)
        if ($LASTEXITCODE -ne 0) { throw 'Could not decode the classic NI capture' }
        $expected = Get-Content -LiteralPath (Join-Path $PSScriptRoot ('data\' + $referenceName))
        $actual = Get-Content -LiteralPath (Join-Path $fixture 'hardware.txt')
        if (Compare-Object $expected $actual -SyncWindow 0) { throw 'Classic NI capture differs from the physical Mac' }
        Write-Output 'All 816 classic NI value/FPSCR cases matched the physical Mac'
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-reciprocal {
    param([string]$ROMAW, [switch]$Interpreter)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-reciprocal.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-reciprocal-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic reciprocal application' }
    $app = Join-Path $fixture 'PPC Reciprocal Probe'
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic reciprocal probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic reciprocal probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode-binary (Join-Path $fixture 'PPC Reciprocal Results') `
            --compare (Join-Path $PSScriptRoot 'data\ppc-fres-603-powers.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Classic reciprocal capture differs from the physical Mac' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-classic-spr {
    param([string]$ROMAW, [switch]$Interpreter)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build\ppc-classic-spr.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-spr-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the classic SPR application' }
    $app = Join-Path $fixture 'PPC SPR Hardware Probe'
    $option = if ($Interpreter) { '-no-ppc-jit ' } else { '' }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ($option + '-debug trace "' + $app + '"') `
        -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $null = $process.Handle
    try {
        if (-not $process.WaitForExit(15000)) { throw 'Classic SPR probe did not finish' }
        if ($process.ExitCode -ne 0) { throw "Classic SPR probe exited with $($process.ExitCode)" }
        & python $generator $fixture --decode (Join-Path $fixture 'PPC SPR Results')
        if ($LASTEXITCODE -ne 0) { throw 'Classic SPR capture did not match' }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Invoke-Check-mac-ni-conversion {
    param([string]$ROMAW, [string]$Reference)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $workspace ('build/mac-ni-conversion.' + [guid]::NewGuid().ToString('N'))
    $generator = Join-Path $PSScriptRoot 'mac-ni-conversion-reference.py'
    & python $generator $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate NI comparison/conversion probe' }
    $referenceArgs = if ($Reference) { @('--reference', (Resolve-Path -LiteralPath $Reference).Path) } else { @() }
    $result = Join-Path $fixture 'PPC NI Compare Convert Results'
    $emptyResult = [byte[]]::new((Get-Item -LiteralPath $result).Length)
    foreach ($mode in @('', '-no-ppc-jit')) {
        [System.IO.File]::WriteAllBytes($result, $emptyResult)
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -sdlaudio test -debug trace "' + (Join-Path $fixture 'PPC NI Compare Convert Probe') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(30000)) { throw 'NI comparison/conversion probe timed out' }
            if ($process.ExitCode -ne 0) { throw "NI comparison/conversion probe exited with $($process.ExitCode)" }
            & python $generator $fixture --decode $result @referenceArgs
            if ($LASTEXITCODE -ne 0) { throw 'NI comparison/conversion probe results differ' }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('classic-branch', 'classic-compare', 'classic-conversion', 'classic-cr', 'classic-fpr', 'classic-mcrfs', 'classic-ni', 'classic-reciprocal', 'classic-spr', 'mac-ni-conversion') -CheckArguments $args
