# +===================================================================
#   File:      register-image-file-types.ps1
#
#   Summary:   Register ROMAW image icons and mount commands.
#
#   Classes:   ROMAWImageShellNotification.
#
#   Functions: Script entry point.
#
#   Origin:    Executor Windows file association integration.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
param([string]$ROMAW, [string]$IconDirectory, [switch]$AllUsers)
$ErrorActionPreference = 'Stop'
if (-not $ROMAW) {
    $localROMAW = Join-Path $PSScriptRoot 'ROMAW.exe'
    $ROMAW = if (Test-Path -LiteralPath $localROMAW) { $localROMAW } else {
        Join-Path (Split-Path $PSScriptRoot -Parent) 'windows-binaries\bin\ROMAW.exe'
    }
}
$ROMAW = (Resolve-Path -LiteralPath $ROMAW).Path
if (-not $IconDirectory) { $IconDirectory = Join-Path (Split-Path $ROMAW -Parent) 'image-file-icons' }
$IconDirectory = (Resolve-Path -LiteralPath $IconDirectory).Path
$mapping = Get-Content -Raw -LiteralPath (Join-Path $IconDirectory 'mapping.json') | ConvertFrom-Json
$changes = @()
foreach ($type in $mapping) {
    if ($type.key -notmatch '^[a-z]+$') { throw 'Invalid image type identifier' }
    $icon = (Resolve-Path -LiteralPath (Join-Path $IconDirectory $type.icon)).Path
    $program = 'ROMAW.DiskImage.' + $type.key
    $changes += [pscustomobject]@{Key = $program; Name = ''; Value = ('Macintosh ' + $type.key.ToUpperInvariant() + ' disk image') }
    $changes += [pscustomobject]@{Key = ($program + '\DefaultIcon'); Name = ''; Value = ('"' + $icon + '",0') }
    $changes += [pscustomobject]@{Key = ($program + '\shell\open\command'); Name = ''; Value = ('"' + $ROMAW + '" -mount "%1"') }
    $changes += [pscustomobject]@{Key = ($program + '\Application'); Name = 'ApplicationName'; Value = 'ROMAW Disk Images' }
    $changes += [pscustomobject]@{Key = ($program + '\Application'); Name = 'ApplicationIcon'; Value = ('"' + $icon + '",0') }
    foreach ($extension in $type.extensions) {
        if ($extension -notmatch '^\.[a-z]+$') { throw 'Invalid image extension' }
        $changes += [pscustomobject]@{Key = $extension; Name = ''; Value = $program }
        $changes += [pscustomobject]@{Key = ($extension + '\OpenWithProgids'); Name = $program; Value = '' }
        $changes += [pscustomobject]@{Key = 'ROMAW.DiskImages\Capabilities\FileAssociations'; Name = $extension; Value = $program }
        $changes += [pscustomobject]@{Key = 'Applications\ROMAW.exe\SupportedTypes'; Name = $extension; Value = '' }
    }
}
$changes += [pscustomobject]@{Key = 'Applications\ROMAW.exe'; Name = 'FriendlyAppName'; Value = 'ROMAW Disk Images' }
$changes += [pscustomobject]@{Key = 'Applications\ROMAW.exe\shell\open\command'; Name = ''; Value = ('"' + $ROMAW + '" -mount "%1"') }
$changes += [pscustomobject]@{Key = 'ROMAW.DiskImages\Capabilities'; Name = 'ApplicationName'; Value = 'ROMAW Disk Images' }
$changes += [pscustomobject]@{Key = 'ROMAW.DiskImages\Capabilities'; Name = 'ApplicationDescription'; Value = 'Mount Macintosh disk images directly in ROMAW.' }
$changes += [pscustomobject]@{Key = 'ROMAW.DiskImages\Capabilities'; Name = 'ApplicationIcon'; Value = ('"' + (Join-Path $IconDirectory 'hfs.ico') + '",0') }
foreach ($change in $changes) {
    $change.Key = if ($change.Key.StartsWith('ROMAW.DiskImages\Capabilities')) {
        $change.Key.Replace('ROMAW.DiskImages\Capabilities', 'Software\ROMAW\DiskImages\Capabilities')
    }
    else { 'Software\Classes\' + $change.Key }
}
$changes += [pscustomobject]@{Key = 'Software\RegisteredApplications'; Name = 'ROMAW Disk Images'; Value = 'Software\ROMAW\DiskImages\Capabilities' }
# Explorer also keeps a per-user list of eligible handlers. Add the current
# ProgIDs there without changing protected UserChoice values.
foreach ($type in $mapping) {
    foreach ($extension in $type.extensions) {
        $changes += [pscustomobject]@{Key = ('Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $extension + '\OpenWithProgids'); Name = ('ROMAW.DiskImage.' + $type.key); Value = '' }
    }
}
$changes += [pscustomobject]@{Key = 'Software\Microsoft\Windows\CurrentVersion\App Paths\ROMAW.exe'; Name = ''; Value = $ROMAW }
if ($AllUsers) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '-AllUsers requires an administrator token.'
    }
    # Publish one machine-visible app entry, retaining per-user extension values
    # and leaving other users' chosen defaults intact.
    $machine = foreach ($change in $changes) {
        if ($change.Key -match '^Software\\Classes\\(ROMAW\.DiskImage\.|Applications\\ROMAW\.exe)' -or
            $change.Key.StartsWith('Software\ROMAW\DiskImages\Capabilities') -or
            $change.Key -eq 'Software\RegisteredApplications' -or
            $change.Key -eq 'Software\Microsoft\Windows\CurrentVersion\App Paths\ROMAW.exe') {
            [pscustomobject]@{Hive = 'LocalMachine'; Key = $change.Key; Name = $change.Name; Value = $change.Value }
        }
    }
    $changes += $machine
    $changes += [pscustomobject]@{Key = 'Software\RegisteredApplications'; Name = 'ROMAW Disk Images'; Remove = $true }
}
# Preserve the values being changed before touching the per-user class keys.
$backup = foreach ($change in $changes) {
    $hive = if ($change.Hive) { $change.Hive } else { 'CurrentUser' }
    $view = if ($change.View) { $change.View } else { 'Registry64' }
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]$hive, [Microsoft.Win32.RegistryView]$view)
    $key = $base.OpenSubKey($change.Key)
    try {
        $present = $key -and $key.GetValueNames().Contains($change.Name)
        [pscustomobject]@{Hive = $hive; View = $view; Key = $change.Key; Name = $change.Name; Present = [bool]$present
            Kind = $(if ($present) { $key.GetValueKind($change.Name).ToString() } else { $null })
            Value = $(if ($present) { $key.GetValue($change.Name, $null, 'DoNotExpandEnvironmentNames') } else { $null })
        }
    }
    finally { if ($key) { $key.Dispose() }; $base.Dispose() }
}
$backupPath = Join-Path $IconDirectory ('association-backup.' + [guid]::NewGuid().ToString('N') + '.json')
$backup | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 -LiteralPath $backupPath
foreach ($change in $changes) {
    $hive = if ($change.Hive) { $change.Hive } else { 'CurrentUser' }
    $view = if ($change.View) { $change.View } else { 'Registry64' }
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]$hive, [Microsoft.Win32.RegistryView]$view)
    $key = $base.CreateSubKey($change.Key)
    try {
        if ($change.Remove) { $key.DeleteValue($change.Name, $false) }
        else { $key.SetValue($change.Name, $change.Value, [Microsoft.Win32.RegistryValueKind]::String) }
    }
    finally { $key.Dispose(); $base.Dispose() }
}
if (-not ('ROMAWImageShellNotification' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ROMAWImageShellNotification {
    [DllImport("shell32.dll")]
    public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
}
'@
}
[ROMAWImageShellNotification]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Output ('Registered ' + (($mapping | ForEach-Object { $_.extensions }).Count) + ' image extensions. Backup: ' + $backupPath)
# Windows owns protected UserChoice defaults. Report them without modifying hashes.
foreach ($type in $mapping) {
    foreach ($extension in $type.extensions) {
        $choice = Get-ItemProperty -LiteralPath ('HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $extension + '\UserChoice') -ErrorAction SilentlyContinue
        if ($choice -and $choice.ProgId -ne ('ROMAW.DiskImage.' + $type.key)) {
            Write-Output ($extension + ' has a protected Windows default (' + $choice.ProgId + '); ROMAW is registered in Open with.')
        }
    }
}
