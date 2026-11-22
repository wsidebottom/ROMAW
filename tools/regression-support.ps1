# +===================================================================
#   File:      regression-support.ps1
#
#   Summary:   Dispatch consolidated regression checks while preserving their named arguments.
#
#   Classes:   None.
#
#   Functions: Invoke-RegressionCheck
#
#   Origin:    Shared dispatcher for the repository's consolidated regression scripts.
#
#   Copyright and Legal notices: Applicable repository license: COPYING.
# ===================================================================+

function Invoke-RegressionCheck {
    param(
        [string[]]$Names,
        [object[]]$CheckArguments
    )
    $selection = if ($CheckArguments.Count) { [string]$CheckArguments[0] } else { '--list' }
    if ($selection -in @('--list', '-List', 'list')) {
        $Names | Write-Output
        return
    }
    if ($selection -notin $Names) { throw "Unknown check '$selection'. Use --list to see the available checks." }
    $command = Get-Command "Invoke-Check-$selection" -CommandType Function -ErrorAction Stop
    $named = @{}
    $positional = [Collections.Generic.List[object]]::new()
    for ($index = 1; $index -lt $CheckArguments.Count; ++$index) {
        $argument = $CheckArguments[$index]
        if ($argument -is [string] -and $argument -match '^-(?<Name>[A-Za-z][A-Za-z0-9-]*)(?::(?<Value>.*))?$') {
            $parameter = $command.Parameters.Values | Where-Object {
                $_.Name -eq $Matches.Name -or $Matches.Name -in $_.Aliases
            } | Select-Object -First 1
            if (-not $parameter) { throw "Unknown parameter '$argument' for '$selection'." }
            if ($parameter.ParameterType -eq [Management.Automation.SwitchParameter]) {
                $named[$parameter.Name] = if ($Matches.ContainsKey('Value') -and $Matches.Value) {
                    [bool]::Parse($Matches.Value.TrimStart('$'))
                }
                else { $true }
            }
            elseif ($Matches.ContainsKey('Value')) {
                $named[$parameter.Name] = $Matches.Value
            }
            else {
                if (++$index -ge $CheckArguments.Count) { throw "Missing value for '$argument'." }
                $named[$parameter.Name] = $CheckArguments[$index]
            }
        }
        else {
            $positional.Add($argument)
        }
    }
    $remaining = $positional.ToArray()
    & $command @named @remaining
}
