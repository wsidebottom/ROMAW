# +===================================================================
#   File:      run-romaw-tests.ps1
#
#   Summary:   Run and validate grouped PowerPC applications and independent integration fixtures.
#
#   Classes:   None declared.
#
#   Functions: No explicitly exported functions.
#
#   Origin:    Existing repository source in synppc/tests; original attribution and provenance comments are preserved
#              below.
#
#   Copyright and Legal notices: File-specific notices, where present, are retained below. License reference:
#              synppc/COPYING.
# ===================================================================+

param(
    [string]$ROMAW,
    [switch]$Jit,
    [switch]$Interpreter,
    [switch]$Aot,
    [switch]$Full,
    [string[]]$Group,
    [switch]$Exhaustive,
    [switch]$Detailed,
    [switch]$Plan
)
$ErrorActionPreference = 'Stop'
if (($Jit -and $Interpreter) -or ($Aot -and ($Jit -or $Interpreter))) { throw 'Choose JIT, AOT or interpreter execution' }
$workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }

function Invoke-ROMAWCheck {
    param($Launch, [string]$Fixture, [string]$JitOption)
    $app = Join-Path $Fixture $Launch.App
    $logPath = Join-Path $Fixture 'stderr.txt'
    $profile = $env:USERPROFILE
    try {
        if ($Aot -or $Launch.Cases -or $Launch.Isolated) { $env:USERPROFILE = $Fixture }
        $process = Start-Process -FilePath $ROMAW -ArgumentList ($JitOption + '-syszone 8m -applzone 64m -sdlaudio test -debug trace "' + $app + '"') `
            -WorkingDirectory $Fixture -WindowStyle Hidden -PassThru
    }
    finally { $env:USERPROFILE = $profile }
    $null = $process.Handle
    try {
        $timeout = if ($Launch.Timeout) { $Launch.Timeout } elseif ($Launch.Cases) { 60000 } else { 15000 }
        $deadline = [DateTime]::UtcNow.AddMilliseconds($timeout)
        while (-not $process.WaitForExit(250)) {
            $tail = (Get-Content -LiteralPath $logPath -Tail 12 -ErrorAction SilentlyContinue) -join "`n"
            if ($tail -match 'PowerPC launch failed|win_exception|Illegal instruction') {
                throw "$($Launch.App) failed; inspect $logPath"
            }
            if ([DateTime]::UtcNow -ge $deadline) { throw "$($Launch.App) timed out; inspect $logPath" }
        }
        if ($process.ExitCode -ne 0) { throw "$($Launch.App) exited with $($process.ExitCode); inspect $logPath" }
        $log = [IO.File]::ReadAllText($logPath)
        if ($log -notmatch 'PowerPC fragment completed' -or $log -match 'PowerPC launch failed|win_exception|Illegal instruction') {
            throw "$($Launch.App) failed; inspect $logPath"
        }
        if ($Interpreter -and $log -match 'PPC JIT executed') { throw 'The interpreter opt-out executed native code' }
        if ($Launch.App -eq 'PPC Native Memory Probe' -and $log -notmatch 'PPC_NATIVE_MEMORY_OK') {
            throw "The fat application did not complete its native Memory Manager checks; inspect $logPath"
        }
        if ($Aot -and $log -notmatch 'PPC AOT saved cache: 1') { throw "AOT cache was not saved; inspect $logPath" }
        return $log
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

function Test-CheckResult {
    param($case, [string]$log, [switch]$Warm, [string]$LaunchLog = $log)
    if ($case.App -eq 'PPC Carbon Fallback Checks') {
        $rows = @(foreach ($manifest in 'cf-values-batch.json', 'cf-collections-batch.json') {
                (Get-Content (Join-Path $workspace ('carbonlib/plans/' + $manifest)) -Raw | ConvertFrom-Json).entries
            })
        $rows += @{ name = 'CFDateGetAbsoluteTime' }
        foreach ($library in 'CarbonLib', 'CoreFoundationLib') {
            foreach ($row in $rows) {
                if ($library -eq 'CarbonLib' -and $row.companion_only) { continue }
                if (-not $log.Contains('PPC service ' + $library + ':' + $row.name + ' ')) {
                    throw "Fallback missed $($library):$($row.name)"
                }
            }
        }
    }
    if ($case.App -eq 'PPC Carbon Classic185 Probe') {
        $accessors = Get-Content (Join-Path $workspace 'carbonlib/tests/classic_access_cases.json') -Raw | ConvertFrom-Json
        foreach ($accessor in $accessors) {
            if (-not $log.Contains('PPC service CarbonLib:' + $accessor.name + ' ')) {
                throw "Classic record fixture missed native $($accessor.name)"
            }
        }
    }
    if ($case.App -in @('PPC Carbon StrOps Probe', 'PPC Core StrOps Probe')) {
        $library = if ($case.App -eq 'PPC Carbon StrOps Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFStringCompare', 'CFStringCompareWithOptions', '__CFStringMakeConstantString',
                'CFStringCreateCopy', 'CFStringHasPrefix', 'CFStringHasSuffix', 'CFStringFind', 'CFStringGetIntValue')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "String fixture missed native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon URLResolve Probe', 'PPC Core URLResolve Probe')) {
        $library = if ($case.App -eq 'PPC Carbon URLResolve Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFURLCopyAbsoluteURL', 'CFURLCopyFileSystemPath', 'CFURLGetFileSystemRepresentation', 'CFURLCreateWithString')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "URL resolution fixture missed native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon URLCreate Probe', 'PPC Core URLCreate Probe')) {
        $library = if ($case.App -eq 'PPC Carbon URLCreate Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('_CFURLAlloc', '_CFURLInitWithString', 'CFURLCreateWithString', 'CFURLCreateWithBytes', 'CFURLCreateData',
                'CFURLCreateWithFileSystemPath', 'CFURLCreateWithFileSystemPathRelativeToBase',
                'CFURLCreateCopyDeletingLastPathComponent', 'CFURLCreateCopyAppendingPathExtension', 'CFURLCreateCopyDeletingPathExtension')) {
            $symbolLibrary = if ($symbol -like '_CFURL*') { 'CoreFoundationLib' } else { $library }
            if (-not $log.Contains('PPC service ' + $symbolLibrary + ':' + $symbol + ' ')) {
                throw "URL creation fixture missed native $($symbolLibrary):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon URLComp Probe', 'PPC Core URLComp Probe')) {
        $library = if ($case.App -eq 'PPC Carbon URLComp Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFURLCopyHostName', 'CFURLCopyUserName', 'CFURLCopyPassword', 'CFURLCopyParameterString',
                'CFURLCopyQueryString', 'CFURLCopyFragment', 'CFURLGetPortNumber', '__CFURLReservedPtr', '__CFURLSetReservedPtr',
                'CFURLCopyPathExtension', 'CFURLCreateFromFileSystemRepresentation',
                'CFURLCreateFromFileSystemRepresentationRelativeToBase', 'CFURLCreateFromFSRef', 'CFURLGetFSRef')) {
            $symbolLibrary = if ($symbol -like '__CFURL*') { 'CoreFoundationLib' } else { $library }
            if (-not $log.Contains('PPC service ' + $symbolLibrary + ':' + $symbol + ' ')) {
                throw "URL component fixture missed native $($symbolLibrary):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon URL Probe', 'PPC Core URL Probe')) {
        $library = if ($case.App -eq 'PPC Carbon URL Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFURLGetTypeID', 'CFURLCanBeDecomposed', 'CFURLGetString', 'CFURLGetBaseURL',
                'CFURLHasDirectoryPath', 'CFURLCopyScheme', 'CFURLCopyNetLocation', 'CFURLCopyPath',
                'CFURLCopyStrictPath', 'CFURLCopyResourceSpecifier', 'CFURLCopyLastPathComponent',
                'CFURLCreateCopyAppendingPathComponent')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "URL fixture missed native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon BLife Probe', 'PPC Core BLife Probe')) {
        $library = if ($case.App -eq 'PPC Carbon BLife Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFBundleCreate', 'CFBundleGetLocalInfoDictionary', 'CFBundleUnloadExecutable',
                'CFBundleGetVersionNumber', 'CFBundleGetDevelopmentRegion', 'CFBundleGetFunctionPointersForNames',
                'CFBundleGetDataPointersForNames')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Bundle lifecycle fixture missed native $($library):$symbol"
            }
        }
    }
    if ($case.App -eq 'PPC Shared Probe') {
        # Dependencies initialize before a grouped app prints its check markers.
        $prerequisite = $LaunchLog.IndexOf('PPC initializing ProbePrereq')
        $cycle = $LaunchLog.IndexOf('PPC initializing ProbeCycle')
        if ($prerequisite -lt 0 -or $cycle -le $prerequisite) { throw 'Cyclic library prerequisite initialized out of order' }
    }
    if ($Aot -and $case.App -eq 'PPC Private Memory Probe') {
        $startup = if ($Warm) { 'PPC AOT loaded 4096 cached blocks' } else { 'PPC AOT prepared 4096 blocks in 2 linked images' }
        $lateCount = [regex]::Match($log, '(?m)^trace: PPC AOT prepared (\d+) blocks in 1 linked images\r?$')
        $lateInit = '(?m)^trace: PPC initializing PrivateModule\r?\ntrace: PPC call [^\n]+\r?\n' +
        'trace: PPC JIT executed 27 instructions in ([1-9][0-9]*) blocks \(0 compilations, \1 cached\)'
        $lateTerm = '(?m)^trace: PPC JIT executed 26 instructions in ([1-9][0-9]*) blocks \(0 compilations, \1 cached\)'
        if ($log -notmatch $startup -or -not $lateCount.Success -or
            ([int]$lateCount.Groups[1].Value -eq 0) -ne [bool]$Warm -or
            [regex]::Matches($log, $lateInit).Count -ne 5 -or [regex]::Matches($log, $lateTerm).Count -ne 5) {
            throw 'The memory-loaded library lifecycle did not run from the expected AOT cache'
        }
        if ($Warm -and ($log -notmatch 'PPC AOT prepared 0 blocks in 2 linked images' -or
                $log -match 'PPC AOT prepared [1-9][0-9]* blocks in 1 linked images|PPC AOT published late cache:')) {
            throw 'The warm launch prepared or republished late blocks'
        }
        $checkpoint = 'PPC AOT prepared [1-9][0-9]* blocks in 1 linked images\r?\n' +
        'trace: PPC AOT published late cache: 1\r?\ntrace: PPC initializing PrivateModule'
        if (-not $Warm -and $log -notmatch $checkpoint) {
            throw 'The late AOT cache was not saved before the memory fragment initializer'
        }
        if ($log -notmatch 'PPC service InterfaceLib:ExitToShell ') {
            throw 'The private memory app did not exit through the Toolbox service'
        }
    }
    if ($case.App -in @('PPC Carbon Pascal Probe', 'PPC StdCLib Pascal Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Pascal Probe') { 'CarbonLib' } else { 'StdCLib' }
        foreach ($symbol in @('PLstrlen', 'PLstrcmp', 'PLstrncmp', 'PLstrcpy', 'PLstrncpy', 'PLstrcat',
                'PLstrncat', 'PLstrchr', 'PLstrrchr', 'PLstrpbrk', 'PLstrspn', 'PLstrstr', 'PLpos')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Pascal probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon CF Layout Probe', 'PPC Core CF Layout Probe')) {
        $library = if ($case.App -eq 'PPC Carbon CF Layout Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFArrayApplyFunction', 'CFArrayGetCount', 'CFArrayGetValueAtIndex', 'CFArrayGetValues', 'CFGetTypeID', 'CFDataGetLength', 'CFDataGetBytePtr',
                'CFDataGetMutableBytePtr', 'CFDataGetBytes', 'CFStringGetLength', 'CFStringGetCharactersPtr')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "CoreFoundation probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Date Probe', 'PPC Core Date Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Date Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFAbsoluteTimeGetCurrent', 'CFDateGetAbsoluteTime', 'CFDateGetTimeIntervalSinceDate', 'CFDateCompare')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Date probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -eq 'PPC Carbon GWorld Probe') {
        foreach ($symbol in @('GetGWorld', 'GetGWorldDevice', 'GetGWorldPixMap', 'GetPixBaseAddr', 'GetPixelsState')) {
            if (-not $log.Contains('PPC service CarbonLib:' + $symbol + ' ')) {
                throw "GWorld probe did not execute native $symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Search Probe', 'PPC Core Search Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Search Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        if (-not $log.Contains('PPC service ' + $library + ':CFArrayBSearchValues ')) {
            throw "Search probe did not execute native $library"
        }
    }
    if ($case.App -in @('PPC Carbon String Pointer Probe', 'PPC Core String Pointer Probe')) {
        $library = if ($case.App -eq 'PPC Carbon String Pointer Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFStringGetCStringPtr', 'CFStringGetPascalStringPtr')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Borrowed pointer probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -eq 'PPC Carbon Bundle Probe') {
        foreach ($symbol in @('CFBundleGetFunctionPointerForName', 'CFBundleGetDataPointerForName')) {
            if (-not $log.Contains('PPC service CarbonLib:' + $symbol + ' ')) {
                throw "Bundle probe did not execute native $symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon BState Probe', 'PPC Core BState Probe')) {
        $library = if ($case.App.StartsWith('PPC Carbon')) { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFBundleGetTypeID', 'CFBundleCopyBundleURL', 'CFBundleGetInfoDictionary', 'CFBundleGetValueForInfoDictionaryKey', 'CFBundleGetIdentifier', 'CFBundleIsExecutableLoaded', 'CFBundleLoadExecutable', 'CFBundleGetPlugIn')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Bundle state probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon CF Query Probe', 'PPC Core CF Query Probe')) {
        $library = if ($case.App.StartsWith('PPC Carbon')) { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFStringGetMaximumSizeForEncoding', 'CFStringConvertEncodingToNSStringEncoding', 'CFStringConvertNSStringEncodingToEncoding', 'CFStringConvertEncodingToWindowsCodepage', 'CFStringConvertWindowsCodepageToEncoding', 'CFStringIsEncodingAvailable', 'CFStringGetMostCompatibleMacStringEncoding')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Query probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon CF Encode Probe', 'PPC Core CF Encode Probe')) {
        $library = if ($case.App.StartsWith('PPC Carbon')) { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFStringGetBytes', 'CFStringGetFastestEncoding')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Encoding probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon CF Pascal Probe', 'PPC Core CF Pascal Probe', 'PPC Carbon CF CString Probe', 'PPC Core CF CString Probe')) {
        $library = if ($case.App.StartsWith('PPC Carbon')) { 'CarbonLib' } else { 'CoreFoundationLib' }
        $symbol = if ($case.App.Contains('CString')) { 'CFStringGetCString' } else { 'CFStringGetPascalString' }
        if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
            throw "Conversion probe did not execute native $($library):$symbol"
        }
    }
    if ($case.App -eq 'PPC UniChar Probe' -and -not $log.Contains('PPC service CoreFoundationLib:CFUniCharIsMemberOf ')) {
        throw 'Companion probe did not execute the native CoreFoundationLib entry'
    }
    if ($case.App -in @('PPC Carbon CF String Probe', 'PPC Core CF String Probe')) {
        $library = if ($case.App -eq 'PPC Carbon CF String Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFStringGetCharacterAtIndex', 'CFStringGetCharacters')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "String probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Array New Probe', 'PPC Core Array New Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Array New Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFArrayCreate', 'CFArrayGetTypeID')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Array creation probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Own Probe', 'PPC Core Own Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Own Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFRetain', 'CFRelease', 'CFGetRetainCount', 'CFGetAllocator')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Ownership probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Create Probe', 'PPC Core Create Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Create Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFAllocatorCreate', 'CFAllocatorGetTypeID')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Creation probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Alloc Probe', 'PPC Core Alloc Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Alloc Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        foreach ($symbol in @('CFAllocatorAllocate', 'CFAllocatorReallocate', 'CFAllocatorDeallocate')) {
            if (-not $log.Contains('PPC service ' + $library + ':' + $symbol + ' ')) {
                throw "Allocation probe did not execute native $($library):$symbol"
            }
        }
    }
    if ($case.App -in @('PPC Carbon Allocator Probe', 'PPC Core Allocator Probe')) {
        $library = if ($case.App -eq 'PPC Carbon Allocator Probe') { 'CarbonLib' } else { 'CoreFoundationLib' }
        if (-not $log.Contains('PPC service ' + $library + ':CFAllocatorGetDefault ')) {
            throw "Allocator probe did not execute native $library"
        }
    }
    if (-not $Interpreter -and $case.App -in @('PPC JIT Probe', 'PPC Code Publish Probe') -and
        $LaunchLog -notmatch 'PPC JIT executed [1-9][0-9]* instructions') {
        throw 'The native-block probe completed without executing native code'
    }
}

$cases = @(
    @{Generator = 'make_ppc_carbon_cf_values_probe.py'; App = 'PPC Carbon Fallback Checks'; Arguments = @('--grouped-fallback') },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe' },
    @{Generator = 'make_ppc_carbon_cfstrings_probe.py'; App = 'PPC Carbon StrOps Probe' },
    @{Generator = 'make_ppc_corefoundation_cfstrings_probe.py'; App = 'PPC Core StrOps Probe' },
    @{Generator = 'make_ppc_carbon_cfstrings_foreign_probe.py'; App = 'PPC Carbon StrOps Probe' },
    @{Generator = 'make_ppc_probe.py'; App = 'PPC Toolbox Probe' },
    @{Generator = 'make_ppc_inplace_string_probe.py'; App = 'PPC Inplace String Probe' },
    @{Generator = 'make_ppc_closure_probe.py'; App = 'PPC Closure Probe' },
    @{Generator = 'make_ppc_private_named_probe.py'; App = 'PPC Private Named Probe' },
    @{
        Generator = 'make_ppc_private_disk_probe.py'
        App = 'PPC Private Disk Probe'
        FragmentFiles = @('PrivateDiskModule')
        Directories = @('NotAFragment')
    },
    @{Generator = 'make_ppc_private_memory_probe.py'; App = 'PPC Private Memory Probe' },
    @{Generator = 'make_ppc_shared_probe.py'; App = 'PPC Shared Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('intl'); Report = 'PPC International Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('grow'); Report = 'PPC Grow Zone Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('queue'); Report = 'PPC Event Queue Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('linebreak'); Report = 'PPC Line Break Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('crm'); Report = 'PPC CRM Callback Probe' },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Arguments = @('ot'); Report = 'PPC Open Transport Callback Probe' },
    @{Generator = 'make_ppc_standard_alert_probe.py'; App = 'PPC Standard Alert Probe' },
    @{Generator = 'make_ppc_decimal_probe.py'; App = 'PPC Decimal Probe' },
    @{Generator = 'make_ppc_fpu_probe.py'; App = 'PPC FPU Probe' },
    @{Generator = 'make_ppc_jit_probe.py'; App = 'PPC JIT Probe' },
    @{Generator = 'make_ppc_code_publish_probe.py'; App = 'PPC Code Publish Probe' },
    @{Generator = 'make_ppc_fused_probe.py'; App = 'PPC Fused Probe' },
    @{Generator = 'make_ppc_fpr_probe.py'; App = 'PPC FPR Probe' },
    @{Generator = 'make_ppc_init_probe.py'; App = 'PPC INIT Probe' },
    @{Generator = 'make_ppc_windows_probe.py'; App = 'PPC Windows Probe' },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe' },
    @{Generator = 'make_ppc_component_probe.py'; App = 'PPC Component Probe' },
    @{Generator = 'make_ppc_layer_probe.py'; App = 'PPC Layer Probe' },
    @{Generator = 'make_ppc_memory_probe.py'; App = 'PPC Memory Probe' },
    @{Generator = 'make_ppc_native_memory_probe.py'; App = 'PPC Native Memory Probe'; Isolated = $true },
    @{Generator = 'make_ppc_native_libraries_probe.py'; App = 'PPC Native Libraries Probe'; Isolated = $true },
    @{Generator = 'make_ppc_import_reentry_probe.py'; App = 'PPC Import Reentry Probe'; Isolated = $true },
    @{Generator = 'make_ppc_appearance_font_probe.py'; App = 'PPC Appearance Font Probe'; Isolated = $true },
    @{Generator = 'make_ppc_appearance_state_probe.py'; App = 'PPC Appearance State Probe'; Isolated = $true },
    @{Generator = 'make_ppc_queue_probe.py'; App = 'PPC Queue Probe'; Isolated = $true },
    @{Generator = 'make_ppc_menus_library_probe.py'; App = 'PPC Menus Library Probe'; Isolated = $true },
    @{Generator = 'make_ppc_handle_copy_probe.py'; App = 'PPC Handle Copy Probe' },
    @{Generator = 'make_ppc_process_probe.py'; App = 'PPC Process Probe' },
    @{Generator = 'make_ppc_thread_probe.py'; App = 'PPC Thread Probe' },
    @{Generator = 'make_ppc_appleevent_probe.py'; App = 'PPC AppleEvent Probe' },
    @{Generator = 'make_ppc_resource_probe.py'; App = 'PPC Resource Probe' },
    @{Generator = 'make_ppc_file_probe.py'; App = 'PPC File Probe'; Files = @('Probe.data') },
    @{Generator = 'make_ppc_volume_probe.py'; App = 'PPC Volume Probe' },
    @{Generator = 'make_ppc_sound_probe.py'; App = 'PPC Sound Probe' },
    @{Generator = 'make_ppc_fileplay_probe.py'; App = 'PPC Fileplay Probe'; Files = @('Probe.aiff', 'Probe.mac3', 'Probe.mac6') },
    @{Generator = 'make_ppc_movie_probe.py'; App = 'PPC Movie Probe'; Files = @('Probe.mov') },
    @{Generator = 'make_ppc_speech_probe.py'; App = 'PPC Speech Probe' },
    @{Generator = 'make_ppc_appearance_probe.py'; App = 'PPC Appearance Probe' },
    @{Generator = 'make_ppc_popup_probe.py'; App = 'PPC Popup Probe' },
    @{Generator = 'make_ppc_tsm_probe.py'; App = 'PPC TSM Probe' },
    @{Generator = 'make_ppc_picture_probe.py'; App = 'PPC Picture Probe' },
    @{Generator = 'make_ppc_icon_probe.py'; App = 'PPC Icon Probe' },
    @{Generator = 'make_ppc_carbon_wait_probe.py'; App = 'PPC Carbon Wait Probe' }
    @{Generator = 'make_ppc_carbon_native_probe.py'; App = 'PPC Carbon Native Probe' }
    @{Generator = 'make_ppc_carbon_pattern_probe.py'; App = 'PPC Carbon Pattern Probe' }
    @{Generator = 'make_ppc_carbon_dialog_probe.py'; App = 'PPC Carbon Dialog Probe' }
    @{Generator = 'make_ppc_carbon_invalidation_probe.py'; App = 'PPC Carbon Invalidation Probe' }
    @{Generator = 'make_ppc_carbon_string_probe.py'; App = 'PPC Carbon String Probe' }
    @{Generator = 'make_ppc_carbon_atomic_probe.py'; App = 'PPC Carbon Atomic Probe' }
    @{Generator = 'make_ppc_carbon_records_probe.py'; App = 'PPC Carbon Records Probe' }
    @{Generator = 'make_ppc_carbon_descriptor_probe.py'; App = 'PPC Carbon Descriptor Probe' }
    @{Generator = 'make_ppc_carbon_pascal_probe.py'; App = 'PPC Carbon Pascal Probe' }
    @{Generator = 'make_ppc_stdclib_pascal_probe.py'; App = 'PPC StdCLib Pascal Probe' }
    @{Generator = 'make_ppc_carbon_cf_probe.py'; App = 'PPC Carbon CF Layout Probe' }
    @{Generator = 'make_ppc_corefoundation_layout_probe.py'; App = 'PPC Core CF Layout Probe' }
    @{Generator = 'make_ppc_carbon_date_probe.py'; App = 'PPC Carbon Date Probe' }
    @{Generator = 'make_ppc_corefoundation_date_probe.py'; App = 'PPC Core Date Probe' }
    @{Generator = 'make_ppc_carbon_gworld_probe.py'; App = 'PPC Carbon GWorld Probe' }
    @{Generator = 'make_ppc_carbon_callback_probe.py'; App = 'PPC Carbon Callback Probe' }
    @{Generator = 'make_ppc_carbon_search_probe.py'; App = 'PPC Carbon Search Probe' }
    @{Generator = 'make_ppc_corefoundation_search_probe.py'; App = 'PPC Core Search Probe' }
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe' }
    @{Generator = 'make_ppc_corefoundation_allocator_probe.py'; App = 'PPC Core Allocator Probe' }
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe' }
    @{Generator = 'make_ppc_corefoundation_allocation_probe.py'; App = 'PPC Core Alloc Probe' }
    @{Generator = 'make_ppc_carbon_creation_probe.py'; App = 'PPC Carbon Create Probe' }
    @{Generator = 'make_ppc_corefoundation_creation_probe.py'; App = 'PPC Core Create Probe' }
    @{Generator = 'make_ppc_carbon_ownership_probe.py'; App = 'PPC Carbon Own Probe'; Timeout = 60000 }
    @{Generator = 'make_ppc_corefoundation_ownership_probe.py'; App = 'PPC Core Own Probe'; Timeout = 60000 }
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe' }
    @{Generator = 'make_ppc_corefoundation_array_create_probe.py'; App = 'PPC Core Array New Probe' }
    @{Generator = 'make_ppc_carbon_cf_string_probe.py'; App = 'PPC Carbon CF String Probe' }
    @{Generator = 'make_ppc_corefoundation_string_probe.py'; App = 'PPC Core CF String Probe' }
    @{Generator = 'make_ppc_unichar_probe.py'; App = 'PPC UniChar Probe' }
    @{Generator = 'make_ppc_unichar_foreign_probe.py'; App = 'PPC UniChar Probe' }
    @{Generator = 'make_ppc_carbon_url_resolution_probe.py'; App = 'PPC Carbon URLResolve Probe' }
    @{Generator = 'make_ppc_corefoundation_url_resolution_probe.py'; App = 'PPC Core URLResolve Probe' }
    @{Generator = 'make_ppc_carbon_url_resolution_foreign_probe.py'; App = 'PPC Carbon URLResolve Probe' }
    @{Generator = 'make_ppc_carbon_url_creation_probe.py'; App = 'PPC Carbon URLCreate Probe' }
    @{Generator = 'make_ppc_corefoundation_url_creation_probe.py'; App = 'PPC Core URLCreate Probe' }
    @{Generator = 'make_ppc_carbon_url_creation_foreign_probe.py'; App = 'PPC Carbon URLCreate Probe' }
    @{Generator = 'make_ppc_carbon_url_components_probe.py'; App = 'PPC Carbon URLComp Probe' }
    @{Generator = 'make_ppc_corefoundation_url_components_probe.py'; App = 'PPC Core URLComp Probe' }
    @{Generator = 'make_ppc_carbon_url_components_foreign_probe.py'; App = 'PPC Carbon URLComp Probe' }
    @{Generator = 'make_ppc_carbon_url_probe.py'; App = 'PPC Carbon URL Probe' }
    @{Generator = 'make_ppc_corefoundation_url_probe.py'; App = 'PPC Core URL Probe' }
    @{Generator = 'make_ppc_carbon_url_foreign_probe.py'; App = 'PPC Carbon URL Probe' }
    @{Generator = 'make_ppc_carbon_bundle_lifecycle_probe.py'; App = 'PPC Carbon BLife Probe' }
    @{Generator = 'make_ppc_corefoundation_bundle_lifecycle_probe.py'; App = 'PPC Core BLife Probe' }
    @{Generator = 'make_ppc_carbon_bundle_lifecycle_foreign_probe.py'; App = 'PPC Carbon BLife Probe' }
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe' }
    @{Generator = 'make_ppc_corefoundation_bundle_state_probe.py'; App = 'PPC Core BState Probe' }
    @{Generator = 'make_ppc_carbon_bundle_state_foreign_probe.py'; App = 'PPC Carbon BState Probe' }
    @{Generator = 'make_ppc_carbon_bundle_probe.py'; App = 'PPC Carbon Bundle Probe' }
    @{Generator = 'make_ppc_carbon_bundle_foreign_probe.py'; App = 'PPC Carbon Bundle Probe' }
    @{Generator = 'make_ppc_carbon_cf_pascal_probe.py'; App = 'PPC Carbon CF Pascal Probe' }
    @{Generator = 'make_ppc_corefoundation_cf_pascal_probe.py'; App = 'PPC Core CF Pascal Probe' }
    @{Generator = 'make_ppc_carbon_cf_pascal_foreign_probe.py'; App = 'PPC Carbon CF Pascal Probe' }
    @{Generator = 'make_ppc_carbon_cf_cstring_probe.py'; App = 'PPC Carbon CF CString Probe' }
    @{Generator = 'make_ppc_corefoundation_cf_cstring_probe.py'; App = 'PPC Core CF CString Probe' }
    @{Generator = 'make_ppc_carbon_cf_cstring_foreign_probe.py'; App = 'PPC Carbon CF CString Probe' }
    @{Generator = 'make_ppc_carbon_cf_queries_probe.py'; App = 'PPC Carbon CF Query Probe' }
    @{Generator = 'make_ppc_corefoundation_cf_queries_probe.py'; App = 'PPC Core CF Query Probe' }
    @{Generator = 'make_ppc_carbon_cf_queries_foreign_probe.py'; App = 'PPC Carbon CF Query Probe' }
    @{Generator = 'make_ppc_carbon_cf_encoding_probe.py'; App = 'PPC Carbon CF Encode Probe' }
    @{Generator = 'make_ppc_corefoundation_cf_encoding_probe.py'; App = 'PPC Core CF Encode Probe' }
    @{Generator = 'make_ppc_carbon_cf_encoding_foreign_probe.py'; App = 'PPC Carbon CF Encode Probe' }
    @{Generator = 'make_ppc_carbon_string_pointer_probe.py'; App = 'PPC Carbon String Pointer Probe' }
    @{Generator = 'make_ppc_corefoundation_string_pointer_probe.py'; App = 'PPC Core String Pointer Probe' }
    @{Generator = 'make_ppc_carbon_string_pointer_foreign_probe.py'; App = 'PPC Carbon String Pointer Probe' }
    @{Generator = 'make_ppc_event_probe.py'; App = 'PPC Event Probe' }
)
$batches = [ordered]@{
    'PPC Arithmetic Checks' = @(
        'PPC Decimal Probe', 'PPC FPU Probe', 'PPC Fused Probe', 'PPC FPR Probe'
    )
    'PPC Toolbox Checks' = @(
        'PPC Toolbox Probe', 'PPC Inplace String Probe', 'PPC Dispatch Probe',
        'PPC Handle Copy Probe', 'PPC Layer Probe', 'PPC TSM Probe', 'PPC Volume Probe',
        'PPC AppleEvent Probe', 'PPC Speech Probe', 'PPC Event Probe', 'PPC StdCLib Pascal Probe',
        'PPC Thread Probe', 'PPC Resource Probe', 'PPC Sound Probe', 'PPC Component Probe'
    )
    'PPC Carbon Toolbox Checks' = @(
        'PPC Carbon Wait Probe', 'PPC Carbon Native Probe', 'PPC Carbon Pattern Probe', 'PPC Carbon Dialog Probe',
        'PPC Carbon Invalidation Probe', 'PPC Carbon String Probe', 'PPC Carbon Atomic Probe',
        'PPC Carbon Records Probe', 'PPC Carbon Descriptor Probe', 'PPC Carbon Pascal Probe',
        'PPC Carbon CF Layout Probe', 'PPC Core CF Layout Probe', 'PPC Carbon Date Probe', 'PPC Core Date Probe',
        'PPC Carbon Callback Probe', 'PPC Carbon Search Probe', 'PPC Core Search Probe'
    )
    'PPC Callback Checks' = @(
        'PPC Callback Probe', 'PPC International Callback Probe', 'PPC Grow Zone Callback Probe',
        'PPC Event Queue Callback Probe', 'PPC Line Break Callback Probe', 'PPC CRM Callback Probe',
        'PPC Open Transport Callback Probe'
    )
    'PPC Graphics Checks' = @(
        'PPC Standard Alert Probe', 'PPC Windows Probe', 'PPC Appearance Probe',
        'PPC Picture Probe', 'PPC Icon Probe', 'PPC Popup Probe', 'PPC Carbon GWorld Probe'
    )
}
$batches['PPC Core Checks'] = $batches['PPC Arithmetic Checks'] + $batches['PPC Toolbox Checks'] + $batches['PPC Callback Checks']
$batches['PPC Carbon and Graphics Checks'] = $batches['PPC Carbon Toolbox Checks'] + $batches['PPC Graphics Checks']
foreach ($case in $cases) {
    if (-not $case.Report) {
        $case.Report = if ($case.Generator -like '*_foreign_probe.py') { $case.App + ' (foreign)' } else { $case.App }
    }
}
$batches['PPC Regression Checks'] = $batches['PPC Core Checks'] + $batches['PPC Carbon and Graphics Checks'] +
@('PPC JIT Probe', 'PPC Code Publish Probe')
$defaultBatches = @('PPC Regression Checks')
$batches['PPC Storage and Media Checks'] = @('PPC Process Probe', 'PPC File Probe', 'PPC Fileplay Probe', 'PPC Movie Probe')
$batches['PPC Carbon Profile Checks'] = @('PPC Carbon Fallback Checks', 'PPC Carbon Classic185 Probe')
$batches['PPC CF Allocation Checks'] = @('PPC Carbon Allocator Probe', 'PPC Carbon Alloc Probe', 'PPC Core Alloc Probe')
$batches['PPC Full Regression Checks'] = $batches['PPC Regression Checks'] + $batches['PPC Storage and Media Checks']
# The profile checks supply a different synthetic CoreFoundationLib image.
$fullBatches = @('PPC Full Regression Checks', 'PPC Carbon Profile Checks')
$batches['PPC Fragment Lifecycle Checks'] = @(
    'PPC Closure Probe', 'PPC Private Named Probe', 'PPC Private Disk Probe',
    'PPC Shared Probe'
)
$batches['PPC Guest Memory Checks'] = @('PPC Memory Probe')
$batches['PPC Execution Checks'] = @('PPC JIT Probe', 'PPC Code Publish Probe') + $batches['PPC Guest Memory Checks']
$batches['PPC Extended Regression Checks'] = $batches['PPC Full Regression Checks'] +
$batches['PPC Fragment Lifecycle Checks']
$batches['PPC CFM Metadata Checks'] = @('PPC INIT Probe', 'PPC Memory Probe')
$exhaustiveBatches = @('PPC Extended Regression Checks', 'PPC CFM Metadata Checks',
    'PPC Carbon Profile Checks', 'PPC CF Allocation Checks')
# Each of these pairs uses the same synthetic CoreFoundationLib image.
foreach ($pair in ([ordered]@{
            'String Operations' = 'StrOps'
            'Creation' = 'Create'
            'Ownership' = 'Own'
            'Array Creation' = 'Array New'
            'String Creation' = 'CF String'
            'URL Resolution' = 'URLResolve'
            'URL Creation' = 'URLCreate'
            'URL Components' = 'URLComp'
            'URL Accessors' = 'URL'
            'Bundle Lifecycle' = 'BLife'
            'Bundle State' = 'BState'
            'Pascal Strings' = 'CF Pascal'
            'C Strings' = 'CF CString'
            'Queries' = 'CF Query'
            'Encoding' = 'CF Encode'
            'String Pointers' = 'String Pointer'
        }).GetEnumerator()) {
    $batch = "PPC CF $($pair.Key) Checks"
    $batches[$batch] = @("PPC Carbon $($pair.Value) Probe", "PPC Core $($pair.Value) Probe")
    $exhaustiveBatches += $batch
}
$batches['PPC CF Ownership Array Checks'] = $batches['PPC CF Ownership Checks'] + $batches['PPC CF Array Creation Checks']
$batches['PPC CF String Conversion Checks'] = $batches['PPC CF Pascal Strings Checks'] + $batches['PPC CF C Strings Checks'] + $batches['PPC CF String Pointers Checks']
$batches['PPC Foreign String Checks'] = @('PPC Carbon CF Pascal Probe (foreign)', 'PPC Carbon CF CString Probe (foreign)',
    'PPC Carbon String Pointer Probe (foreign)')
$exhaustiveBatches = @($exhaustiveBatches | Where-Object {
        $_ -notin @('PPC CF Ownership Checks', 'PPC CF Array Creation Checks', 'PPC CF Pascal Strings Checks', 'PPC CF C Strings Checks', 'PPC CF String Pointers Checks')
    }) + @('PPC CF Ownership Array Checks', 'PPC CF String Conversion Checks', 'PPC Foreign String Checks')

$fallbackAliases = @('PPC Carbon Values Foreign Probe', 'PPC Carbon Coll Foreign Probe',
    'PPC Core Values Foreign Probe', 'PPC Core Coll Foreign Probe')
if ($Group) {
    $Group = @($Group | ForEach-Object { if ($_ -in $fallbackAliases) { 'PPC Carbon Fallback Checks' } else { $_ } } | Select-Object -Unique)
}
$selectedBatches = if ($Group) {
    @($Group | Where-Object { $batches.Contains($_) })
}
elseif ($Exhaustive) { $exhaustiveBatches }
elseif ($Full) { $fullBatches }
else { $defaultBatches }
if ($Group) {
    foreach ($name in $Group) {
        if ($name -notin $selectedBatches -and $name -notin $cases.App -and $name -notin $cases.Report) {
            throw "Unknown PPC regression group $name"
        }
    }
    $reports = @($selectedBatches | ForEach-Object { $batches[$_] })
    $selectedCases = @($cases | Where-Object {
            $_.App -in $Group -or $_.Report -in $Group -or $_.Report -in $reports
        })
    $remainingCases = $selectedCases
    $compatibleBatches = if ('PPC Execution Checks' -in $selectedBatches -or
        'PPC Memory Probe' -in $selectedCases.App) {
        @('PPC CFM Metadata Checks', 'PPC Execution Checks') + @($exhaustiveBatches | Where-Object { $_ -ne 'PPC CFM Metadata Checks' })
    }
    else { $exhaustiveBatches }
    $launches = @(foreach ($batch in $compatibleBatches) {
            $members = @($remainingCases | Where-Object {
                    $_.Report -in $batches[$batch]
                })
            if (-not $members.Count) { continue }
            $matchingBatch = @($selectedBatches | Where-Object {
                    $batches[$_].Count -eq $members.Count -and
                    @($batches[$_] | Where-Object { $_ -notin $members.Report }).Count -eq 0
                })
            if ($members.Count -eq 1 -and -not $matchingBatch.Count) { continue }
            $app = if ($matchingBatch.Count) { $matchingBatch[0] }
            elseif ($batch -eq 'PPC Extended Regression Checks') { 'PPC Selected Regression Checks' }
            else { $batch }
            @{ App = $app; Cases = $members }
            $remainingCases = @($remainingCases | Where-Object { $_ -notin $members })
        })
    $launches += $remainingCases
}
else {
    $launches = @(foreach ($batch in $selectedBatches) {
            $reports = $batches[$batch]
            $members = @($cases | Where-Object { $_.Report -in $reports })
            if ($members.Count -ne $reports.Count) { throw "Invalid case list for $batch" }
            @{ App = $batch; Cases = $members }
        })
    if ($Exhaustive) {
        $batched = @($launches | ForEach-Object { $_.Cases })
        $launches += @($cases | Where-Object { $_ -notin $batched })
    }
}
if ($Plan) {
    $launchCount = $launches.Count
    foreach ($launch in $launches) {
        if ($launch.Cases) {
            Write-Output ("{0}: {1} checks" -f $launch.App, $launch.Cases.Count)
        }
        else {
            Write-Output $launch.Report
            if ($Aot -and $launch.App -eq 'PPC Private Memory Probe') { ++$launchCount }
        }
    }
    Write-Output ("Reporting apps: $($launches.Count). ROMAW launches: $launchCount.")
    return
}
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
$scenarioCount = 0
$launchCount = 0
foreach ($launch in $launches) {
    $fixture = Join-Path $workspace ('build\ppc-launch.' + [guid]::NewGuid().ToString('N'))
    $jitOption = if ($Interpreter) { '-no-ppc-jit ' } elseif ($Aot) { '-ppc-aot ' } else { '-ppc-jit ' }
    $profile = $env:USERPROFILE
    try {
        if ($Aot -or $launch.Cases -or $launch.Isolated) {
            New-Item -ItemType Directory -Force (Join-Path $fixture '.romaw/Apps/System Folder') | Out-Null
            [IO.File]::WriteAllText((Join-Path $fixture '.romaw/ROMAW.prefs'), "[HostDrives]`nSidecarDrives=none`n")
            $env:USERPROFILE = $fixture
        }
        if ($launch.Cases) {
            $manifest = Join-Path $fixture 'batch.json'
            $manifestCases = $launch.Cases
            if ($launch.Cases.Count -eq 2 -and 'PPC INIT Probe' -in $launch.Cases.App -and 'PPC Memory Probe' -in $launch.Cases.App) {
                # Application, ProbeStartup and the three native dependency libraries.
                $manifestCases = @($launch.Cases | ForEach-Object {
                        $row = $_.Clone()
                        $row.Arguments = if ($row.App -eq 'PPC INIT Probe') { @('5', '4', '16', $launch.App) } else { @('5') }
                        $row
                    })
            }
            [IO.File]::WriteAllText($manifest, (ConvertTo-Json -InputObject $manifestCases -Depth 4))
            $generatorOutput = & python (Join-Path $PSScriptRoot 'make_ppc_romaw_batch.py') $fixture $launch.App $manifest
        }
        else {
            $generatorArguments = @()
            if ($launch.Arguments) { $generatorArguments = @($launch.Arguments) }
            $generatorOutput = & python (Join-Path $PSScriptRoot $launch.Generator) $fixture @generatorArguments
        }
        if ($Detailed -or $LASTEXITCODE -ne 0) { $generatorOutput }
        if ($LASTEXITCODE -ne 0) { throw "Could not generate $($launch.App) in $fixture" }
    }
    finally { $env:USERPROFILE = $profile }
    $logPath = Join-Path $fixture 'stderr.txt'
    ++$launchCount
    $launchLog = Invoke-ROMAWCheck $launch $fixture $jitOption
    $members = if ($launch.Cases) { $launch.Cases } else { $launch }
    $reports = [regex]::Matches($launchLog, '(?m)^debugstr: (PPC .+?): (running|passed)\r?$')
    if ($launch.Cases -and ($reports.Count -ne 2 * $launch.Cases.Count -or
            -not $launchLog.Contains("$($launch.App): $($launch.Cases.Count) checks passed"))) {
        throw "$($launch.App) produced an incomplete report; inspect $logPath"
    }
    $reportIndex = 0
    foreach ($case in $members) {
        $log = $launchLog
        if ($launch.Cases) {
            $start = $reports[$reportIndex]
            $finish = $reports[$reportIndex + 1]
            if ($start.Groups[1].Value -cne $case.Report -or $start.Groups[2].Value -cne 'running' -or
                $finish.Groups[1].Value -cne $case.Report -or $finish.Groups[2].Value -cne 'passed') {
                throw "$($case.Report) failed to report completion; inspect $logPath"
            }
            $log = $launchLog.Substring($start.Index, $finish.Index - $start.Index)
            $reportIndex += 2
        }
        Test-CheckResult $case $log -LaunchLog $launchLog
        if ($Detailed) { Write-Output ($case.Report + ': passed') }
        ++$scenarioCount
    }
    Write-Output ("{0}: {1} checks passed" -f $launch.App, @($members).Count)

    if ($Aot -and $launch.App -eq 'PPC Private Memory Probe') {
        Copy-Item -LiteralPath $logPath -Destination (Join-Path $fixture 'cold-aot.log')
        ++$launchCount
        $warmLog = Invoke-ROMAWCheck $launch $fixture $jitOption
        Test-CheckResult $launch $warmLog -Warm
        Write-Output 'PPC Private Memory Probe warm AOT: passed'
    }
}
Write-Output ("All $scenarioCount PPC checks passed in $launchCount ROMAW launches across $($launches.Count) apps")
