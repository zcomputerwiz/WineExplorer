# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds one signed package of the U1.1 view probe (build\<arch>\view.exe) for a Windows oracle run, with a new
# two-day self-signed certificate. The public certificate is exported to the work directory; trusting it is a
# separate step (-TrustStore), taken only on the owner's say-so.
[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$')][string]$Name = 'WineExplorer.U1View',
    [string]$Publisher = 'CN=WineExplorer U1 Oracle',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$Version = '1.0.0.0',
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [Parameter(Mandatory)][string]$WorkDirectory,
    [ValidatePattern('^[0-9A-F]{40}$')][string]$CertificateThumbprint,   # sign with a certificate this script made earlier
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$build = Join-Path $PSScriptRoot "build\$Architecture"
$work = [IO.Path]::GetFullPath($WorkDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
if (-not $work.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($work) -notlike 'u1oracle-*') { throw 'WorkDirectory must be a new u1oracle-* directory under the user temp directory.' }
Write-Host "Create a temporary signing certificate for $Publisher, export its public part to $work, and build one signed package."
if (-not $Execute) { return }
if (Test-Path -LiteralPath $work) { throw 'WorkDirectory must not already exist.' }
if (-not (Test-Path -LiteralPath (Join-Path $build 'view.exe'))) { throw 'Build this architecture first.' }
if (Get-AppxPackage -Name $Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $work | Out-Null
$cert = $null
try {
    if ($CertificateThumbprint) {
        $cert = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
        if ($cert.Subject -ne $Publisher -or $cert.NotAfter -lt (Get-Date)) { throw 'The certificate does not match the publisher or has expired.' }
    } else {
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature -FriendlyName "U1.1 oracle $Name" -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(2) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    }
    @{ Name=$Name; Publisher=$Publisher; Version=$Version; Architecture=$Architecture; Thumbprint=$cert.Thumbprint; WorkDirectory=$work } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'state.json') -Encoding UTF8
    Export-Certificate -Cert $cert -FilePath (Join-Path $work 'oracle.cer') | Out-Null
    Add-Type -AssemblyName System.Drawing
    $stage = Join-Path $work 'stage'
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath (Join-Path $build 'view.exe') -Destination $stage
    foreach ($image in @(@('logo.png',150),@('small.png',44))) {
        $bitmap = New-Object System.Drawing.Bitmap ([int]$image[1]),([int]$image[1])
        try { $bitmap.Save((Join-Path $stage $image[0]), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    }
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw
    foreach ($entry in @{ '@NAME@'=$Name; '@PUBLISHER@'=$Publisher; '@VERSION@'=$Version; '@ARCH@'=$Architecture }.GetEnumerator()) {
        $manifest = $manifest.Replace($entry.Key, [Security.SecurityElement]::Escape($entry.Value))
    }
    [IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))
    $package = Join-Path $work 'view.appx'
    & "$tools\makeappx.exe" pack /d $stage /p $package /o
    if ($LASTEXITCODE -ne 0) { throw 'MakeAppx failed' }
    & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint $package
    if ($LASTEXITCODE -ne 0) { throw 'SignTool failed' }
    Write-Host "Public certificate: $work\oracle.cer (not trusted by this script)."
} catch {
    if ($cert -and -not $CertificateThumbprint) { Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    throw
}
