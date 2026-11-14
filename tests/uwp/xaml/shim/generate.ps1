# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds WinmdGen and writes XamlShim\Abi.g.cs from the Windows SDK metadata for the classes in gen\classes.txt.
$ErrorActionPreference = 'Stop'
$env:PATH = "$env:LOCALAPPDATA\Microsoft\dotnet;$env:PATH"
$winmd = 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.26100.0\Windows.winmd'
dotnet build (Join-Path $PSScriptRoot 'gen') -c Release -v q -nologo
if ($LASTEXITCODE -ne 0) { throw 'WinmdGen build failed' }
$classes = Get-Content (Join-Path $PSScriptRoot 'gen\classes.txt')
dotnet (Join-Path $PSScriptRoot 'gen\bin\Release\net10.0\WinmdGen.dll') $winmd (Join-Path $PSScriptRoot 'XamlShim\Abi.g.cs') XamlShim.Abi @classes
if ($LASTEXITCODE -ne 0) { throw 'WinmdGen failed' }
