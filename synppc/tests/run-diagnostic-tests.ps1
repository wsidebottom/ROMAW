# +===================================================================
#   File:      run-diagnostic-tests.ps1
#
#   Summary:   Implements run diagnostic tests support for synppc PowerPC execution and fragment loading.
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

[CmdletBinding(DefaultParameterSetName = 'Application')]
param(
    [string]$ROMAW,
    [string]$Gdb = 'C:\msys64\mingw32\bin\gdb.exe',
    [switch]$Jit,
    [switch]$Both,
    [Parameter(ParameterSetName = 'Application')]
    [string]$Application = 'PPC OSDispatch Probe',
    [Parameter(ParameterSetName = 'Application')]
    [string[]]$Case,
    [Parameter(Mandatory, ParameterSetName = 'Startup')]
    [switch]$Startup,
    [Parameter(Mandatory, ParameterSetName = 'All')]
    [switch]$All,
    [switch]$Plan,
    [switch]$Detailed
)
$ErrorActionPreference = 'Stop'
function Read-ProbeLog([string]$Path) {
    # GDB can report process exit just before Windows releases the CRT log.
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        try { return [IO.File]::ReadAllText($Path) }
        catch [IO.IOException] {
            if ($attempt -eq 99) { throw }
            Start-Sleep -Milliseconds 25
        }
    }
}
$workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $ROMAW) { $ROMAW = Join-Path $workspace 'windows-binaries\bin\ROMAW.exe' }
if ($Jit -and $Both) { throw 'Choose either -Jit or -Both' }
$engines = if ($Both) { '-no-ppc-jit', '-ppc-jit' } elseif ($Jit) { '-ppc-jit' } else { '-no-ppc-jit' }
$diagnostics = @(
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'class-extent';
        Expected = 'Native Carbon runtime class table extent exceeded'
    },
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'hash-capacity';
        Expected = 'Native Carbon hash insertion exceeds fixed capacity'
    },
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'storage-zero-size';
        Expected = 'Native Carbon CFStorage requires a positive value size'
    },
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'uuid-output';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'tree-parent';
        Expected = 'Native Carbon tree append requires an unparented child'
    },
    @{Generator = 'make_ppc_carbon_cf_collections_probe.py'; App = 'PPC Collections Guard Probe'; Mode = 'hash-extent';
        Expected = 'Native Carbon collection extent exceeds signed 32-bit storage'
    },
    @{Generator = 'make_ppc_inplace_string_probe.py'; App = 'PPC Inplace String Probe'; Mode = 'bad-pascal';
        Expected = 'Invalid InterfaceLib string buffer for p2cstr'
    },
    @{Generator = 'make_ppc_inplace_string_probe.py'; App = 'PPC Inplace String Probe'; Mode = 'bad-c';
        Expected = 'Invalid InterfaceLib string buffer for c2pstr'
    },
    @{Generator = 'make_ppc_carbon_cf_values_probe.py'; App = 'PPC Carbon Values Guard Probe'; Mode = 'bad-data-span';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_cf_values_probe.py'; App = 'PPC Carbon Values Guard Probe'; Mode = 'bad-array-capacity';
        Expected = 'Native Carbon CFArray replacement exceeds fixed capacity'
    },
    @{Generator = 'make_ppc_carbon_cf_values_probe.py'; App = 'PPC Carbon Values Guard Probe'; Mode = 'case-expansion';
        Expected = 'Native Carbon Unicode case expansion exceeds the original string extent'
    },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe'; Mode = 'bad-handle';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe'; Mode = 'bad-master';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe'; Mode = 'bad-output';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe'; Mode = 'bad-copy';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_classic_access_probe.py'; App = 'PPC Carbon Classic185 Probe'; Mode = 'bad-index';
        Expected = 'Invalid native Carbon guest memory'
    },
    @{Generator = 'make_ppc_carbon_cfstrings_probe.py'; App = 'PPC Carbon StrOps Probe'; Mode = 'oversize';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_url_resolution_probe.py'; App = 'PPC Carbon URLResolve Probe'; Mode = 'encoder-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLResolutionFailure'
    },
    @{Generator = 'make_ppc_carbon_url_resolution_probe.py'; App = 'PPC Carbon URLResolve Probe'; Mode = 'merge-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLResolutionFailure'
    },
    @{Generator = 'make_ppc_carbon_url_resolution_probe.py'; App = 'PPC Carbon URLResolve Probe'; Mode = 'path-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLResolutionFailure'
    },
    @{Generator = 'make_ppc_carbon_url_creation_probe.py'; App = 'PPC Carbon URLCreate Probe'; Mode = 'allocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLCreationFailure'
    },
    @{Generator = 'make_ppc_carbon_url_creation_probe.py'; App = 'PPC Carbon URLCreate Probe'; Mode = 'validate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLCreationFailure'
    },
    @{Generator = 'make_ppc_carbon_url_creation_probe.py'; App = 'PPC Carbon URLCreate Probe'; Mode = 'reencode-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLCreationFailure'
    },
    @{Generator = 'make_ppc_carbon_url_components_probe.py'; App = 'PPC Carbon URLComp Probe'; Mode = 'capability-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLComponentFailure'
    },
    @{Generator = 'make_ppc_carbon_url_components_probe.py'; App = 'PPC Carbon URLComp Probe'; Mode = 'bytes-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLComponentFailure'
    },
    @{Generator = 'make_ppc_carbon_url_probe.py'; App = 'PPC Carbon URL Probe'; Mode = 'parse-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLFailure'
    },
    @{Generator = 'make_ppc_carbon_url_probe.py'; App = 'PPC Carbon URL Probe'; Mode = 'convert-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLFailure'
    },
    @{Generator = 'make_ppc_carbon_url_probe.py'; App = 'PPC Carbon URL Probe'; Mode = 'insert-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWURLFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_lifecycle_probe.py'; App = 'PPC Carbon BLife Probe'; Mode = 'create-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleLifecycleFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_lifecycle_probe.py'; App = 'PPC Carbon BLife Probe'; Mode = 'unload-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleLifecycleFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_lifecycle_probe.py'; App = 'PPC Carbon BLife Probe'; Mode = 'metadata-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleLifecycleFailure'
    },
    @{Generator = 'make_ppc_private_named_probe.py'; App = 'PPC Private Named Probe'; Mode = 'init-error';
        Expected = 'PowerPC library PrivateModule initialization failed \(-50\)'
    },
    @{Generator = 'make_ppc_private_named_probe.py'; App = 'PPC Private Named Probe'; Mode = 'term-error';
        Expected = 'unsupported API: InterfaceLib:ROMAWPrivateTermFailure'
    },
    @{Generator = 'make_ppc_private_disk_probe.py'; App = 'PPC Private Disk Probe'; Mode = 'init-error';
        Expected = 'PowerPC library PrivateModule initialization failed \(-50\)'
    },
    @{Generator = 'make_ppc_private_disk_probe.py'; App = 'PPC Private Disk Probe'; Mode = 'term-error';
        Expected = 'unsupported API: InterfaceLib:ROMAWPrivateTermFailure'
    },
    @{Generator = 'make_ppc_private_memory_probe.py'; App = 'PPC Private Memory Probe'; Mode = 'init-error';
        Expected = 'PowerPC library PrivateModule initialization failed \(-50\)'
    },
    @{Generator = 'make_ppc_private_memory_probe.py'; App = 'PPC Private Memory Probe'; Mode = 'term-error';
        Expected = 'unsupported API: InterfaceLib:ROMAWPrivateTermFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'info-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateInfoFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'fragment-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateFragmentFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'cleanup-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateCleanupFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'retain-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateRetainFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'executable-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateExecutableFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_state_probe.py'; App = 'PPC Carbon BState Probe'; Mode = 'log-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleStateLogFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_queries_probe.py'; App = 'PPC Carbon CF Query Probe'; Mode = 'lookup-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFQueryLookupFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_queries_probe.py'; App = 'PPC Carbon CF Query Probe'; Mode = 'adapter-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFQueryAdapterFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_queries_probe.py'; App = 'PPC Carbon CF Query Probe'; Mode = 'dictionary-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFQueryDictionaryFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_encoding_probe.py'; App = 'PPC Carbon CF Encode Probe'; Mode = 'encoding-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFEncodingFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_encoding_probe.py'; App = 'PPC Carbon CF Encode Probe'; Mode = 'engine-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFByteEngineFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_cstring_probe.py'; App = 'PPC Carbon CF CString Probe'; Mode = 'encoding-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFPascalFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_cstring_probe.py'; App = 'PPC Carbon CF CString Probe'; Mode = 'encoder-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFPascalFailure'
    },
    @{Generator = 'make_ppc_carbon_string_pointer_probe.py'; App = 'PPC Carbon String Pointer Probe'; Mode = 'init-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFBorrowFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_pascal_probe.py'; App = 'PPC Carbon CF Pascal Probe'; Mode = 'utf8-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFUTF8Failure'
    },
    @{Generator = 'make_ppc_carbon_cf_pascal_probe.py'; App = 'PPC Carbon CF Pascal Probe'; Mode = 'nonlossy-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFNonLossyFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_pascal_probe.py'; App = 'PPC Carbon CF Pascal Probe'; Mode = 'encoding-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFPascalFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_pascal_probe.py'; App = 'PPC Carbon CF Pascal Probe'; Mode = 'encoder-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCFPascalFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_probe.py'; App = 'PPC Carbon Bundle Probe'; Mode = 'loader-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleFailure'
    },
    @{Generator = 'make_ppc_carbon_bundle_probe.py'; App = 'PPC Carbon Bundle Probe'; Mode = 'converter-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWBundleFailure'
    },
    @{Generator = 'make_ppc_unichar_probe.py'; App = 'PPC UniChar Probe'; Mode = 'wrong-library';
        Expected = 'unsupported API: CarbonLib:CFUniCharIsMemberOf'
    },
    @{Generator = 'make_ppc_unichar_probe.py'; App = 'PPC UniChar Probe'; Mode = 'foreign-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWUniCharFailure'
    },
    @{Generator = 'make_ppc_carbon_cf_string_probe.py'; App = 'PPC Carbon CF String Probe'; Mode = 'index-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_cf_string_probe.py'; App = 'PPC Carbon CF String Probe'; Mode = 'copy-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_cf_string_probe.py'; App = 'PPC Carbon CF String Probe'; Mode = 'converter-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonStringFailure'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'retain-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'allocate-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'init-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'retain-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonArrayCreateFailure'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'allocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonArrayCreateFailure'
    },
    @{Generator = 'make_ppc_carbon_array_create_probe.py'; App = 'PPC Carbon Array New Probe'; Mode = 'init-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonArrayCreateFailure'
    },
    @{Generator = 'make_ppc_carbon_ownership_probe.py'; App = 'PPC Carbon Own Probe'; Mode = 'finalize-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_ownership_probe.py'; App = 'PPC Carbon Own Probe'; Mode = 'free-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_ownership_probe.py'; App = 'PPC Carbon Own Probe'; Mode = 'finalize-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonOwnershipFailure'
    },
    @{Generator = 'make_ppc_carbon_ownership_probe.py'; App = 'PPC Carbon Own Probe'; Mode = 'free-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonOwnershipFailure'
    },
    @{Generator = 'make_ppc_carbon_creation_probe.py'; App = 'PPC Carbon Create Probe'; Mode = 'retain-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_creation_probe.py'; App = 'PPC Carbon Create Probe'; Mode = 'allocate-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_creation_probe.py'; App = 'PPC Carbon Create Probe'; Mode = 'retain-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonCreationFailure'
    },
    @{Generator = 'make_ppc_carbon_creation_probe.py'; App = 'PPC Carbon Create Probe'; Mode = 'allocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonCreationFailure'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'allocate-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'reallocate-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'deallocate-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'allocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonAllocationFailure'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'reallocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonAllocationFailure'
    },
    @{Generator = 'make_ppc_carbon_allocation_probe.py'; App = 'PPC Carbon Alloc Probe'; Mode = 'deallocate-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonAllocationFailure'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'preferred-invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'preferred-failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonAllocatorFailure'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'missing';
        Expected = 'Native Carbon requires a real reference function: CoreFoundationLib:CFAllocatorGetDefault'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'wrong-class';
        Expected = 'Native Carbon requires a real reference function: CoreFoundationLib:CFAllocatorGetDefault'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'data-code';
        Expected = 'Native Carbon requires a real reference function: CoreFoundationLib:CFAllocatorGetDefault'
    },
    @{Generator = 'make_ppc_carbon_allocator_probe.py'; App = 'PPC Carbon Allocator Probe'; Mode = 'unaligned';
        Expected = 'Native Carbon requires a real reference function: CoreFoundationLib:CFAllocatorGetDefault'
    },
    @{Generator = 'make_ppc_carbon_search_failure_probe.py'; App = 'PPC Carbon Search Failure'; Mode = 'invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_search_failure_probe.py'; App = 'PPC Carbon Search Failure'; Mode = 'failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonSearchFailure'
    },
    @{Generator = 'make_ppc_carbon_callback_probe.py'; App = 'PPC Carbon Callback Probe'; Mode = 'invalid';
        Expected = 'Invalid native Carbon PowerPC callback'
    },
    @{Generator = 'make_ppc_carbon_callback_probe.py'; App = 'PPC Carbon Callback Probe'; Mode = 'failure';
        Expected = 'unsupported API: InterfaceLib:ROMAWCarbonCallbackFailure'
    },
    @{Generator = 'make_ppc_service_failure_probe.py'; App = 'PPC Service Failure Probe'; Mode = 'pointer';
        Expected = 'unhandled PowerPC host service at StdCLib:memcpy'
    },
    @{Generator = 'make_ppc_service_failure_probe.py'; App = 'PPC Service Failure Probe'; Mode = 'service-id';
        Expected = 'unhandled PowerPC host service \(service 65535/'
    },
    @{Generator = 'make_ppc_resource_probe.py'; App = 'PPC Resource Probe'; Mode = 'truncated';
        Expected = 'truncated PEF extent'
    },
    @{Generator = 'make_ppc_callback_failure_probe.py'; App = 'PPC Callback Failure Probe'; Mode = 'valid';
        Expected = 'unsupported API: InterfaceLib:ROMAWCallbackFailure'
    },
    @{Generator = 'make_ppc_callback_failure_probe.py'; App = 'PPC Callback Failure Probe'; Mode = 'event-avail';
        Expected = 'unsupported API: InterfaceLib:ROMAWCallbackFailure'
    },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Mode = 'invalid-stack';
        Expected = 'Invalid PowerPC callback stack 0x00000000'
    },
    @{Generator = 'make_ppc_callback_probe.py'; App = 'PPC Callback Probe'; Mode = 'recursive';
        Expected = 'PowerPC callback nesting limit exceeded'
    },
    @{Generator = 'make_ppc_osdispatch_probe.py'; App = 'PPC TSM Probe'; Mode = 'tsm';
        Expected = 'unsupported TextServicesDispatch selector 0x7ffe \(ProcInfo 0x00000398, caller 0x[0-9a-f]{8}\)'
    },
    @{Generator = 'make_ppc_osdispatch_probe.py'; App = 'PPC OSDispatch Probe'; Mode = 'valid';
        Expected = 'unsupported OSDispatch selector 0x7ffe \(ProcInfo 0x03f79fbe, caller 0x[0-9a-f]{8}\)'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'missing';
        Expected = 'No PowerPC routine for Mixed Mode selector 0x0000005a'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'signature';
        Expected = 'Unsupported or invalid dispatched PowerPC routine descriptor'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'relative';
        Expected = 'Unsupported or invalid dispatched PowerPC routine descriptor'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'unprepared';
        Expected = 'Unsupported or invalid dispatched PowerPC routine descriptor'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'count';
        Expected = 'Unsupported or invalid dispatched PowerPC routine descriptor'
    },
    @{Generator = 'make_ppc_dispatch_probe.py'; App = 'PPC Dispatch Probe'; Mode = 'indexed';
        Expected = 'Unsupported or invalid dispatched PowerPC routine descriptor'
    }
)
$selected = if ($All) { $diagnostics }
elseif ($Startup) {
    # Exercise distinct startup failures before broadening to individual API families.
    $startupCases = @{
        'PPC OSDispatch Probe' = 'valid'
        'PPC TSM Probe' = 'tsm'
        'PPC Private Named Probe' = 'init-error'
        'PPC Resource Probe' = 'truncated'
    }
    @($diagnostics | Where-Object { $startupCases[$_.App] -eq $_.Mode })
}
else { @($diagnostics | Where-Object { $_.App -eq $Application }) }
if (-not $selected) { throw "Unknown diagnostic application: $Application" }
if ($Case) {
    $unknown = @($Case | Where-Object { $_ -notin $selected.Mode })
    if ($unknown.Count) { throw "Unknown case for $Application ($($unknown -join ', ')). Choose $($selected.Mode -join ', ')." }
    $selected = @($selected | Where-Object { $_.Mode -in $Case })
}
$plannedLaunches = $selected.Count * @($engines).Count
if ($Plan) {
    foreach ($diagnostic in $selected) {
        Write-Output ("{0}: {1}" -f $diagnostic.App, $diagnostic.Mode)
    }
    Write-Output ("Diagnostic cases: $($selected.Count). ROMAW launches: $plannedLaunches ($($engines -join ', ')).")
    return
}
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
$Gdb = (Resolve-Path -LiteralPath $Gdb).Path
$fixtures = 0
$checks = 0
$summary = [ordered]@{}
foreach ($diagnostic in $selected) {
    $fixture = Join-Path $workspace ('build\ppc-diagnostic.' + [guid]::NewGuid().ToString('N'))
    $generation = & python (Join-Path $PSScriptRoot $diagnostic.Generator) $fixture $diagnostic.Mode
    if ($Detailed -or $LASTEXITCODE -ne 0) { $generation | Write-Output }
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate diagnostic probe' }
    $app = (Join-Path $fixture $diagnostic.App).Replace('\', '/')
    ++$fixtures
    foreach ($engine in $engines) {
        $logs = $fixture
        if ($Both) {
            $logs = Join-Path $fixture $engine.TrimStart('-')
            $null = New-Item -ItemType Directory -Path $logs
        }
        $script = Join-Path $logs 'diagnostic.gdb'
        # Suppress the expected native message box so this regression runs unattended.
        $commands = @'
set pagination off
set confirm off
break ROMlib_win_ppc_error
commands
silent
printf "Expected startup diagnostic: %s\n", message
return
continue
end
'@ + "`nrun $engine -syszone 8m -applzone 64m -sdlaudio test -debug trace `"$app`"`n"
        [IO.File]::WriteAllText($script, $commands, [Text.UTF8Encoding]::new($false))
        $stdout = Join-Path $logs 'gdb.txt'
        $process = Start-Process -FilePath $Gdb -ArgumentList ('-batch -x "' + $script + '" "' + $ROMAW + '"') `
            -WorkingDirectory $logs -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $logs 'gdb-error.txt')
        $null = $process.Handle
        try {
            if (-not $process.WaitForExit(15000)) { throw "Diagnostic probe timed out; inspect $logs" }
            $output = Read-ProbeLog $stdout
            if ($process.ExitCode -ne 0 -or $output -notmatch 'exited with code 02') {
                throw "$($diagnostic.App) $($diagnostic.Mode) did not exit with status 2 under GDB. Inspect $logs"
            }
            $log = Read-ProbeLog (Join-Path $logs 'stderr.txt')
            if ($output -notmatch $diagnostic.Expected -or $log -match 'win_exception|Illegal instruction') {
                throw "$($diagnostic.App) $($diagnostic.Mode) emitted an unexpected diagnostic. Inspect $logs"
            }
            if ($diagnostic.App -eq 'PPC OSDispatch Probe' -and
                $log -notmatch 'arguments=9 00000011 00000022 00000033 00000044 00000055 00000066 00000077 00000088 00000099') {
                throw "Incorrect decoded OSDispatch arguments; inspect $logs"
            }
            ++$checks
            if (-not $summary.Contains($diagnostic.App)) { $summary[$diagnostic.App] = 0 }
            ++$summary[$diagnostic.App]
            if ($Detailed) {
                Write-Output ($engine + ' ' + $diagnostic.App + ' ' + $diagnostic.Mode + ': controlled exit 2, expected diagnostic and no Windows exception')
            }
        }
        finally {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            $process.Dispose()
        }
    }
}
if (-not $Detailed) {
    foreach ($entry in $summary.GetEnumerator()) {
        Write-Output ("{0}: {1} controlled exits passed" -f $entry.Key, $entry.Value)
    }
}
Write-Output ("Diagnostic checks passed: $checks. Application fixtures generated: $fixtures.")
