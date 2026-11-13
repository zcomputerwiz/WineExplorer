# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
[CmdletBinding()]
param(
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [string]$OutDirectory,
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$sdk = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt'
$out = if ($OutDirectory) { [IO.Path]::GetFullPath($OutDirectory) } else { Join-Path $PSScriptRoot "build\$Architecture" }
$command = "cl /nologo /std:c++17 /EHsc /MT /I`"$sdk`" `"$PSScriptRoot\view.cpp`" /link /APPCONTAINER /OUT:view.exe runtimeobject.lib windowsapp.lib ole32.lib advapi32.lib uuid.lib d3d11.lib dxgi.lib"
Write-Host "call `"$vs`" $Architecture 10.0.26100.0"
Write-Host $command
if (-not $Execute) { return }
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $out
try {
    $line = 'call "' + $vs + '" ' + $Architecture + ' 10.0.26100.0 && ' + $command
    & cmd.exe /d /c $line
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally { Pop-Location }
