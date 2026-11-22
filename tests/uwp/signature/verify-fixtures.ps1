# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# What signtool (WinVerifyTrust with the generic action) says about every package of a fixture directory made by
# make-fixtures.ps1. Usage: verify-fixtures.ps1 -Directory dir
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Directory)
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$dir = [IO.Path]::GetFullPath($Directory)
$report = @()
foreach ($file in Get-ChildItem -LiteralPath $dir -Filter *.appx | Sort-Object Name) {
    $log = Join-Path $env:TEMP 'signtool-verify.txt'
    $process = Start-Process -FilePath "$tools\signtool.exe" -ArgumentList @('verify', '/pa', '/v', "`"$($file.FullName)`"") `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    $text = (Get-Content -LiteralPath $log -Raw) + (Get-Content -LiteralPath "$log.err" -Raw)
    $report += "== $($file.Name): exit $($process.ExitCode)"
    $report += ($text -split "`r?`n" | Where-Object { $_ -match 'Error|Successfully|Issued to|No signature|0x8' } | ForEach-Object { '   ' + $_.Trim() } | Select-Object -First 5)
}
$report | Set-Content -LiteralPath (Join-Path $dir 'signtool-report.txt') -Encoding UTF8
$report
