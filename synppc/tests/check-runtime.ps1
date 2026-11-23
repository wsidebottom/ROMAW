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
#   Copyright and Legal notices: Applicable repository license: synppc/COPYING.
# ===================================================================+

function Invoke-Check-system750 {
    param(
        [string]$ROMAW,
        [ValidateSet('Jit', 'Interpreter', 'Aot')]
        [string]$Engine = 'Jit',
        [switch]$Full,
        [switch]$Detailed
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/system750-batch.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
    [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
    $generator = Join-Path $PSScriptRoot 'make_ppc_system_batch.py'
    & python $generator $fixture
    if ($LASTEXITCODE) { throw 'Cannot generate 750 batch' }
    $expected = @('PPC 750 System Probe', 'PPC 750 FP Probe', 'PPC 750 Break Probe', 'PPC 750 Watch Probe', 'PPC 750 Program Probe', 'PPC 750 Cache Probe', 'PPC 750 Reserve Probe', 'PPC 750 Thermal Probe', 'PPC 750 Performance Probe', 'PPC Toolbox Probe', 'PPC Cached Toolbox Probe', 'PPC L2 Cached Toolbox Probe')
    $engineOptions = @{
        Jit = '-ppc-jit'
        Interpreter = '-no-ppc-jit'
        Aot = '-ppc-aot'
    }
    $modes = if ($Full) { $engineOptions.Interpreter, $engineOptions.Jit, $engineOptions.Aot }
    else { $engineOptions[$Engine] }
    $savedProfile = $env:USERPROFILE
    try {
        $env:USERPROFILE = $fixture
        foreach ($mode in $modes) {
            $arguments = '-ppc-system ' + $mode + ' -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + (Join-Path $fixture 'PPC 750 Batch Probe') + '"'
            $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            try {
                if (-not $process.WaitForExit(15000)) { throw "750 batch timed out: $fixture" }
                if ($process.ExitCode) { throw "750 batch exited $($process.ExitCode): $fixture" }
                $log = Get-Content (Join-Path $fixture 'stderr.txt') -Raw
                [IO.File]::WriteAllText((Join-Path $fixture ($mode + '.log')), $log)
                $reports = [regex]::Matches($log, '(?m)^debugstr: (PPC [^\r\n]+): passed\r?$')
                $reported = @($reports | ForEach-Object { $_.Groups[1].Value })
                if ($reports.Count -ne 12 -or (Compare-Object $expected $reported) -or $log -notmatch 'debugstr: PPC 750 batch: 12 checks passed' -or
                    $log -notmatch 'PPC 750 architectural core enabled' -or $log -notmatch 'PowerPC fragment completed' -or
                    $log -notmatch '(?m)^debugstr: 750 translated Pascal string\r?$' -or $log -match 'FAILED|win_exception' -or
                    ($mode -eq '-no-ppc-jit' -and $log -match 'PPC JIT executed') -or
                    ($mode -ne '-no-ppc-jit' -and $log -notmatch 'PPC JIT executed [1-9][0-9]* instructions in [1-9][0-9]* blocks')) {
                    throw "750 batch report incomplete: $fixture"
                }
                if ($mode -eq '-ppc-aot') {
                    $published = @([regex]::Matches($log, '(?m)^trace: PPC AOT prepared (\d+) published instructions\r?$') |
                            ForEach-Object { [int]$_.Groups[1].Value })
                    if ($published.Count -ne 4 -or $published[0] -lt 5 -or $published[1] -lt 1 -or
                        $published[2] -lt 5 -or $published[3] -lt 1) {
                        throw "750 AOT skipped guest code publication: $fixture"
                    }
                }
                if ($Detailed) {
                    foreach ($report in $reports) { Write-Output ($report.Groups[1].Value + ' ' + $mode + ': passed') }
                }
                Write-Output "PPC 750 Batch Probe ${mode}: $($reports.Count) checks passed. Logs in $fixture"
            }
            finally {
                if (-not $process.HasExited) { Stop-Process -Id $process.Id }
                $process.Dispose()
            }
        }
        if (-not $Full) { return }
        # The reporting app must identify and fail an injected bad check.
        $negative = Join-Path $fixture 'negative'
        & python $generator $negative --fail-case 'PPC 750 Cache Probe'
        if ($LASTEXITCODE) { throw 'Cannot generate failed-check fixture' }
        $arguments = '-ppc-system -no-ppc-jit -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + (Join-Path $negative 'PPC 750 Batch Probe') + '"'
        $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $negative -WindowStyle Hidden -PassThru
        try {
            $timer = [Diagnostics.Stopwatch]::StartNew()
            while (-not $process.WaitForExit(100)) {
                $process.Refresh()
                if ($process.MainWindowTitle -eq 'ROMAW - PowerPC application') {
                    # Dismiss only this test process's expected startup-error dialog.
                    $null = $process.CloseMainWindow()
                }
                if ($timer.ElapsedMilliseconds -gt 15000) { throw 'Failed-check batch timed out' }
            }
            $log = Get-Content (Join-Path $negative 'stderr.txt') -Raw
            if ($process.ExitCode -ne 2 -or $log -notmatch 'debugstr: PPC 750 Cache Probe: FAILED' -or
                $log -match 'batch: 12 checks passed|PPC 750 Reserve Probe: passed|win_exception') {
                throw "Failed-check batch incorrectly accepted: $fixture"
            }
            Write-Output '750 batch identifies an injected failure and rejects the launch: passed'
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
    finally { $env:USERPROFILE = $savedProfile }
}

function Invoke-Check-system750-isolated {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/system750-launch.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
    [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
    foreach ($generator in @('make_ppc_system_probe.py', 'make_ppc_probe.py')) {
        $generatorArguments = @((Join-Path $PSScriptRoot $generator), $fixture)
        if ($generator -eq 'make_ppc_probe.py') { $generatorArguments += '--l2' }
        & python @generatorArguments
        if ($LASTEXITCODE) { throw 'Cannot generate 750 fixture' }
    }
    $savedProfile = $env:USERPROFILE
    try {
        $env:USERPROFILE = $fixture
        foreach ($engine in @('-no-ppc-jit', '-ppc-jit', '-ppc-aot')) {
            foreach ($app in @('PPC 750 System Probe', 'PPC 750 FP Probe', 'PPC 750 Break Probe', 'PPC 750 Watch Probe', 'PPC 750 Program Probe', 'PPC 750 Cache Probe', 'PPC 750 Reserve Probe', 'PPC 750 Thermal Probe', 'PPC 750 Performance Probe', 'PPC Toolbox Probe', 'PPC Cached Toolbox Probe', 'PPC L2 Cached Toolbox Probe')) {
                $arguments = '-ppc-system ' + $engine + ' -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + (Join-Path $fixture $app) + '"'
                $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
                try {
                    if (-not $process.WaitForExit(15000)) { throw "750 launch timed out: $fixture" }
                    if ($process.ExitCode) { throw "750 launch exited $($process.ExitCode): $fixture" }
                    $log = Get-Content (Join-Path $fixture 'stderr.txt') -Raw
                    [IO.File]::WriteAllText((Join-Path $fixture ($app + $engine + '.log')), $log)
                    if ($log -notmatch 'PPC 750 architectural core enabled' -or
                        $log -notmatch 'PowerPC fragment completed' -or $log -match 'win_exception' -or ($engine -eq '-no-ppc-jit' -and $log -match 'PPC JIT executed')) {
                        throw "750 launch did not complete through the checked CPU path: $fixture"
                    }
                    Write-Output ($app + ' ' + $engine + ': passed')
                }
                finally {
                    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
                    $process.Dispose()
                }
            }
        }
    }
    finally { $env:USERPROFILE = $savedProfile }
}

function Invoke-Check-aot {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/aot-launch.' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
    [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
    & python (Join-Path $PSScriptRoot 'make_ppc_aot_probe.py') $fixture
    if ($LASTEXITCODE) { throw 'Cannot generate AOT fixture' }
    $savedProfile = $env:USERPROFILE
    $entriesByZone = @{}
    try {
        $env:USERPROFILE = $fixture
        foreach ($run in @(
                @{ Zone = '8m'; Loaded = 0 },
                @{ Zone = '10m'; Loaded = 0 },
                @{ Zone = '8m'; Loaded = 4096 },
                @{ Zone = '10m'; Loaded = 4096 }
            )) {
            $name = "$($run.Zone)-loaded-$($run.Loaded)"
            $options = "-ppc-aot -syszone $($run.Zone) -applzone 64m -sdlaudio test -debug trace "
            $process = Start-Process -FilePath $ROMAW -ArgumentList ($options + '"' + (Join-Path $fixture 'PPC AOT Probe') + '"') -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            try {
                if (-not $process.WaitForExit(15000)) { throw 'AOT launch timed out' }
                if ($process.ExitCode) { throw "AOT launch exited $($process.ExitCode)" }
                $log = Get-Content (Join-Path $fixture 'stderr.txt') -Raw
                [IO.File]::WriteAllText((Join-Path $fixture "$name.log"), $log)
                $prepared = 4096 - $run.Loaded
                if ($log -notmatch 'PowerPC fragment completed' -or
                    $log -notmatch "PPC AOT loaded $($run.Loaded) cached blocks" -or
                    $log -notmatch "PPC AOT prepared $prepared blocks in 2 linked images" -or
                    $log -notmatch 'PPC initializing ProbeAOTLibrary' -or
                    $log -notmatch 'PPC JIT executed 4 instructions in ([1-9][0-9]*) blocks \(0 compilations, \1 cached\)' -or
                    $log -notmatch 'PPC JIT executed 129 instructions in ([1-9][0-9]*) blocks \(0 compilations, \1 cached\)' -or
                    $log -notmatch 'PPC AOT saved cache: 1') {
                    throw "AOT application and library did not both use prepared native code; see $fixture\$name.log"
                }

                $entries = @([regex]::Matches($log, '(?m)^trace: PPC call pc=([0-9a-f]+) ') |
                        ForEach-Object { $_.Groups[1].Value })
                if ($entries.Count -ne 2 -or ($run.Loaded -and
                        ($entries -join ',') -cne ($entriesByZone[$run.Zone] -join ','))) {
                    throw "Unexpected AOT entry addresses; see $fixture\$name.log"
                }
                $entriesByZone[$run.Zone] = $entries
            }
            finally {
                if (-not $process.HasExited) { Stop-Process -Id $process.Id }
                $process.Dispose()
            }
        }
        if ($entriesByZone['8m'][0] -eq $entriesByZone['10m'][0] -or
            $entriesByZone['8m'][1] -eq $entriesByZone['10m'][1]) {
            throw "The changed system zone did not relocate both AOT fragments; see $fixture"
        }
    }
    finally { $env:USERPROFILE = $savedProfile }
    Write-Output 'Cold AOT and disk-cache reuse passed at both linked layouts without runtime compilation'
}

function Invoke-Check-carbon-bundle-copy {
    # Compare successful private-CFM-copy loading through native and original bundle entries.
    param([string]$ROMAW, [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe')
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $Gdb = (Resolve-Path -LiteralPath $Gdb).Path
    foreach ($engine in @('-no-ppc-jit', '-ppc-jit')) {
        foreach ($original in @($false, $true)) {
            $fixture = Join-Path $workspace ('build\carbon-bundle-copy.' + [guid]::NewGuid().ToString('N'))
            $generatorArgs = @($fixture, '--real', '--private-copy')
            if ($original) { $generatorArgs += '--original-load' }
            & python (Join-Path $PSScriptRoot 'make_ppc_carbon_bundle_state_probe.py') @generatorArgs
            if ($LASTEXITCODE -ne 0) { throw 'Could not generate real private-copy bundle probe' }
            $app = (Join-Path $fixture 'PPC Carbon BState Real Probe').Replace('\', '/')
            $script = Join-Path $fixture 'copy.gdb'
            $commands = @'
set pagination off
set confirm off
set $native_cfm=0
break bundle_load_cfm
commands
silent
set $native_cfm=$native_cfm+1
continue
end
break ROMlib_win_ppc_error
commands
silent
printf "BUNDLE_COPY_ERROR %s\n",message
quit 1
end
'@ + "`nrun $engine -system 9.2.2 -syszone 16m -applzone 64m -sdlaudio test -debug trace `"$app`"`nprintf `"BUNDLE_COPY_COMPLETED native_cfm=%d\n`",`$native_cfm`n"
            [IO.File]::WriteAllText($script, $commands, [Text.UTF8Encoding]::new($false))
            $stdout = Join-Path $fixture 'gdb.txt'
            $process = Start-Process -FilePath $Gdb -ArgumentList ('-batch -x "' + $script + '" "' + $ROMAW + '"') `
                -WorkingDirectory $fixture -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $fixture 'gdb-error.txt')
            $null = $process.Handle
            try {
                if (-not $process.WaitForExit(30000)) { throw "Private-copy comparison timed out: $fixture" }
                $output = [IO.File]::ReadAllText($stdout)
                $log = [IO.File]::ReadAllText((Join-Path $fixture 'stderr.txt'))
                $expectedCount = if ($original) { 0 } else { 1 }
                if ($process.ExitCode -ne 0 -or
                    $output -notmatch ('BUNDLE_COPY_COMPLETED native_cfm=' + $expectedCount + '(?:\r?\n|$)') -or
                    $output -notmatch 'exited normally' -or
                    $log -notmatch 'PPC initializing CoreFoundationLib' -or
                    $log -notmatch 'PPC service InterfaceLib:GetDiskFragment ' -or
                    $log -notmatch 'PowerPC fragment completed' -or
                    $log -match 'PowerPC launch failed|win_exception|Illegal instruction') {
                    throw "Native/original private-copy behavior differed: $fixture"
                }
                $entry = if ($original) { 'original PPC' } else { 'native C' }
                Write-Output "$engine $($entry): private CFM executable loaded and completed"
            }
            finally {
                if (-not $process.HasExited) { Stop-Process -Id $process.Id }
                $process.Dispose()
            }
        }
    }
}

function Invoke-Check-carbon-cf-real {
    # Validate native accessors against the user's installed CarbonLib 1.4 objects.
    # This optional suite requires the same local library assets used by MyABCD.
    param(
        [string]$ROMAW,
        [string[]]$Group = @('Objects'),
        [string]$ProfileDirectory,
        [ValidateSet('Both', 'Interpreter', 'Jit')]
        [string]$Engine = 'Jit',
        [switch]$Detailed
    )
    $ErrorActionPreference = 'Stop'
    function Read-ProbeTail([string]$Path) {
        try {
            $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            try {
                $size = [int][Math]::Min([long]4096, $stream.Length)
                $null = $stream.Seek(-$size, [IO.SeekOrigin]::End)
                $bytes = [byte[]]::new($size)
                $count = $stream.Read($bytes, 0, $size)
                return [Text.Encoding]::UTF8.GetString($bytes, 0, $count)
            }
            finally { $stream.Dispose() }
        }
        catch [IO.IOException] { return '' }
    }
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    if ($ProfileDirectory) { $ProfileDirectory = (Resolve-Path -LiteralPath $ProfileDirectory).Path }
    $groups = [ordered]@{
        Objects = @('CF', 'carbon_cf')
        Dates = @('Date', 'carbon_date')
        Search = @('Search', 'carbon_search')
        Allocator = @('Allocator', 'carbon_allocator')
        Allocation = @('Alloc', 'carbon_allocation')
        Creation = @('Create', 'carbon_creation')
        ArrayCreation = @('Array New', 'carbon_array_create')
        CFStrings = @('CF String', 'carbon_cf_string')
        Bundle = @('Bundle', 'carbon_bundle')
        CFPascal = @('CF Pascal', 'carbon_cf_pascal')
        StringPointers = @('Str Ptr', 'carbon_string_pointer')
        CFCString = @('CF CStr', 'carbon_cf_cstring')
        Encoding = @('CF Encode', 'carbon_cf_encoding')
        EncodingQueries = @('CF Query', 'carbon_cf_queries')
        BundleState = @('BState', 'carbon_bundle_state')
        BundleLifecycle = @('BLife', 'carbon_bundle_lifecycle')
        URLs = @('URL', 'carbon_url')
        URLComponents = @('URLComp', 'carbon_url_components')
        URLCreation = @('URLCreate', 'carbon_url_creation')
        URLResolution = @('URLRes', 'carbon_url_resolution')
        StringOps = @('StrOps', 'carbon_cfstrings')
        ClassicAccess = @('Classic', 'carbon_classic_access')
        Values = @('Values', 'carbon_cf_values')
        Collections = @('Coll', 'carbon_cf_collections')
        Ownership = @('Own', 'carbon_ownership')
        UniChar = @('UniChar', 'unichar')
    }
    $selected = if ($Group.Count -eq 1 -and $Group[0] -eq 'All') { @($groups.Keys) } else { @($Group | Select-Object -Unique) }
    if (-not $selected) { throw 'Choose CoreFoundation groups or All' }
    foreach ($name in $selected) {
        if (-not $groups.Contains($name)) { throw "Unknown CoreFoundation group '$name'. Choose $($groups.Keys -join ', ') or All" }
    }
    $manifestRows = @(foreach ($groupName in $selected) {
            $kind, $generator = $groups[$groupName]
            $symbols = if ($groupName -eq 'Collections') {
                @((Get-Content (Join-Path $workspace 'carbonlib/plans/cf-collections-batch.json') -Raw | ConvertFrom-Json).entries.name)
            }
            elseif ($groupName -eq 'Values') {
                @((Get-Content (Join-Path $workspace 'carbonlib/plans/cf-values-batch.json') -Raw | ConvertFrom-Json).entries.name)
            }
            elseif ($groupName -eq 'ClassicAccess') {
                @((Get-Content (Join-Path $workspace 'carbonlib/tests/classic_access_cases.json') -Raw | ConvertFrom-Json).name)
            }
            elseif ($groupName -eq 'StringOps') {
                @('CFStringCompare', 'CFStringCompareWithOptions', '__CFStringMakeConstantString', 'CFStringCreateCopy',
                    'CFStringHasPrefix', 'CFStringHasSuffix', 'CFStringFind', 'CFStringGetIntValue')
            }
            elseif ($groupName -eq 'URLResolution') {
                @('CFURLCopyAbsoluteURL', 'CFURLCopyFileSystemPath', 'CFURLGetFileSystemRepresentation', 'CFURLCreateWithString')
            }
            elseif ($groupName -eq 'URLCreation') {
                @('_CFURLAlloc', '_CFURLInitWithString', 'CFURLCreateWithString', 'CFURLCreateWithBytes', 'CFURLCreateData',
                    'CFURLCreateWithFileSystemPath', 'CFURLCreateWithFileSystemPathRelativeToBase',
                    'CFURLCreateCopyDeletingLastPathComponent', 'CFURLCreateCopyAppendingPathExtension', 'CFURLCreateCopyDeletingPathExtension')
            }
            elseif ($groupName -eq 'URLComponents') {
                @('CFURLCopyHostName', 'CFURLCopyUserName', 'CFURLCopyPassword', 'CFURLCopyParameterString',
                    'CFURLCopyQueryString', 'CFURLCopyFragment', 'CFURLGetPortNumber', '__CFURLReservedPtr',
                    '__CFURLSetReservedPtr', 'CFURLCopyPathExtension', 'CFURLCreateFromFileSystemRepresentation',
                    'CFURLCreateFromFileSystemRepresentationRelativeToBase', 'CFURLCreateFromFSRef', 'CFURLGetFSRef')
            }
            elseif ($groupName -eq 'URLs') {
                @('CFURLGetTypeID', 'CFURLCanBeDecomposed', 'CFURLGetString', 'CFURLGetBaseURL', 'CFURLHasDirectoryPath',
                    'CFURLCopyScheme', 'CFURLCopyNetLocation', 'CFURLCopyPath', 'CFURLCopyStrictPath',
                    'CFURLCopyResourceSpecifier', 'CFURLCopyLastPathComponent', 'CFURLCreateCopyAppendingPathComponent')
            }
            elseif ($groupName -eq 'BundleLifecycle') {
                @('CFBundleCreate', 'CFBundleGetLocalInfoDictionary', 'CFBundleUnloadExecutable', 'CFBundleGetVersionNumber', 'CFBundleGetDevelopmentRegion', 'CFBundleGetFunctionPointersForNames', 'CFBundleGetDataPointersForNames')
            }
            elseif ($groupName -eq 'BundleState') {
                @('CFBundleGetTypeID', 'CFBundleCopyBundleURL', 'CFBundleGetInfoDictionary', 'CFBundleGetValueForInfoDictionaryKey', 'CFBundleGetIdentifier', 'CFBundleIsExecutableLoaded', 'CFBundleLoadExecutable', 'CFBundleGetPlugIn')
            }
            elseif ($groupName -eq 'EncodingQueries') {
                @('CFStringGetMaximumSizeForEncoding', 'CFStringConvertEncodingToNSStringEncoding', 'CFStringConvertNSStringEncodingToEncoding', 'CFStringConvertEncodingToWindowsCodepage', 'CFStringConvertWindowsCodepageToEncoding', 'CFStringIsEncodingAvailable', 'CFStringGetMostCompatibleMacStringEncoding')
            }
            elseif ($groupName -eq 'StringPointers') {
                @('CFStringGetCStringPtr', 'CFStringGetPascalStringPtr')
            }
            elseif ($groupName -eq 'CFPascal') {
                @('CFStringGetPascalString')
            }
            elseif ($groupName -eq 'CFCString') {
                @('CFStringGetCString')
            }
            elseif ($groupName -eq 'Encoding') {
                @('CFStringGetBytes', 'CFStringGetFastestEncoding')
            }
            elseif ($groupName -eq 'Bundle') {
                @('CFBundleGetFunctionPointerForName', 'CFBundleGetDataPointerForName')
            }
            elseif ($groupName -eq 'UniChar') {
                @('CFUniCharIsMemberOf')
            }
            elseif ($groupName -eq 'CFStrings') {
                @('CFStringGetCharacterAtIndex', 'CFStringGetCharacters')
            }
            elseif ($groupName -eq 'ArrayCreation') {
                @('CFArrayCreate', 'CFArrayGetTypeID')
            }
            elseif ($groupName -eq 'Ownership') {
                @('CFRetain', 'CFRelease', 'CFGetRetainCount', 'CFGetAllocator')
            }
            elseif ($groupName -eq 'Creation') {
                @('CFAllocatorCreate', 'CFAllocatorGetTypeID', 'CFAllocatorAllocate', 'CFAllocatorDeallocate')
            }
            elseif ($groupName -eq 'Allocation') {
                @('CFAllocatorAllocate', 'CFAllocatorReallocate', 'CFAllocatorDeallocate')
            }
            elseif ($groupName -eq 'Allocator') {
                @('CFAllocatorGetDefault', 'CFAllocatorGetContext', 'CFAllocatorGetPreferredSizeForSize', 'CFGetTypeID')
            }
            elseif ($groupName -eq 'Search') {
                @('CFArrayBSearchValues')
            }
            elseif ($groupName -eq 'Dates') {
                @('CFAbsoluteTimeGetCurrent', 'CFDateGetAbsoluteTime', 'CFDateGetTimeIntervalSinceDate', 'CFDateCompare')
            }
            else {
                @('CFArrayApplyFunction', 'CFArrayGetCount', 'CFArrayGetValueAtIndex', 'CFArrayGetValues', 'CFGetTypeID', 'CFDataGetLength', 'CFDataGetBytePtr',
                    'CFDataGetMutableBytePtr', 'CFDataGetBytes', 'CFStringGetLength', 'CFStringGetCharactersPtr')
            }
            $libraries = if ($groupName -eq 'UniChar') { @('CoreFoundationLib') } elseif ($groupName -eq 'ClassicAccess') { @('CarbonLib') } else { @('CarbonLib', 'CoreFoundationLib') }
            foreach ($library in $libraries) {
                $label = if ($library -eq 'CarbonLib') { 'Carbon' } else { 'Core' }
                $entries = $symbols
                if ($groupName -eq 'Collections' -and $library -eq 'CarbonLib') {
                    $entries = @((Get-Content (Join-Path $workspace 'carbonlib/plans/cf-collections-batch.json') -Raw |
                                ConvertFrom-Json).entries | Where-Object { -not $_.companion_only } | ForEach-Object { $_.name })
                }
                @{
                    Generator = 'make_ppc_' + $generator + '_probe.py'
                    App = "PPC $label $kind Real Probe"
                    Report = "$library $groupName"
                    Arguments = @('--real', '--library', $library)
                    Library = $library
                    Symbols = $entries
                }
            }
        })
    $fixture = Join-Path $workspace ('build/cf-real.' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $null = New-Item -ItemType Directory -Path $fixture
    $manifest = Join-Path $fixture 'batch.json'
    [IO.File]::WriteAllText($manifest, (ConvertTo-Json -InputObject $manifestRows -Depth 4))
    $application = 'PPC Carbon Regression'
    $generation = & python (Join-Path $PSScriptRoot 'make_ppc_romaw_batch.py') $fixture $application $manifest --real-carbon
    if ($Detailed -or $LASTEXITCODE -ne 0) { $generation }
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the CoreFoundation regression application' }

    $timeoutSeconds = 120
    # The ownership loops and exhaustive BMP checks emit a trace for every call.
    if ('Ownership' -in $selected) { $timeoutSeconds += 60 }
    if ('UniChar' -in $selected) { $timeoutSeconds += 240 }
    $app = Join-Path $fixture $application
    $logPath = Join-Path $fixture 'stderr.txt'
    $modes = switch ($Engine) {
        'Both' { @('-no-ppc-jit', '-ppc-jit') }
        'Interpreter' { '-no-ppc-jit' }
        'Jit' { '-ppc-jit' }
    }
    foreach ($mode in $modes) {
        [IO.File]::WriteAllText($logPath, '')
        $profile = $env:USERPROFILE
        try {
            if ($ProfileDirectory) { $env:USERPROFILE = $ProfileDirectory }
            $process = Start-Process -FilePath $ROMAW -ArgumentList ($mode + ' -system 9.2 -syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $app + '"') `
                -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        }
        finally { $env:USERPROFILE = $profile }
        $null = $process.Handle
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
            while (-not $process.WaitForExit(250)) {
                $tail = Read-ProbeTail $logPath
                if ($tail -match 'PowerPC launch failed|win_exception|Illegal instruction') {
                    throw "CoreFoundation regression failed in $mode. Inspect $logPath"
                }
                if ([DateTime]::UtcNow -ge $deadline) { throw "CoreFoundation regression timed out in $mode. Inspect $logPath" }
            }
            if ($process.ExitCode -ne 0) { throw "CoreFoundation regression exited with $($process.ExitCode) in $mode. Inspect $logPath" }
            $log = [IO.File]::ReadAllText($logPath)
            if ($log -notmatch 'PowerPC fragment completed' -or
                $log -notmatch 'PPC initializing CoreFoundationLib' -or
                $log -match 'PowerPC launch failed|win_exception|Illegal instruction' -or
                -not $log.Contains("${application}: $($manifestRows.Count) checks passed")) {
                throw "CoreFoundation regression did not complete in $mode. Inspect $logPath"
            }
            if ($mode -eq '-no-ppc-jit' -and $log -match 'PPC JIT executed') {
                throw 'The interpreter regression executed native blocks'
            }
            if ($mode -eq '-ppc-jit' -and $log -notmatch 'PPC JIT executed [1-9][0-9]* instructions') {
                throw 'The JIT regression did not execute native blocks'
            }
            $reports = [regex]::Matches($log, '(?m)^debugstr: ((?:CoreFoundationLib|CarbonLib) [^\r\n]+): (running|passed)\r?$')
            if ($reports.Count -ne 2 * $manifestRows.Count) { throw "Incomplete section report in $mode. Inspect $logPath" }
            for ($i = 0; $i -lt $manifestRows.Count; ++$i) {
                $case = $manifestRows[$i]
                $library = $case.Library
                $start = $reports[2 * $i]
                $finish = $reports[2 * $i + 1]
                if ($start.Groups[1].Value -cne $case.Report -or $start.Groups[2].Value -cne 'running' -or
                    $finish.Groups[1].Value -cne $case.Report -or $finish.Groups[2].Value -cne 'passed') {
                    throw "Incomplete $($case.Report) section in $mode. Inspect $logPath"
                }
                $trace = $log.Substring($start.Index, $finish.Index - $start.Index)
                foreach ($symbol in $case.Symbols) {
                    $symbolLibrary = if ($symbol -match '^_{1,2}CFURL') { 'CoreFoundationLib' } else { $library }
                    if (-not $trace.Contains('PPC service ' + $symbolLibrary + ':' + $symbol + ' ')) {
                        throw "$library $Group did not execute native $symbol in $mode"
                    }
                }
                if ($Detailed) { Write-Output "$($case.Report) $mode passed" }
            }
            Write-Output "${application} ${mode}: $($manifestRows.Count) checks passed"
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
            Copy-Item -LiteralPath $logPath -Destination (Join-Path $fixture ($mode.TrimStart('-') + '.txt'))
        }
    }
    Write-Output "Logs are in $fixture"
}

function Invoke-Check-cfm68k {
    param([string]$ROMAW, [switch]$TextCommon)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $cases = @(
        @{ Generator = 'make_cfm68k_probe.py'; App = 'CFM-68K Probe'; Options = @(); Name = 'flat' },
        @{ Generator = 'make_cfm68k_probe.py'; App = 'CFM-68K Probe'; Options = @('--segmented'); Name = 'segmented' },
        @{ Generator = 'make_cfm68k_probe.py'; App = 'CFM-68K Probe'; Options = @('--exit-trap'); Name = 'exit-trap' },
        @{ Generator = 'make_cfm68k_shared_probe.py'; App = 'CFM-68K Shared Probe'; Options = @(); Name = 'shared' },
        @{ Generator = 'make_cfm68k_classic_probe.py'; App = 'Classic CFM-68K Probe'; Options = @(); Name = 'classic' },
        @{ Generator = 'make_classic_ppc_memory_probe.py'; App = 'Classic PPC Memory Probe'; Options = @(); Name = 'classic-ppc-memory' }
    )
    if ($TextCommon) {
        $cases += @{ Generator = 'make_cfm68k_text_probe.py'; App = 'CFM-68K TextCommon Probe'; Options = @(); Name = 'TextCommon' }
    }
    foreach ($case in $cases) {
        $fixture = Join-Path $workspace ('build/cfm68k-' + $case.Name + '.' + [guid]::NewGuid().ToString('N'))
        & python (Join-Path $PSScriptRoot $case.Generator) $fixture @($case.Options)
        if ($LASTEXITCODE -ne 0) { throw 'Could not generate the CFM-68K fixture' }
        $app = Join-Path $fixture $case.App
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $app + '"') `
            -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(15000)) { throw "CFM-68K probe timed out: $fixture" }
            if ($process.ExitCode -ne 0) { throw "CFM-68K probe exited with $($process.ExitCode): $fixture" }
            $log = [IO.File]::ReadAllText((Join-Path $fixture 'stderr.txt'))
            $completed = if ($case.Name -like 'classic*') { 'CFM-68K classic connection closure released' } else { 'CFM-68K fragment completed' }
            if ($log -notmatch $completed -or
                $log -match 'launch failed|win_exception|Illegal instruction|Fatal error') {
                throw "CFM-68K probe failed: $fixture"
            }
            Write-Output ('CFM-68K ' + $case.Name + ': passed')
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        }
    }
}

function Invoke-Check-fileplay-capture {
    param([string]$ROMAW, [string]$MaceTest)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    if (-not $MaceTest) { $MaceTest = Join-Path $workspace 'build\mace-test.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $MaceTest = (Resolve-Path -LiteralPath $MaceTest).Path
    foreach ($executionMode in @('-ppc-jit', '-no-ppc-jit')) {
        $fixture = Join-Path $workspace ('build\fileplay-capture.' + [guid]::NewGuid().ToString('N'))
        $profile = Join-Path $fixture '.romaw'
        $apps = Join-Path $profile 'Apps'
        New-Item -ItemType Directory -Path (Join-Path $apps 'System Folder') -Force | Out-Null
        $legacyMode = if ($executionMode -eq '-ppc-jit') { 'SoundOff' } else { 'PretendSound' }
        Set-Content -LiteralPath (Join-Path $profile 'ROMAW.prefs') -Encoding ascii -Value @(
            '[HostDrives]', 'SidecarDrives=none', '[APPL.PPRB]', "$legacyMode=1"
        )
        & python (Join-Path $PSScriptRoot 'make_ppc_fileplay_probe.py') $apps
        if ($LASTEXITCODE -ne 0) { throw 'Could not generate file-playback probe' }
        & $MaceTest (Join-Path $fixture 'reference-mac3.pcm') (Join-Path $fixture 'reference-mac6.pcm')
        if ($LASTEXITCODE -ne 0) { throw 'MACE reference export failed; build it with check-media.sh mace' }
        $launch = [Diagnostics.ProcessStartInfo]::new()
        $launch.FileName = $ROMAW
        $launch.Arguments = $executionMode + ' -syszone 8m -applzone 64m -sdlaudio test -debug trace "' +
            (Join-Path $apps 'PPC Fileplay Probe') + '"'
        $launch.WorkingDirectory = $fixture
        $launch.UseShellExecute = $false
        $launch.CreateNoWindow = $true
        $launch.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $launch.EnvironmentVariables['ROMAW_TEST_AUDIO_CAPTURE'] = Join-Path $fixture 'output.raw'
        $launch.EnvironmentVariables['USERPROFILE'] = $fixture
        $process = [Diagnostics.Process]::Start($launch)
        try {
            if (-not $process.WaitForExit(15000)) { throw "Playback capture timed out: $fixture" }
            if ($process.ExitCode -ne 0) { throw "Playback capture exited with $($process.ExitCode): $fixture" }
            & python (Join-Path $PSScriptRoot 'check_fileplay_capture.py') $fixture
            if ($LASTEXITCODE -ne 0) { throw "Captured samples differ: $fixture" }
            Write-Output "${executionMode}: sample capture passed with legacy $legacyMode"
        }
        finally {
            if (-not $process.HasExited) { $process.Kill() }
            $process.Dispose()
        }
    }
}

function Invoke-Check-startup-profile {
    param([string]$ROMAW, [string]$Gdb = 'C:\msys64\mingw64\bin\gdb.exe')
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $check = Join-Path $workspace ('build\ppc-startup-profile.' + [guid]::NewGuid().ToString('N'))
    $profile = Join-Path $check 'Profile'
    $prefs = Join-Path $profile '.romaw\ROMAW.prefs'
    New-Item -ItemType Directory -Force -Path (Split-Path $prefs -Parent) | Out-Null
    $original = "[HostDrives]`nSidecarDrives=none`n[APPL.PPRB]`nSystemVersion=7.1.1`nSysZone=2m`nApplZone=6m`nSoundOn=1`nCustom=retain`n[APPL.OTHR]`nSysZone=1m`n"
    $savedProfile = $env:USERPROFILE
    New-Item -ItemType Junction -Path (Join-Path $profile '.romaw\Apps') `
        -Target (Join-Path $savedProfile '.romaw\Apps') | Out-Null
    $env:USERPROFILE = $profile
    function Write-Prefs {
        [IO.File]::WriteAllText($prefs, $original)
    }
    function Run-Success([string]$App) {
        $process = Start-Process -FilePath $ROMAW -ArgumentList ('-debug trace "' + $App + '"') `
            -WorkingDirectory $check -WindowStyle Hidden -PassThru
        $null = $process.Handle
        $completed = $false
        $deadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 100
            try { $text = [IO.File]::ReadAllText($prefs) } catch [IO.IOException] { continue }
            if ($text -match 'SystemVersion=9\.2\.2' -and $text -match 'SysZone=16777216' -and $text -match 'ApplZone=67108864') {
                # Replacement processes share the launch directory, so their debug
                # logs can overlap during shutdown. Completion is established by
                # the successful-run preference save and all owned children exiting.
                $running = Get-CimInstance Win32_Process -Filter "Name='ROMAW.exe'" | Where-Object {
                    $_.CommandLine -and ($_.CommandLine.Contains($check) -or $_.CommandLine.Contains($check.Replace('\', '/')))
                }
                if (-not $running) { $completed = $true; break }
            }
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not $completed) {
            throw "Startup retry chain failed; inspect $check"
        }
        if ($text -notmatch 'Custom=retain' -or $text -notmatch 'SoundOn=1' -or $text -notmatch 'SysZone=1m') {
            throw 'Saving the winning profile changed unrelated preferences'
        }
        if (-not $process.WaitForExit(5000) -or $process.ExitCode -ne 0) { throw 'Original startup process did not exit cleanly' }
    }
    try {
        foreach ($mode in @('success', 'initializer-fault', 'launch-stub', 'library-failure', 'main-failure', 'running-failure', 'termination-failure')) {
            $fixture = Join-Path $check $mode
            & python (Join-Path $PSScriptRoot 'make_ppc_startup_profile_probe.py') $fixture $mode
            if ($LASTEXITCODE -ne 0) { throw 'Could not generate startup fixture' }
            $app = (Join-Path $fixture 'PPC Startup Profile Probe').Replace('\', '/')
            Write-Prefs
            if ($mode -in @('success', 'initializer-fault', 'launch-stub')) {
                Run-Success $app
                $hash = (Get-FileHash -LiteralPath $prefs).Hash
                Run-Success $app
                if ((Get-FileHash -LiteralPath $prefs).Hash -ne $hash) { throw 'Reusing the profile rewrote preferences' }
                Write-Output 'Retry chain and per-app profile reuse passed'
                continue
            }
            $version = if ($mode -eq 'library-failure') { '9.2.2' } else { '8.6' }
            $commands = @'
set pagination off
set confirm off
set print thread-events off
break ROMlib_win_ppc_error
commands
silent
printf "EXPECTED_STARTUP_ERROR: %s\n", message
quit 2
end
'@ + "`nrun -debug trace -system $version -syszone 16m -applzone 64m -ppc-startup-retry `"$app`"`n"
            $script = Join-Path $fixture 'test.gdb'
            [IO.File]::WriteAllText($script, $commands)
            $output = Join-Path $fixture 'gdb.txt'
            $process = Start-Process -FilePath $Gdb -ArgumentList ('-batch -x "' + $script + '" "' + $ROMAW + '"') `
                -WorkingDirectory $check -WindowStyle Hidden -PassThru -RedirectStandardOutput $output `
                -RedirectStandardError (Join-Path $fixture 'gdb-error.txt')
            $null = $process.Handle
            if (-not $process.WaitForExit(15000)) { Stop-Process -Id $process.Id; throw 'Failure probe timed out' }
            $result = [IO.File]::ReadAllText($output)
            if ($process.ExitCode -ne 2 -or $result -notmatch 'EXPECTED_STARTUP_ERROR') { throw "Unexpected failure result: $result" }
            $expected = switch ($mode) {
                'library-failure' { 'CarbonLib initialization failed' }
                'main-failure' { 'illegal PowerPC instruction' }
                'running-failure' { 'CarbonLib initialization failed' }
                default { 'unsupported API: InterfaceLib:ROMAWProfileTermFailure' }
            }
            if ($result -notmatch $expected) { throw "Wrong failure for ${mode}: $result" }
            $unchanged = [IO.File]::ReadAllText($prefs)
            if ($unchanged -notmatch 'SystemVersion=7\.1\.1' -or
                $unchanged -notmatch 'SysZone=2m' -or $unchanged -notmatch 'ApplZone=6m') {
                throw 'Failed startup saved a profile'
            }
            Write-Output ($mode + ': controlled final failure, no retry loop and no saved profile')
        }
    }
    finally {
        $env:USERPROFILE = $savedProfile
        # Terminate only probe processes carrying this unique fixture directory.
        Get-CimInstance Win32_Process -Filter "Name='ROMAW.exe'" | Where-Object {
            $_.CommandLine -and ($_.CommandLine.Contains($check) -or $_.CommandLine.Contains($check.Replace('\', '/')))
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
    }
}

function Invoke-Check-system-request {
    param([string]$ROMAW)
    $ErrorActionPreference = 'Stop'
    $workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $check = Join-Path $workspace ('build/system-request.' + [guid]::NewGuid().ToString('N'))
    $originalProfile = $env:USERPROFILE
    try {
        foreach ($mode in @('explicit', 'known', 'running')) {
            $fixture = Join-Path $check $mode
            $profile = Join-Path $fixture 'profile'
            New-Item -ItemType Directory -Force (Join-Path $profile '.romaw/Apps/System Folder') | Out-Null
            $prefs = Join-Path $profile '.romaw/ROMAW.prefs'
            [IO.File]::WriteAllText($prefs, "[HostDrives]`nSidecarDrives=none`n[APPL.PPRB]`nSystemVersion=7.1.1`nCustom=retain`n[FNDR.MACS]`nSystemVersion=7.1.1`n")
            & python (Join-Path $PSScriptRoot 'make_ppc_system_request_probe.py') $fixture $mode
            if ($LASTEXITCODE -ne 0) { throw 'Could not generate system requirement fixture' }
            $env:USERPROFILE = $profile
            $app = (Join-Path $fixture 'PPC System Request Probe').Replace('\', '/')
            $process = Start-Process $ROMAW -ArgumentList ('-debug trace -syszone 8m -applzone 16m "' + $app + '"') `
                -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            $passed = $false
            do {
                Start-Sleep -Milliseconds 100
                $log = Get-Content -LiteralPath (Join-Path $fixture 'stderr.txt') -Raw -ErrorAction SilentlyContinue
                $saved = [IO.File]::ReadAllText($prefs)
                $owned = @(Get-CimInstance Win32_Process -Filter "Name='ROMAW.exe'" | Where-Object {
                        $_.CommandLine -and ($_.CommandLine.Contains($fixture) -or $_.CommandLine.Contains($fixture.Replace('\', '/')))
                    })
                if ($mode -eq 'running') {
                    if ($log -match 'system-version request left to running application after event processing') {
                        if ($saved -notmatch 'SystemVersion=7\.1\.1' -or $saved -match 'SystemVersion=9\.0\.1') {
                            throw 'An application already processing events changed its profile'
                        }
                        $passed = $true
                        break
                    }
                }
                else {
                    $version = if ($mode -eq 'known') { '8.6.0' } else { '9.0.1' }
                    if ($saved.Contains('SystemVersion=' + $version) -and -not $owned.Count) {
                        # Replacements share CRT log paths with exiting parents.
                        # A successful-run profile save plus all owned processes
                        # exiting is the stable completion signal.
                        if ($saved -notmatch 'Custom=retain') {
                            throw 'The required-version run changed unrelated preferences'
                        }
                        $passed = $true
                        break
                    }
                }
            } while ([DateTime]::UtcNow -lt $deadline)
            if (-not $passed) { throw "System request $mode failed; inspect $fixture" }
            $owned | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
            $process.Dispose()
            Write-Output ("${mode}: system-version request handling passed")
        }
    }
    finally {
        $env:USERPROFILE = $originalProfile
        Get-CimInstance Win32_Process -Filter "Name='ROMAW.exe'" | Where-Object {
            $_.CommandLine -and ($_.CommandLine.Contains($check) -or $_.CommandLine.Contains($check.Replace('\', '/')))
        } | ForEach-Object { Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue }
    }
}

function Invoke-Check-finder-services {
    param(
        [string]$ROMAW,
        [ValidateSet('Both', 'Jit', 'Interpreter')]
        [string]$Engine = 'Both'
    )
    $ErrorActionPreference = 'Stop'
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (-not $ROMAW) { $ROMAW = Join-Path $root 'windows-binaries/bin/ROMAW.exe' }
    $ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
    $fixture = Join-Path $root ('build/finder-services.' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
    & python (Join-Path $PSScriptRoot 'make_ppc_finder_services_probe.py') $fixture
    [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
    if ($LASTEXITCODE) { throw 'Cannot generate Finder services probe' }
    $modes = if ($Engine -eq 'Both') { @('-no-ppc-jit', '-ppc-jit') }
    elseif ($Engine -eq 'Jit') { @('-ppc-jit') }
    else { @('-no-ppc-jit') }
    foreach ($mode in $modes) {
        & python (Join-Path $PSScriptRoot 'make_ppc_finder_services_probe.py') $fixture
        if ($LASTEXITCODE) { throw 'Cannot reset Finder services fixture' }
        $savedProfile = $env:USERPROFILE
        try {
            $env:USERPROFILE = $fixture
            $arguments = $mode + ' -system 8.6 -syszone 16m -applzone 64m -sdlaudio test -debug trace "' + (Join-Path $fixture 'PPC Finder Services Probe') + '"'
            $process = Start-Process -FilePath $ROMAW -ArgumentList $arguments -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
        }
        finally { $env:USERPROFILE = $savedProfile }
        try {
            if (-not $process.WaitForExit(15000)) { throw "Finder services timed out: $fixture" }
            if ($process.ExitCode) { throw "Finder services exited $($process.ExitCode): $fixture" }
            $log = Get-Content (Join-Path $fixture 'stderr.txt') -Raw
            [IO.File]::WriteAllText((Join-Path $fixture ($mode + '.log')), $log)
            if ($log -notmatch 'debugstr: PPC Finder services: passed' -or
                $log -notmatch 'PowerPC fragment completed' -or
                $log -match 'win_exception|PowerPC launch failed|Illegal instruction') {
                throw "Finder services failed: $fixture"
            }
            Write-Output ("${mode}: Finder services, object callbacks and resource-fork icon refresh passed")
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}

. (Join-Path $PSScriptRoot '../../tools/regression-support.ps1')
Invoke-RegressionCheck -Names @('system750', 'system750-isolated', 'aot', 'carbon-bundle-copy', 'carbon-cf-real', 'cfm68k', 'fileplay-capture', 'startup-profile', 'system-request', 'finder-services') -CheckArguments $args
