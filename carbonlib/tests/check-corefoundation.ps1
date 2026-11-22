# +===================================================================
#   File:      check-corefoundation.ps1
#
#   Summary:   Run grouped CarbonLib regressions and report results by subsystem and PPC engine.
#
#   Classes:   None.
#
#   Functions: Script entry point; Read-ProbeTail, Test-GroupTrace, Invoke-ProbeEngine.
#
#   Origin:    Original consolidated runner for the generated CarbonLib cohort fixtures.
#
#   Copyright and Legal notices: See COPYING and synppc/COPYING.
# ===================================================================+
param(
    [string]$ROMAW,
    [ValidateSet('Values', 'Collections', 'Frameworks', 'ClassicAccess', 'Fallback', 'Toolbox', 'Textension', 'ATSUI', 'Printing')]
    [string[]]$Group = @('Values', 'Collections', 'Frameworks', 'ClassicAccess', 'Toolbox'),
    [ValidateSet('Both', 'Interpreter', 'Jit')]
    [string]$Engine = 'Jit',
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 120,
    [string]$ProfileDirectory,
    [switch]$ReferenceOnly,
    [switch]$ToolboxMath,
    [switch]$ToolboxATSUI,
    [switch]$Detailed,
    [Alias('RequireDraftServices')][switch]$RequireServiceEntries,
    [switch]$Full,
    [switch]$Plan
)
$ErrorActionPreference = 'Stop'
if ($ReferenceOnly -and $RequireServiceEntries) { throw 'Service verification requires a native candidate' }
$workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$groupNames = @('Values', 'Collections', 'Frameworks', 'ClassicAccess', 'Fallback', 'Toolbox', 'Textension', 'ATSUI', 'Printing')
if ($Full -and -not $PSBoundParameters.ContainsKey('Group')) { $Group += 'Fallback' }
if ($ToolboxATSUI) {
    $Group = @($Group | ForEach-Object { if ($_ -eq 'Toolbox') { 'ATSUI' } else { $_ } })
}
$selectedGroups = @($Group | ForEach-Object { $groupNames -eq $_ } | Select-Object -Unique)
if ($ReferenceOnly -and 'ATSUI' -in $selectedGroups) { throw 'ATSUI checks require native and saved implementations' }
if ('Textension' -in $selectedGroups -and ($ReferenceOnly -or $ToolboxMath -or 'Toolbox' -in $selectedGroups)) {
    throw 'Textension is a focused native Toolbox group and requires its saved oracle'
}
$realGroups = @($selectedGroups | Where-Object { $_ -ne 'Fallback' })
$applications = @()
if ($realGroups.Count) {
    if (('Toolbox' -in $realGroups -or 'Textension' -in $realGroups) -and 'ATSUI' -in $realGroups) {
        # The original FontManager changes the Toolbox menu drawing path at process startup.
        $realGroups = @($realGroups | Where-Object { $_ -ne 'ATSUI' })
        $applications += @{ Generator = 'make_ppc_carbon_regression.py'; Arguments = @('ATSUI'); Groups = @('ATSUI'); Prefix = 'atsui' }
    }
    $generatorArguments = @($realGroups)
    if ($ReferenceOnly) { $generatorArguments += '--original-only' }
    if ($ToolboxMath) { $generatorArguments += '--math' }
    $applications += @{ Generator = 'make_ppc_carbon_regression.py'; Arguments = $generatorArguments; Groups = $realGroups; Prefix = 'real' }
}
if ('Fallback' -in $selectedGroups) {
    $applications += @{ Generator = 'make_ppc_carbon_cf_values_probe.py'; Arguments = @('--grouped-fallback'); Groups = @('Fallback'); Prefix = 'fallback' }
}
$modes = switch ($Engine) {
    'Both' { @('-no-ppc-jit', '-ppc-jit') }
    'Interpreter' { @('-no-ppc-jit') }
    'Jit' { @('-ppc-jit') }
}
if ($Plan) {
    foreach ($application in $applications) {
        Write-Output ("Carbon groups: " + ($application.Groups -join ', '))
    }
    Write-Output ("Reporting apps: $($applications.Count). ROMAW launches: $($applications.Count * @($modes).Count) ($($modes -join ', ')).")
    return
}
if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
$romawHash = (Get-FileHash -LiteralPath $ROMAW -Algorithm SHA256).Hash
if ($ProfileDirectory) { $ProfileDirectory = (Resolve-Path -LiteralPath $ProfileDirectory).Path }
$tag = [guid]::NewGuid().ToString('N').Substring(0, 8)
$reportDirectory = Join-Path $workspace ('build/cf-r.' + $tag)
$null = New-Item -ItemType Directory -Path $reportDirectory
$groupPattern = '(?m)^debugstr: (' + ($groupNames -join '|') + '): (running|passed)\r?$'

function Read-ProbeTail([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    try { return (Get-Content -LiteralPath $Path -Tail 16 -ErrorAction Stop) -join "`n" }
    catch [IO.IOException] { return '' }
}

function Test-GroupTrace([object]$Coverage, [string]$Trace, [string]$DiagnosticPath, [string]$Mode) {
    if (-not $ReferenceOnly) {
        $library = if ($Coverage.group -in @('ClassicAccess', 'Toolbox', 'Textension', 'ATSUI', 'Printing')) { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in $Coverage.exercised_entries) {
            if (-not $Trace.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "$($Coverage.group) missed native entry ${symbol}"
            }
        }
    }
    if ($Coverage.diagnostic_lines) {
        $lines = [IO.File]::ReadAllLines($DiagnosticPath)
        foreach ($expected in $Coverage.diagnostic_lines) {
            if (@($lines | Where-Object { $_ -ceq $expected }).Count -ne 2) {
                throw "Expected one diagnostic line per implementation in $Mode [$expected]"
            }
        }
    }
}

function Invoke-ProbeEngine([string]$Fixture, [object]$Coverage, [string]$Mode) {
    $logPath = Join-Path $Fixture 'stderr.txt'
    $diagnosticPath = Join-Path $Fixture 'CoreFoundationLib Log'
    [IO.File]::WriteAllText($logPath, '')
    $app = Join-Path $Fixture $Coverage.application
    $process = $null
    try {
        $profile = $env:USERPROFILE
        try {
            if ($ProfileDirectory) { $env:USERPROFILE = $ProfileDirectory }
            $process = Start-Process -FilePath $ROMAW -ArgumentList ($Mode + ' -system 9.2 -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $app + '"') `
                -WorkingDirectory $Fixture -WindowStyle Hidden -PassThru
        }
        finally { $env:USERPROFILE = $profile }
        $null = $process.Handle
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while (-not $process.WaitForExit(250)) {
            $tail = Read-ProbeTail $logPath
            # Fixtures deliberately execute opcode zero after a failed assertion.
            # Such a failure does not establish a missing PPC instruction.
            if ($tail -match 'PowerPC launch failed|Illegal instruction|win_exception') {
                throw "Assertion/runtime failure: $Fixture`n$tail"
            }
            if ([DateTime]::UtcNow -ge $deadline) { throw "Probe timed out: $Fixture" }
        }
        if ($process.ExitCode -ne 0) { throw "Regression app exited with $($process.ExitCode): $Fixture`n$(Read-ProbeTail $logPath)" }
        $log = [IO.File]::ReadAllText($logPath)
        $needsCoreFoundation = @($Coverage.groups | Where-Object { $_ -and -not $_.math_reference }).Count -gt 0
        if ($log -notmatch 'PowerPC fragment completed' -or ($needsCoreFoundation -and $log -notmatch 'PPC initializing CoreFoundationLib') -or
            $log -match 'PowerPC launch failed|Illegal instruction|win_exception') {
            throw "Probe initialization/completion failed: $Fixture`n$(Read-ProbeTail $logPath)"
        }
        if ($Mode -eq '-no-ppc-jit' -and $log -match 'PPC JIT executed') {
            throw 'Interpreter probe executed JIT blocks'
        }
        if ($Mode -eq '-ppc-jit' -and $log -notmatch 'PPC JIT executed [1-9][0-9]* instructions') {
            throw 'JIT probe did not execute JIT blocks'
        }
        if ($Coverage.synthetic_fallback) {
            if (-not $ReferenceOnly) {
                foreach ($namespace in $Coverage.entries_by_library.PSObject.Properties) {
                    foreach ($symbol in $namespace.Value) {
                        if (-not $log.Contains('PPC service ' + $namespace.Name + ':' + $symbol + ' ')) {
                            throw "Fallback missed $($namespace.Name):$symbol"
                        }
                    }
                }
            }
            return
        }
        $reports = [regex]::Matches($log, $groupPattern)
        $summary = "$($Coverage.application): $($Coverage.groups.Count) checks passed"
        if ($reports.Count -ne 2 * $Coverage.groups.Count -or -not $log.Contains($summary)) {
            throw "Incomplete group report: $Fixture"
        }
        for ($index = 0; $index -lt $Coverage.groups.Count; ++$index) {
            $groupCoverage = $Coverage.groups[$index]
            $start = $reports[2 * $index]
            $finish = $reports[2 * $index + 1]
            if ($start.Groups[1].Value -cne $groupCoverage.group -or $start.Groups[2].Value -cne 'running' -or
                $finish.Groups[1].Value -cne $groupCoverage.group -or $finish.Groups[2].Value -cne 'passed') {
                throw "Incomplete report for $($groupCoverage.group): $Fixture"
            }
            Test-GroupTrace $groupCoverage ($log.Substring($start.Index, $finish.Index - $start.Index)) $diagnosticPath $Mode
        }
    }
    finally {
        if ($process) {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id
                $null = $process.WaitForExit(5000)
            }
            $process.Dispose()
        }
        if (Test-Path -LiteralPath $logPath) {
            Copy-Item -LiteralPath $logPath -Destination (Join-Path $Fixture ($Mode.TrimStart('-') + '.txt'))
        }
        if (Test-Path -LiteralPath $diagnosticPath) {
            Move-Item -LiteralPath $diagnosticPath -Destination (Join-Path $Fixture ($Mode.TrimStart('-') + '-cf.log'))
        }
    }
}

$results = @()
$failed = $false
$launchAttempts = 0
foreach ($configuration in $applications) {
    $fixture = Join-Path $reportDirectory ($configuration.Prefix + '.' + $tag)
    $applicationResults = @(foreach ($name in $configuration.Groups) {
            [ordered]@{ group = $name; fixture = $fixture; passed = $false; scenarios = @() }
        })
    try {
        $generator = Join-Path $workspace ('synppc/tests/' + $configuration.Generator)
        $generatorArguments = @($configuration.Arguments)
        $generation = & python $generator $fixture @generatorArguments
        if ($Detailed -or $LASTEXITCODE -ne 0) { $generation | Write-Output }
        if ($LASTEXITCODE -ne 0) { throw "Could not generate $($configuration.Groups -join ', ') application" }
        $coverage = Get-Content (Join-Path $fixture 'coverage.json') -Raw | ConvertFrom-Json
        if ($coverage.synthetic_fallback) {
            $coverage | Add-Member -NotePropertyName group -NotePropertyValue 'Fallback'
            $groupCoverage = @($coverage)
        }
        else { $groupCoverage = @($coverage.groups) }
        if (($groupCoverage.group -join ',') -cne ($configuration.Groups -join ',')) { throw 'Generated group list differs from request' }
        for ($index = 0; $index -lt $applicationResults.Count; ++$index) {
            $result = $applicationResults[$index]
            $groupReport = $groupCoverage[$index]
            if ($RequireServiceEntries -and $result.group -eq 'Frameworks' -and $groupReport.service_entries.Count -ne 100) {
                throw 'Frameworks must exercise all 100 service entries'
            }
            if ($groupReport.pending_entries.Count) { throw "$($result.group) has entries without scenarios" }
            $sectionChecks = ($groupReport.sections | Measure-Object -Property checks -Sum).Sum
            if ($sectionChecks -ne $groupReport.assertions) { throw "$($result.group) section counts do not match assertion count" }
            $result.application = $coverage.application
            $result.assertions_per_engine = $groupReport.assertions
            $result.native_entries = if ($ReferenceOnly) { 0 } else { $groupReport.exercised_entries.Count }
            $result.sections = $groupReport.sections
            if ($groupReport.service_entries) { $result.service_entries = $groupReport.service_entries }
        }
        foreach ($mode in $modes) {
            $scenario = [ordered]@{ mode = $mode; passed = $false }
            try {
                ++$launchAttempts
                Invoke-ProbeEngine $fixture $coverage $mode
                $scenario.passed = $true
                $checks = ($applicationResults | ForEach-Object { $_.assertions_per_engine } | Measure-Object -Sum).Sum
                $groups = $configuration.Groups -join ', '
                Write-Output ("$($coverage.application) ${mode}: $checks assertions passed ($groups)")
            }
            catch {
                $scenario.error = $_.Exception.Message
                $logPath = Join-Path $fixture 'stderr.txt'
                if (-not $coverage.synthetic_fallback -and (Test-Path -LiteralPath $logPath)) {
                    $failureLog = [IO.File]::ReadAllText($logPath)
                    $reports = [regex]::Matches($failureLog, $groupPattern)
                    $scenario.completed_groups = @(foreach ($entry in $reports) {
                            if ($entry.Groups[2].Value -eq 'passed') { $entry.Groups[1].Value }
                        })
                    if ($reports.Count -and $reports[$reports.Count - 1].Groups[2].Value -eq 'running') {
                        $scenario.active_group = $reports[$reports.Count - 1].Groups[1].Value
                        $fault = [regex]::Match($failureLog, '(?m)^trace: PPC fault .*\br0=4341([0-9a-f]{4})\b')
                        if ($fault.Success) {
                            $index = [Convert]::ToInt32($fault.Groups[1].Value, 16) - 1
                            $checksPath = Join-Path $fixture ($scenario.active_group + '-checks.json')
                            if (Test-Path -LiteralPath $checksPath) {
                                $assertions = @(Get-Content -LiteralPath $checksPath -Raw | ConvertFrom-Json)
                                if ($index -ge 0 -and $index -lt $assertions.Count) {
                                    $scenario.failed_assertion = $assertions[$index].check
                                    $scenario.error = "$($scenario.active_group) check $($index + 1) failed: $($scenario.failed_assertion)`n$($scenario.error)"
                                }
                            }
                        }
                    }
                }
                Write-Output ("$($coverage.application) ${mode}: FAILED; $($scenario.error)")
                $failed = $true
            }
            foreach ($result in $applicationResults) { $result.scenarios += $scenario }
        }
        foreach ($result in $applicationResults) {
            $result.passed = @($result.scenarios | Where-Object { -not $_.passed }).Count -eq 0
            if ($Detailed) {
                foreach ($section in $result.sections) {
                    Write-Output ("  $($section.section): $($section.checks) assertions per engine")
                }
            }
        }
    }
    catch {
        foreach ($result in $applicationResults) { $result.error = $_.Exception.Message }
        Write-Output ("$($configuration.Groups -join ', '): FAILED; $($_.Exception.Message)")
        $failed = $true
    }
    $results += $applicationResults
}
$finalHash = (Get-FileHash -LiteralPath $ROMAW -Algorithm SHA256).Hash
if ($finalHash -ne $romawHash) {
    $failed = $true
    Write-Output 'FAILED: ROMAW changed during regression runs'
}
$assertionsPassed = 0
foreach ($result in $results) {
    $successfulModes = @($result.scenarios | Where-Object { $_.passed }).Count
    $assertionsPassed += $result.assertions_per_engine * $successfulModes
}
$report = [ordered]@{
    romaw = $ROMAW
    romaw_sha256 = $romawHash
    final_romaw_sha256 = $finalHash
    reference_only = [bool]$ReferenceOnly
    require_service_entries = [bool]$RequireServiceEntries
    applications = $applications.Count
    launch_attempts = $launchAttempts
    passed = -not $failed
    assertions_passed = $assertionsPassed
    groups = $results
}
$reportPath = Join-Path $reportDirectory 'validation.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output ("Apps $($applications.Count), launch attempts $launchAttempts, groups $($results.Count), assertions passed $assertionsPassed. Report: $reportPath")
if ($failed) { throw "CoreFoundation regression checks failed; see $reportPath" }
