# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds the XAML probe (C++/CX) and its signed package with one of the U1 oracle certificates
# (publisher CN=WineExplorer U1 Oracle, already trusted on the oracle host). Output: build\package\.
[CmdletBinding()]
param(
    [ValidateSet('x64','Win32')][string]$Platform = 'x64',
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-F]{40}$')][string]$Thumbprint
)
$ErrorActionPreference = 'Stop'
$assets = Join-Path $PSScriptRoot 'Assets'
New-Item -ItemType Directory -Force -Path $assets | Out-Null
Add-Type -AssemblyName System.Drawing
foreach ($image in @(@('Logo.png',150),@('SmallLogo.png',44),@('StoreLogo.png',50))) {
    $bitmap = New-Object System.Drawing.Bitmap ([int]$image[1]),([int]$image[1])
    try { $bitmap.Save((Join-Path $assets $image[0]), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
}
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe'
# the first build of a clean tree fails with APPX1673 ("missing PhoneIdentity") before the manifest is generated; the
# second succeeds, so one retry
foreach ($attempt in 1, 2) {
    & $msbuild (Join-Path $PSScriptRoot 'XamlProbe.vcxproj') /nologo /v:m /p:Configuration=Release /p:Platform=$Platform `
        /p:ProbeThumbprint=$Thumbprint /p:UapAppxPackageBuildMode=SideloadOnly /p:AppxPackage=true
    if ($LASTEXITCODE -eq 0) { break }
}
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $LASTEXITCODE" }
