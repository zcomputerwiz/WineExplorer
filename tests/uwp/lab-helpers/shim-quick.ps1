# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# host: a quick XamlShim build for the lab: the ABI again only when classes.txt or the generator changed, an incremental
# publish into quickpub\XamlShim, the classes list the component writes, and quick.tar with the files that differ from
# the base release zip (the one the lab's base prefix was made from).
# usage: shim-quick.ps1 -BaseZip xamlshim-0.2.128-win-x64.zip
param([Parameter(Mandatory)][string]$BaseZip)
$ErrorActionPreference = 'Stop'
$pad = $PSScriptRoot
$wex = 'D:\DeepThought\Projects\uno-uwp\wex'
$env:PATH = "$env:LOCALAPPDATA\Microsoft\dotnet;$env:PATH"
$abi = Join-Path $wex 'XamlShim\Abi.g.cs'
$inputs = @(Get-Item (Join-Path $wex 'WinmdGen\classes.txt')) + @(Get-ChildItem (Join-Path $wex 'WinmdGen') -Filter *.cs)
if (-not (Test-Path $abi) -or ($inputs | Where-Object { $_.LastWriteTime -gt (Get-Item $abi).LastWriteTime })) {
    Write-Host 'generating the ABI'
    & (Join-Path $wex 'generate.ps1') | Out-Null
}
$dir = Join-Path $pad 'quickpub\XamlShim'
dotnet publish (Join-Path $wex 'XamlShim') -c Release -f net10.0-desktop -r win-x64 --self-contained -o $dir -nologo -v q
if ($LASTEXITCODE -ne 0) { throw 'dotnet publish failed' }
$list = Join-Path $dir 'classes.txt'
$p = Start-Process -FilePath (Join-Path $dir 'XamlShim.exe') -ArgumentList '--classes', "`"$list`"" -Wait -PassThru -NoNewWindow
if ($p.ExitCode -ne 0) { throw 'XamlShim --classes failed' }
python -I (Join-Path $pad 'quick-pack.py') $dir (Join-Path $pad $BaseZip) (Join-Path $pad 'quick.tar')
if ($LASTEXITCODE -ne 0) { throw 'quick-pack failed' }
