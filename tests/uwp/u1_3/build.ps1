# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds the U1.3 fixture (build\<arch>\u1fw.dll and depapp.exe).
[CmdletBinding()]
param(
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$out = Join-Path $PSScriptRoot "build\$Architecture"
$commands = @(
    "cl /nologo /EHsc /MT /LD `"$PSScriptRoot\fwdll.cpp`" /link /OUT:u1fw.dll",
    "cl /nologo /std:c++17 /EHsc /MT `"$PSScriptRoot\depapp.cpp`" /link /OUT:depapp.exe windowsapp.lib"
)
Write-Host "call `"$vs`" $Architecture 10.0.26100.0"
$commands | ForEach-Object { Write-Host $_ }
if (-not $Execute) { return }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $out
try {
    $line = 'call "' + $vs + '" ' + $Architecture + ' 10.0.26100.0 && ' + ($commands -join ' && ')
    & cmd.exe /d /c $line
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally { Pop-Location }
