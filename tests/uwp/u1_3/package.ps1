# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds two signed packages for a Windows oracle run: the framework package and the package that depends on it
# (framework.appx, app.appx), with a certificate this repository's scripts made earlier (-CertificateThumbprint).
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$')][string]$Name = 'WineExplorer.U1Dependent',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$')][string]$FrameworkName = 'WineExplorer.U1Framework',
    [string]$Publisher = 'CN=WineExplorer U1 Oracle',
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[0-9A-F]{40}$')][string]$CertificateThumbprint,
    [string]$FrameworkVersion = '1.2.0.0',
    [string]$FrameworkMinVersion = '1.0.0.0',
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$build = Join-Path $PSScriptRoot "build\$Architecture"
$work = [IO.Path]::GetFullPath($WorkDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
if (-not $work.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($work) -notlike 'u1oracle-*') { throw 'WorkDirectory must be a new u1oracle-* directory under the user temp directory.' }
Write-Host "Build the framework package $FrameworkName and the dependent package $Name, signed with the certificate $CertificateThumbprint."
if (-not $Execute) { return }
if (Test-Path -LiteralPath $work) { throw 'WorkDirectory must not already exist.' }
foreach ($file in 'u1fw.dll','depapp.exe') { if (-not (Test-Path -LiteralPath (Join-Path $build $file))) { throw 'Build this architecture first.' } }
if (Get-AppxPackage -Name $Name) { throw 'The dependent package is already installed for this user.' }
if (Get-AppxPackage -Name $FrameworkName) { throw 'The framework package is already installed for this user.' }
$cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if ($cert.Subject -ne $Publisher -or $cert.NotAfter -lt (Get-Date)) { throw 'The certificate does not match the publisher or has expired.' }
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.Drawing
Export-Certificate -Cert $cert -FilePath (Join-Path $work 'oracle.cer') | Out-Null
@{ Name=$Name; FrameworkName=$FrameworkName; Publisher=$Publisher; Version='1.0.0.0'; FrameworkVersion=$FrameworkVersion; Architecture=$Architecture; WorkDirectory=$work } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'state.json') -Encoding UTF8
function Build-Package($case, $manifestFile, $files) {
    $stage = Join-Path $work "stage-$case"
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($f in $files) { Copy-Item -LiteralPath (Join-Path $build $f) -Destination $stage }
    foreach ($image in @(@('logo.png',150),@('small.png',44))) {
        $bitmap = New-Object System.Drawing.Bitmap ([int]$image[1]),([int]$image[1])
        try { $bitmap.Save((Join-Path $stage $image[0]), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot $manifestFile) -Raw
    $values = @{ '@NAME@'=$Name; '@FWNAME@'=$FrameworkName; '@PUBLISHER@'=$Publisher; '@VERSION@'='1.0.0.0'; '@FWVERSION@'=$FrameworkVersion;
                 '@FWMINVERSION@'=$FrameworkMinVersion; '@ARCH@'=$Architecture }
    foreach ($entry in $values.GetEnumerator()) { $manifest = $manifest.Replace($entry.Key, [Security.SecurityElement]::Escape($entry.Value)) }
    [IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))
    $package = Join-Path $work "$case.appx"
    & "$tools\makeappx.exe" pack /d $stage /p $package /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed for $case" }
    & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint $package
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed for $case" }
}
Build-Package 'framework' 'FrameworkManifest.xml' @('u1fw.dll')
Build-Package 'app' 'AppManifest.xml' @('depapp.exe')
Write-Host "Packages in $work (the certificate must be trusted in LocalMachine TrustedPeople)."
