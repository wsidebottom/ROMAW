# +===================================================================
#   File:      clean-build.ps1
#
#   Summary:   Removes old generated fixtures and optional test products. Preview is default; toolchains, backups,
#              reference captures and runtime build objects are retained.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

# Removes old generated fixtures and optional test products. Preview is default;
# toolchains, backups, reference captures and runtime build objects are retained.
[CmdletBinding()]
param(
    [switch]$Apply,
    [int]$MinimumAgeHours = 24,
    [switch]$IncludeTestArtifacts
)

$ErrorActionPreference = 'Stop'
if ($MinimumAgeHours -lt 1) {
    throw 'MinimumAgeHours must be at least one hour.'
}
$workspace = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $workspace 'build'
$cutoff = (Get-Date).AddHours(-$MinimumAgeHours)
$guidPrefixes = @(
    'help-test', 'ppc-launch', 'ppc-diagnostic', 'ppc-startup-profile', 'ecf-test', 'aot-launch',
    'ppc-classic-branch', 'ppc-classic-compare', 'ppc-classic-conversion', 'ppc-classic-cr',
    'ppc-classic-fpr', 'ppc-classic-mcrfs', 'ppc-classic-ni', 'ppc-classic-reciprocal', 'ppc-classic-spr',
    'mac-branch', 'mac-control-transfer', 'mac-bsr', 'mac-dbcc', 'mac-divide-word', 'mac-divide',
    'mac-pea', 'mac-movem', 'mac-multiply-word', 'mac-lea', 'mac-link', 'mac-multiply', 'mac-movep',
    'mac-ni-conversion', 'fileplay-capture', 'display-scaling', 'display-scale-options',
    'numeric-options', 'printer-options', 'startup-options', 'volume-startup', 'startup-window',
    'version-options', 'window-size-options', 'launch-desk-accessory',
    'cfm68k-flat', 'cfm68k-segmented', 'cfm68k-exit-trap', 'cfm68k-shared', 'cfm68k-classic', 'cfm68k-TextCommon'
)
$temporaryPrefixes = @('generated-files', 'sidecar-test', 'config-prefs', 'object-isolation', 'config-freshness',
    'desktop-database-test', 'diskimage', 'drive-prefs', 'host-trash', 'windows-icons',
    'host-speech', 'window-prefs')
$guidPattern = ($guidPrefixes | ForEach-Object { [regex]::Escape($_) }) -join '|'
$temporaryPattern = ($temporaryPrefixes | ForEach-Object { [regex]::Escape($_) }) -join '|'
$pattern = "^(?:(?:$guidPattern)\.[0-9a-f]{32}|(?:$temporaryPattern)\.[A-Za-z0-9]{6})`$"

function Test-CleanupPath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($workspace + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside workspace: $resolved"
    }
    # Check ancestors too: a regular file inside a junction is unsafe.
    for ($current = $resolved; $current -ne $workspace; $current = [IO.Path]::GetDirectoryName($current)) {
        if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            return $false
        }
    }
    return $true
}

if (-not (Test-CleanupPath $buildRoot)) { throw 'Build directory must not be a junction or symlink.' }
# Running applications may use a fixture without modifying its directory time.
$processes = @(Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -or $_.CommandLine })
$gitRoot = & git -C $workspace rev-parse --show-toplevel
if ($LASTEXITCODE) { throw 'Cannot locate the workspace Git repository.' }
$tracked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
& git -c core.quotepath=false -C $gitRoot ls-files --cached | ForEach-Object {
    $null = $tracked.Add([IO.Path]::GetFullPath((Join-Path $gitRoot $_)))
}
if ($LASTEXITCODE) { throw 'Cannot check tracked files before cleanup.' }
$candidates = @(Get-ChildItem -LiteralPath $buildRoot -Directory | Where-Object {
        $_.Name -cmatch $pattern -and $_.LastWriteTime -lt $cutoff -and
        -not ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint)
    })
$manifest = @()
foreach ($candidate in $candidates) {
    $resolved = [System.IO.Path]::GetFullPath($candidate.FullName)
    if (-not $resolved.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetDirectoryName($resolved) -ne $buildRoot) {
        throw "Refusing path outside build: $resolved"
    }
    if (-not (Test-CleanupPath $resolved)) { continue }
    # Walk without entering reparse points, then refuse the entire linked tree.
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($resolved)
    $children = @()
    $unsafe = $false
    while ($pending.Count -and -not $unsafe) {
        foreach ($child in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint -or
                $child.LastWriteTime -ge $cutoff -or $tracked.Contains($child.FullName)) {
                $unsafe = $true
                break
            }
            if ($child.PSIsContainer) { $pending.Push($child.FullName) }
            else { $children += $child }
        }
    }
    if ($unsafe -or ($processes | Where-Object {
                ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($resolved + '\', [StringComparison]::OrdinalIgnoreCase)) -or
                ($_.CommandLine -and $_.CommandLine.IndexOf($resolved, [StringComparison]::OrdinalIgnoreCase) -ge 0)
            })) { continue }
    $bytes = ($children | Measure-Object -Property Length -Sum).Sum
    $manifest += [PSCustomObject]@{ Path = $resolved; Bytes = $bytes; Directory = $true }
}
if ($IncludeTestArtifacts) {
    # Only test-named binaries at these exact generated roots. Do not match
    # production .o/.a files, generators, reference data or staged executables.
    foreach ($relative in @('build', 'synppc/obj', 'syn68k/obj', 'syn68k/obj/tests',
            'synppc/obj/tests', 'system/obj/tests', 'system/obj/native-finder/windows-x64')) {
        $directory = Join-Path $workspace $relative
        if (-not (Test-Path -LiteralPath $directory) -or -not (Test-CleanupPath $directory)) { continue }
        $testTree = $relative.EndsWith('/tests')
        foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse:$testTree) {
            $testProduct = $file.Name -match '(?:^|[-_])(?:test|probe)(?:[-_.].*)?\.(?:exe|o|obj|a|d)$'
            if ($testTree) { $testProduct = $file.Extension -in @('.exe', '.o', '.obj', '.a', '.d') }
            if ($relative -eq 'build' -and $file.Name -match '^(?:68040-|cooperative-context-|jit-|mmu040-|root-metadata-|trap-terminal-).*\.(?:o|obj)$') {
                $testProduct = $true
            }
            if ($relative -eq 'synppc/obj' -and $file.Name -match '^ppc-aot-test\.cache(?:\.[a-z-]+)*$') { $testProduct = $true }
            if ($file.LastWriteTime -ge $cutoff -or $tracked.Contains($file.FullName) -or
                -not $testProduct -or
                -not (Test-CleanupPath $file.FullName) -or
                ($processes | Where-Object { $_.ExecutablePath -eq $file.FullName })) { continue }
            $manifest += [PSCustomObject]@{ Path = $file.FullName; Bytes = $file.Length; Directory = $false }
        }
    }
}
if ($Apply) {
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'cleanup-manifest.json')
    foreach ($item in $manifest) {
        if (-not (Test-CleanupPath $item.Path)) { throw "Cleanup path changed: $($item.Path)" }
        if ($item.Directory) { Remove-Item -LiteralPath $item.Path -Recurse -Force }
        else { Remove-Item -LiteralPath $item.Path -Force }
    }
}
$total = ($manifest | Measure-Object -Property Bytes -Sum).Sum
Write-Output ("{0} generated test paths, {1:N0} bytes; applied={2}" -f $manifest.Count, $total, $Apply)
