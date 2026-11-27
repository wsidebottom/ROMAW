# +===================================================================
#   File:      run-mactcp-ppc.ps1
#
#   Summary:   Run bounded Windows PowerPC MacTCP interpreter and JIT probes.
#
#   Classes:   None.
#
#   Functions: Script entry point.
#
#   Origin:    ROMAW original Windows guest regression harness.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
param(
    [Parameter(Mandatory = $true)][string]$ROMAW,
    [Parameter(Mandatory = $true)][string]$Probe
)
$ErrorActionPreference = 'Stop'
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
$Probe = (Resolve-Path -LiteralPath $Probe).Path
foreach ($mode in @('no-ppc-jit', 'ppc-jit')) {
    $directory = Join-Path (Split-Path -Parent $Probe) $mode
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    foreach ($name in @('stdout.txt', 'stderr.txt')) {
        $log = Join-Path $directory $name
        if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    }
    $process = Start-Process -FilePath $ROMAW -ArgumentList ('-' + $mode + ' -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $Probe + '"') -WorkingDirectory $directory -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(40)
    $text = ''
    try {
        do {
            Start-Sleep -Milliseconds 100
            $text = (@('stdout.txt', 'stderr.txt') | ForEach-Object {
                    $log = Join-Path $directory $_
                    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Raw }
                }) -join "`n"
            if ($text -match 'PowerPC launch failed|win_exception|PPC MacTCP Probe failed') {
                throw "PowerPC MacTCP $mode failed; see $directory"
            }
            if ($text -match 'PPC MacTCP Probe passed') { break }
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($text -notmatch 'PPC MacTCP Probe passed') { throw "PowerPC MacTCP $mode timed out" }
        if (!$process.WaitForExit(5000)) { throw "PowerPC MacTCP $mode did not exit" }
        if ($process.ExitCode -ne 0) { throw "PowerPC MacTCP $mode exited $($process.ExitCode)" }
        Write-Output "PASS PowerPC MacTCP $mode"
    }
    finally {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
