# +===================================================================
#   File:      test-clean-build.ps1
#
#   Summary:   Implements test clean build support for ROMAW build and development support.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root 'build'
$fixture = Join-Path $build ('clean-build-test.' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path (Join-Path $fixture 'tools') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'clean-build.ps1') -Destination (Join-Path $fixture 'tools')
$script = Join-Path $fixture 'tools/clean-build.ps1'
$old = (Get-Date).AddDays(-3)
function Write-Fixture([string]$Relative) {
    $path = Join-Path $fixture $Relative
    $null = New-Item -ItemType Directory -Path (Split-Path $path) -Force
    [IO.File]::WriteAllText($path, 'generated test bytes')
    (Get-Item -LiteralPath $path).LastWriteTime = $old
    return $path
}
$oldProbe = Write-Fixture ('build/cfm68k-flat.' + ('1' * 32) + '/probe')
$recentProbe = Write-Fixture ('build/ppc-launch.' + ('2' * 32) + '/recent.txt')
$activeProbe = Write-Fixture ('build/ppc-launch.' + ('3' * 32) + '/stdout.txt')
$backup = Write-Fixture 'build/Apps-backup/keep.txt'
$history = Write-Fixture 'build/git-history/keep.bundle'
$testObject = Write-Fixture 'build/rect-probe.o'
$testExe = Write-Fixture 'synppc/obj/ppc-runtime-test.exe'
$nestedTest = Write-Fixture 'syn68k/obj/tests/mmu040/opcodes.o'
$oracle = Write-Fixture 'syn68k/obj/tests/reference/hardware.txt'
$cache = Write-Fixture 'synppc/obj/ppc-aot-test.cache.memory.indexed'
$runtimeObject = Write-Fixture 'synppc/obj/ppc-runtime.o'
$staged = Write-Fixture 'build/ROMAW.exe'
$reference = Write-Fixture 'reference/keep.txt'
$linkedProbe = Split-Path (Write-Fixture ('build/cfm68k-flat.' + ('4' * 32) + '/probe'))
$junction = Join-Path $linkedProbe 'linked-reference'
$null = New-Item -ItemType Junction -Path $junction -Target (Split-Path $reference)
Get-ChildItem -LiteralPath $fixture -Directory -Recurse | Where-Object {
    -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
} | ForEach-Object { $_.LastWriteTime = $old }
(Get-Item -LiteralPath $recentProbe).LastWriteTime = Get-Date
(Get-Item -LiteralPath (Split-Path $recentProbe)).LastWriteTime = Get-Date
# An old directory with a freshly written log is still active.
(Get-Item -LiteralPath $activeProbe).LastWriteTime = Get-Date
& $script -MinimumAgeHours 1 -IncludeTestArtifacts
if (-not (Test-Path -LiteralPath $oldProbe) -or -not (Test-Path -LiteralPath $testObject)) {
    throw 'Preview modified a fixture.'
}
& $script -MinimumAgeHours 1 -IncludeTestArtifacts -Apply
foreach ($path in @($oldProbe, $testObject, $testExe, $nestedTest, $cache)) {
    if (Test-Path -LiteralPath $path) { throw "Old test product was retained: $path" }
}
foreach ($path in @($recentProbe, $activeProbe, $backup, $history, $runtimeObject, $oracle, $staged, $reference, $linkedProbe)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Protected fixture was removed: $path" }
}
if (-not ((Get-Item -LiteralPath $junction -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Test junction changed before cleanup.'
}
# Windows PowerShell 5.1 Remove-Item can throw on a directory junction.
# Non-recursive Directory.Delete removes this link without following its target.
[IO.Directory]::Delete($junction)
$resolved = [IO.Path]::GetFullPath($fixture)
if ([IO.Path]::GetDirectoryName($resolved) -ne $build) { throw 'Test cleanup escaped build.' }
Remove-Item -LiteralPath $resolved -Recurse -Force
Write-Output 'PASS: preview, stale fixtures/test objects, recent logs, backups, runtime objects and junction guards'
