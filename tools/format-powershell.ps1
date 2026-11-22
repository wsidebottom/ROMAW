# +===================================================================
#   File:      format-powershell.ps1
#
#   Summary:   Implements format powershell support for ROMAW build and development support.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in tools; original attribution and provenance comments are preserved below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
# ===================================================================+

param([switch]$Check)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sourceRoots = (Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'source-layout.json') | ConvertFrom-Json).source_roots
Import-Module PSScriptAnalyzer -RequiredVersion 1.23.0
$settings = @{
    IncludeRules = @('PSPlaceOpenBrace', 'PSPlaceCloseBrace', 'PSUseConsistentWhitespace', 'PSUseConsistentIndentation')
    Rules = @{
        PSPlaceOpenBrace = @{ Enable = $true; OnSameLine = $true; NewLineAfter = $true; IgnoreOneLineBlock = $true }
        PSPlaceCloseBrace = @{ Enable = $true; NewLineAfter = $true; IgnoreOneLineBlock = $true }
        PSUseConsistentWhitespace = @{ Enable = $true; CheckOpenBrace = $true; CheckInnerBrace = $true; CheckOperator = $true; CheckSeparator = $true }
        PSUseConsistentIndentation = @{ Enable = $true; Kind = 'space'; IndentationSize = 4; PipelineIndentation = 'IncreaseIndentationForFirstPipeline' }
    }
}
Push-Location $root
try {
    $files = & git ls-files --cached --others --exclude-standard -- '*.ps1' '*.psm1'
    if ($LASTEXITCODE) { throw 'Cannot enumerate PowerShell sources' }
    $failed = $false
    $files = @($files | Sort-Object -Unique | Where-Object {
            ($_ -split '/')[0] -in $sourceRoots -and
            $_ -notmatch '(^|/)(third_party|build|obj)/|(^|/)%' -and
            (Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf)
        })
    if ($files.Count) {
        $headerArguments = @((Join-Path $PSScriptRoot 'file_headers.py'))
        if ($Check) { $headerArguments += '--check' }
        $headerArguments += $files
        & python @headerArguments
        if ($LASTEXITCODE) { $failed = $true }
    }
    foreach ($file in $files | Sort-Object -Unique) {
        if (($file -split '/')[0] -notin $sourceRoots) { continue }
        if ($file -match '(^|/)(third_party|build|obj)/|(^|/)%') { continue }
        $path = Join-Path $root $file
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $original = [IO.File]::ReadAllText($path)
        $formatted = (Invoke-Formatter -ScriptDefinition $original -Settings $settings).Replace("`r`n", "`n").TrimEnd("`r", "`n") + "`n"
        if ($original -cne $formatted) {
            if ($Check) { Write-Output $file; $failed = $true }
            else {
                $temporary = $path + '.format-tmp'
                [IO.File]::WriteAllText($temporary, $formatted, [Text.UTF8Encoding]::new($false))
                [IO.File]::Move($temporary, $path, $true)
            }
        }
    }
    if ($failed) { exit 1 }
}
finally {
    Pop-Location
}
