# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Oracle run of the U1.1 view probe on Windows: install the signed UWP package made by package.ps1, activate its two
# applications (View: multithreaded apartment, ViewSta: single-threaded) through the U1.0 launcher, collect the
# probe's output from the package's own folder, remove the package. The certificate must already be trusted (not
# done here).
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if ($results.StartsWith($work.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $results -eq $work) { throw 'ResultsDirectory must be outside WorkDirectory.' }
Write-Host 'Add-AppxPackage, ActivateApplication (launcher.exe) for each application, read the output, Remove-AppxPackage.'
if (-not $Execute) { return }
if (Test-Path -LiteralPath $results) { throw 'ResultsDirectory must not already exist.' }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work) { throw 'Work directory does not match package state.' }
if (Get-AppxPackage -Name $state.Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $results | Out-Null
$launcher = Join-Path $PSScriptRoot "..\u1_0\build\$($state.Architecture)\launcher.exe"
$installed = $null
try {
    Add-AppxPackage -Path (Join-Path $work 'view.appx') -ErrorAction Stop
    $matches = @(Get-AppxPackage -Name $state.Name)
    if ($matches.Count -ne 1) { throw 'Cannot identify the installed package uniquely.' }
    $installed = $matches[0]
    if ($installed.Publisher -ne $state.Publisher -or $installed.Version.ToString() -ne $state.Version -or $installed.Architecture.ToString() -ne $state.Architecture) { throw 'Installed identity does not match the requested identity.' }
    # the application's own folder: %LOCALAPPDATA% of the process (the AC folder of the package)
    $folder = Join-Path $env:LOCALAPPDATA "Packages\$($installed.PackageFamilyName)\AC"
    foreach ($mode in @(@('View','positive','expected-positive.txt'), @('ViewReturn','return','expected-return.txt'), @('ViewSta','sta','expected-sta.txt'))) {
        $file = Join-Path $folder "view-$($mode[1]).txt"
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        try { & $launcher ($installed.PackageFamilyName + '!' + $mode[0]) $mode[1] 'unused' $installed.PackageFullName 2>&1 | ForEach-Object { Write-Host $_ } } catch { Write-Host "$($mode[1]): $($_.Exception.Message)" }
        "$($mode[1]) launcher exit $LASTEXITCODE" | Add-Content -LiteralPath (Join-Path $results 'exit.txt') -Encoding ASCII
        Start-Sleep -Seconds 2
        if (-not (Test-Path -LiteralPath $file)) { Write-Host "$($mode[1]): no output in $folder"; continue }
        Copy-Item -LiteralPath $file -Destination (Join-Path $results "$($mode[1]).txt")
        $checks = @(Get-Content -LiteralPath $file | Where-Object { $_ -match '^(PASS|FAIL)' } | Sort-Object)
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $mode[2]))) { Write-Host "$($mode[1]): collected (no $($mode[2]) yet)"; continue }
        $expected = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot $mode[2]) | Where-Object { $_ -notmatch '^#' -and $_ -ne '' } | Sort-Object)
        $diff = @(Compare-Object -ReferenceObject $expected -DifferenceObject $checks)
        if ($diff.Count) { $diff | Out-String | Set-Content -LiteralPath (Join-Path $results "$($mode[1])-diff.txt"); Write-Host "$($mode[1]): differs from $($mode[2])" }
        else { Write-Host "$($mode[1]): all $($expected.Count) checks match $($mode[2])" }
    }
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop }
}
