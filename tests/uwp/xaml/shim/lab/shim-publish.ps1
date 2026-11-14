# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: publish XamlShim cleanly into scratchpad\cxprobe\xamlshim and add xamlproxy.dll
$ErrorActionPreference = 'Stop'
$env:PATH = "$env:LOCALAPPDATA\Microsoft\dotnet;$env:PATH"
$pad = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $pad 'cxprobe\xamlshim'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
Push-Location 'D:\DeepThought\Projects\uno-uwp\wex\XamlShim'
dotnet publish -c Release -f net10.0-desktop -r win-x64 --self-contained -o $out -v q -nologo *> (Join-Path $pad 'shim-publish.log')
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { Select-String -Path (Join-Path $pad 'shim-publish.log') -Pattern 'error' | Select-Object -First 10 | ForEach-Object Line; throw "publish failed" }
Copy-Item 'D:\DeepThought\Projects\WineExplorer-uwp\tests\uwp\xaml\shim\build\x64\xamlproxy.dll' $out
"published"
