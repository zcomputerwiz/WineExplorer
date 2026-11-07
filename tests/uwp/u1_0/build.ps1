# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
[CmdletBinding()]
param([ValidateSet('x64','x86')][string]$Architecture = 'x64', [switch]$Execute)
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$sdk = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt'
$out = Join-Path $PSScriptRoot "build\$Architecture"
$commands = @(
    "cl /nologo /std:c++17 /EHsc /MT /LD `"$PSScriptRoot\component.cpp`" /link /DEF:`"$PSScriptRoot\component.def`" /OUT:component.dll runtimeobject.lib ole32.lib uuid.lib",
    "cl /nologo /std:c++17 /EHsc /MT /I`"$sdk`" `"$PSScriptRoot\probe.cpp`" /link /OUT:probe.exe runtimeobject.lib windowsapp.lib ole32.lib advapi32.lib uuid.lib",
    "cl /nologo /std:c++17 /EHsc /MT `"$PSScriptRoot\launcher.cpp`" /link /OUT:launcher.exe ole32.lib uuid.lib"
)
Write-Host "call `"$vs`" $Architecture 10.0.26100.0"
$commands | ForEach-Object { Write-Host $_ }
if (-not $Execute) { return }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $out
try {
    $line = 'call "' + $vs + '" ' + $Architecture + ' 10.0.26100.0 && ' + ($commands -join ' && ')
    & cmd.exe /d /s /c "`"$line`""
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally { Pop-Location }
