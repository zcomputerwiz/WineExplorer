# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if ($results.StartsWith($work.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $results -eq $work) { throw 'ResultsDirectory must be outside WorkDirectory.' }
Write-Host 'For each variant: Add-AppxPackage, ActivateApplication, wait for exit and read probe output, then Remove-AppxPackage.'
Write-Host 'Run the unpackaged negative directly, record each observed HRESULT, and compare checks with expected.txt.'
if (-not $Execute) { return }
if (Test-Path -LiteralPath $results) { throw 'ResultsDirectory must not already exist.' }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work) { throw 'Work directory does not match package state.' }
if (Get-AppxPackage -Name $state.Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $results | Out-Null
$launcher = Join-Path $PSScriptRoot "build\$($state.Architecture)\launcher.exe"
$probe = Join-Path $PSScriptRoot "build\$($state.Architecture)\probe.exe"
$checks = [Collections.Generic.List[string]]::new()
$observed = [Collections.Generic.List[string]]::new()
try {
    foreach ($case in @('positive','missing_class','missing_dll','unpackaged')) {
        $output = Join-Path $results "$case.txt"
        $installed = $null
        try {
            if ($case -ne 'unpackaged') {
                if (Get-AppxPackage -Name $state.Name) { throw 'Package name became occupied.' }
                Add-AppxPackage -Path (Join-Path $work "$case.appx") -ErrorAction Stop
                $matches = @(Get-AppxPackage -Name $state.Name)
                if ($matches.Count -ne 1) { throw 'Cannot identify installed package uniquely.' }
                $candidate = $matches[0]
                if ($candidate.Publisher -ne $state.Publisher -or $candidate.Version.ToString() -ne $state.Version -or $candidate.Architecture.ToString() -ne $state.Architecture) { throw 'Installed identity does not match the requested identity.' }
                $installed = $candidate
                & $launcher ($installed.PackageFamilyName + '!Probe') $case $output $installed.PackageFullName
            } else {
                & $probe $case $output 'unpackaged'
            }
            $exit = $LASTEXITCODE
            if (-not (Test-Path -LiteralPath $output)) { throw "$case did not produce output (exit $exit)." }
            $lines = @(Get-Content -LiteralPath $output)
            if ($case -eq 'positive') { foreach ($line in $lines) { $checks.Add($line) } }
            else {
                $hr = @($lines | Where-Object { $_ -match "^HRESULT $case 0x[0-9A-F]{8}$" })
                if ($hr.Count -eq 1) { $observed.Add($hr[0]); $observed | Set-Content -LiteralPath (Join-Path $results 'observed-hresults.txt') -Encoding ASCII }
                if ($hr.Count -eq 1 -and @($lines | Where-Object { $_ -eq 'PASS negative_activation' }).Count -eq 1 -and $lines.Count -eq 2 -and $exit -eq 0) { $checks.Add("PASS $case") }
                else { $checks.Add("FAIL $case") }
            }
            if ($exit -gt 1) { throw "$case exited with $exit." }
        } finally {
            if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop }
        }
    }
    $checks | Set-Content -LiteralPath (Join-Path $results 'actual.txt') -Encoding ASCII
    $observed | Set-Content -LiteralPath (Join-Path $results 'observed-hresults.txt') -Encoding ASCII
    $expected = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'expected.txt') | Where-Object { $_ -notmatch '^#' -and $_ -ne '' })
    $actual = @($checks)
    $diff = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual -SyncWindow 0)
    if ($diff.Count) { $diff | Out-String | Set-Content -LiteralPath (Join-Path $results 'diff.txt'); throw 'Output differs from expected.txt.' }
    Write-Host "PASS: $results"
} finally {
    & (Join-Path $PSScriptRoot 'cleanup.ps1') -WorkDirectory $work -Execute
    Write-Host 'Remove any certificate trust you added separately.'
}
