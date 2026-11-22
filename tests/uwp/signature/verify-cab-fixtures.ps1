# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# What Windows says about each cabinet fixture (signtool verify /pa). Usage: verify-cab-fixtures.ps1 -Directory dir
param([Parameter(Mandatory)][string]$Directory)
$tool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
foreach ($f in Get-ChildItem -LiteralPath $Directory -Filter *.cab) {
    $text = & $tool verify /pa $f.FullName 2>&1 | Out-String
    $lines = @($text -split "`r?`n" | Where-Object { $_ -match 'rror|Success|trusted|digest|signature' })
    $f.Name.PadRight(20) + ' ' + ($lines -join ' | ')
}
