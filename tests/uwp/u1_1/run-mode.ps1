# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Oracle run of an unattended probe mode (pkgapi, appdata) on Windows: install the signed package made by package.ps1,
# activate the application View<Mode> -Runs times through the U1.0 launcher, copy each run's output (<mode>-<n>.txt),
# remove the package. The certificate must already be trusted (not done here). The window is not needed.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory,
      [Parameter(Mandatory)][ValidateSet('pkgapi','appdata','storage','resources')][string]$Mode, [int]$Runs = 1, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if ($results.StartsWith($work.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $results -eq $work) { throw 'ResultsDirectory must be outside WorkDirectory.' }
Write-Host "Add-AppxPackage, activate View$Mode $Runs time(s), read the output, Remove-AppxPackage."
if (-not $Execute) { return }
if (Test-Path -LiteralPath $results) { throw 'ResultsDirectory must not already exist.' }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work) { throw 'Work directory does not match package state.' }
if (Get-AppxPackage -Name $state.Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $results | Out-Null
$launcher = Join-Path $PSScriptRoot "..\u1_0\build\$($state.Architecture)\launcher.exe"
$app = 'View' + (Get-Culture).TextInfo.ToTitleCase($Mode)
$installed = $null
try {
    Add-AppxPackage -Path (Join-Path $work 'view.appx') -ErrorAction Stop
    $installed = @(Get-AppxPackage -Name $state.Name)[0]
    $folder = Join-Path $env:LOCALAPPDATA "Packages\$($installed.PackageFamilyName)\AC"
    $file = Join-Path $folder "view-$Mode.txt"
    foreach ($run in 1..$Runs) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
        try { & $launcher ($installed.PackageFamilyName + '!' + $app) $Mode 'unused' $installed.PackageFullName 2>&1 | ForEach-Object { Write-Host $_ } } catch { Write-Host "launcher: $($_.Exception.Message)" }
        $deadline = (Get-Date).AddSeconds(40)
        while ((Get-Date) -lt $deadline -and -not ((Test-Path -LiteralPath $file) -and (Select-String -LiteralPath $file -Pattern '^Uninitialize' -Quiet))) { Start-Sleep -Milliseconds 300 }
        Start-Sleep -Seconds 2
        if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $results "$Mode-$run.txt") } else { Write-Host "run ${run}: no output in $folder" }
    }
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop }
}
