# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab only: unpack the XamlProbe package and the VCLibs framework DLLs into scratchpad\cxprobe\stage, tar it
$ErrorActionPreference = 'Stop'
$pad = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkg = Get-ChildItem 'D:\DeepThought\Projects\WineExplorer-uwp\tests\uwp\xaml\cx-probe\build\package' -Directory -Filter 'XamlProbe_*_x64_Test' |
    Sort-Object { [version]($_.Name -split '_')[1] } | Select-Object -Last 1 -ExpandProperty FullName
$msix = Get-ChildItem $pkg -Filter 'XamlProbe_*_x64.msix' | Select-Object -First 1 -ExpandProperty FullName
$out = Join-Path $pad 'cxprobe'
$stage = Join-Path $out 'stage'
$vc = Join-Path $out 'vclibs'
foreach ($d in $stage, $vc) { if (Test-Path $d) { Remove-Item $d -Recurse -Force } }   # the published shim stays
New-Item -ItemType Directory -Path $stage | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
"staging $msix"
[IO.Compression.ZipFile]::ExtractToDirectory($msix, $stage)
foreach ($f in 'AppxSignature.p7x','AppxBlockMap.xml','[Content_Types].xml') { Remove-Item -LiteralPath (Join-Path $stage $f) -ErrorAction SilentlyContinue }
[IO.Compression.ZipFile]::ExtractToDirectory("$pkg\Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx", $vc)
Get-ChildItem $vc -Filter *.dll | Copy-Item -Destination $stage
# the compiled pages stay where the XAML compiler put them: embedded in resources.pri (no loose .xbf files)
Get-ChildItem $stage -Recurse | ForEach-Object { $_.FullName.Substring($stage.Length) }
