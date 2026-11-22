# +===================================================================
#   File:      run-tests.ps1
#
#   Summary:   Implements run tests support for regression tests and test fixtures.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in executor/tests/type9-resources; original attribution and provenance
#              comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

param(
    [string]$ROMAW,
    [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
$check = Join-Path $workspace 'build\type9-resource-check'
New-Item -ItemType Directory -Force $check | Out-Null
Copy-Item -LiteralPath (Join-Path $workspace 'build\type9-probe\Type9ResourceProbe.ad') -Destination (Join-Path $check 'Type9ResourceProbe')
Copy-Item -LiteralPath (Join-Path $workspace 'build\type9-probe\%Type9ResourceProbe.ad') -Destination (Join-Path $check '._Type9ResourceProbe')
$app = (Join-Path $check 'Type9ResourceProbe').Replace('\', '/')
$script = Join-Path $check 'test.gdb'
$commands = @'
set pagination off
set confirm off
set print thread-events off
break C_SysBeep
commands
silent
if i < 0
printf "TYPE9_RESOURCE_FAILURE_%d\n", -i
quit 1
end
if i == 777
printf "TYPE9_RESOURCE_OK\n"
quit 0
end
return
continue
end
run "APP"
printf "TYPE9_RESOURCE_ENDED_WITHOUT_SUCCESS\n"
quit 1
'@
[IO.File]::WriteAllText($script, $commands.Replace('APP', $app))
Push-Location $check
try {
    $output = & $Gdb -q -batch -x $script $ROMAW 2>&1 | Out-String
    $status = $LASTEXITCODE
}
finally { Pop-Location }
[IO.File]::WriteAllText((Join-Path $check 'gdb.log'), $output)
if ($status -ne 0 -or $output -notmatch 'TYPE9_RESOURCE_OK') { throw "Type-9 resource probe failed: $output" }
Write-Output 'Type-9 resource decompression and malformed-header tests passed.'
