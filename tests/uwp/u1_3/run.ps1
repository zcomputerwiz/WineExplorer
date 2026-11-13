# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Oracle run of the U1.3 dependency fixture on Windows: the dependent package is refused without its framework, both are
# installed, the dependent program is activated through the U1.0 launcher and its output compared with expected.txt,
# the framework cannot be removed while the dependent package needs it, then both are removed.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if ($results.StartsWith($work.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $results -eq $work) { throw 'ResultsDirectory must be outside WorkDirectory.' }
Write-Host 'Add-AppxPackage app (refused), framework, app; activate; Remove-AppxPackage framework (refused), app, framework.'
if (-not $Execute) { return }
if (Test-Path -LiteralPath $results) { throw 'ResultsDirectory must not already exist.' }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work) { throw 'Work directory does not match package state.' }
if ((Get-AppxPackage -Name $state.Name) -or (Get-AppxPackage -Name $state.FrameworkName)) { throw 'A package of this fixture is already installed.' }
New-Item -ItemType Directory -Path $results | Out-Null
$launcher = Join-Path $PSScriptRoot "..\u1_0\build\$($state.Architecture)\launcher.exe"
$notes = [Collections.Generic.List[string]]::new()
$framework = $null; $app = $null
try {
    # 1. the dependent package without its framework
    try { Add-AppxPackage -Path (Join-Path $work 'app.appx') -ErrorAction Stop; $notes.Add('app without framework: INSTALLED') }
    catch { $notes.Add('app without framework: refused: ' + ($_.Exception.Message -split "`n")[0]) }
    if (Get-AppxPackage -Name $state.Name) { Remove-AppxPackage -Package (Get-AppxPackage -Name $state.Name).PackageFullName }
    # 2. both
    Add-AppxPackage -Path (Join-Path $work 'framework.appx') -ErrorAction Stop
    $framework = @(Get-AppxPackage -Name $state.FrameworkName)[0]
    $notes.Add("framework installed: $($framework.PackageFullName) IsFramework=$($framework.IsFramework)")
    Add-AppxPackage -Path (Join-Path $work 'app.appx') -ErrorAction Stop
    $app = @(Get-AppxPackage -Name $state.Name)[0]
    $notes.Add("app installed: $($app.PackageFullName); dependencies: $(@($app.Dependencies | ForEach-Object { $_.PackageFullName }) -join ', ')")
    # 3. activate
    $output = Join-Path $results 'dep.txt'
    & $launcher ($app.PackageFamilyName + '!Dep') positive $output $app.PackageFullName
    Start-Sleep -Seconds 2
    if (-not (Test-Path -LiteralPath $output)) { throw 'The dependent program produced no output.' }
    $checks = @(Get-Content -LiteralPath $output | Where-Object { $_ -match '^(PASS|FAIL)' } | Sort-Object)
    $expected = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'expected.txt') | Where-Object { $_ -notmatch '^#' -and $_ -ne '' } | Sort-Object)
    $diff = @(Compare-Object -ReferenceObject $expected -DifferenceObject $checks)
    if ($diff.Count) { $diff | Out-String | Set-Content -LiteralPath (Join-Path $results 'diff.txt'); $notes.Add('output DIFFERS from expected.txt') } else { $notes.Add("output matches expected.txt ($($expected.Count) checks)") }
    # 4. the framework is in use
    try { Remove-AppxPackage -Package $framework.PackageFullName -ErrorAction Stop; $notes.Add('framework removed while in use: ALLOWED'); $framework = $null }
    catch { $notes.Add('framework removal while in use: refused: ' + ($_.Exception.Message -split "`n")[0]) }
} finally {
    if ($app) { Remove-AppxPackage -Package $app.PackageFullName }
    if ($framework) { Remove-AppxPackage -Package $framework.PackageFullName }
    $notes | Set-Content -LiteralPath (Join-Path $results 'notes.txt')
}
$notes | ForEach-Object { Write-Host $_ }
