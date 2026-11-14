# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab only: unpack the XamlProbe package and the VCLibs framework DLLs into scratchpad\cxprobe\stage, tar it
$ErrorActionPreference = 'Stop'
$pad = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkg = 'D:\DeepThought\Projects\WineExplorer-uwp\tests\uwp\xaml\cx-probe\build\package\XamlProbe_1.0.0.0_x64_Test'
$out = Join-Path $pad 'cxprobe'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Path $stage | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory("$pkg\XamlProbe_1.0.0.0_x64.msix", $stage)
foreach ($f in 'AppxSignature.p7x','AppxBlockMap.xml','[Content_Types].xml') { Remove-Item -LiteralPath (Join-Path $stage $f) -ErrorAction SilentlyContinue }
$vc = Join-Path $out 'vclibs'
[IO.Compression.ZipFile]::ExtractToDirectory("$pkg\Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx", $vc)
Get-ChildItem $vc -Filter *.dll | Copy-Item -Destination $stage
# the compiled pages stay where the XAML compiler put them: embedded in resources.pri (no loose .xbf files)
Get-ChildItem $stage -Recurse | ForEach-Object { $_.FullName.Substring($stage.Length) }
