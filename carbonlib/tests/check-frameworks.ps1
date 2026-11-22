# +===================================================================
#   File:      check-frameworks.ps1
#
#   Summary:   Run the framework group through the consolidated CoreFoundation regression runner.
#
#   Classes:   None.
#
#   Functions: Script entry point.
#
#   Origin:    Original consolidated runner for the generated framework fixture.
#
#   Copyright and Legal notices: See COPYING and synppc/COPYING.
# ===================================================================+
param(
    [string]$ROMAW,
    [switch]$ReferenceOnly
)
& (Join-Path $PSScriptRoot 'check-corefoundation.ps1') -ROMAW $ROMAW -Group Frameworks -ReferenceOnly:$ReferenceOnly
