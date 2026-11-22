# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds the signature-check fixtures of wexappx on a Windows host: one small package signed with a new self-signed test
# certificate (ten years, key discarded afterwards, nothing added to any trust store) and copies that break it in one way
# each, and writes what signtool says about every one of them. Usage: make-fixtures.ps1 -Exe tiny.exe -OutDirectory dir
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OutDirectory
)
$ErrorActionPreference = 'Stop'
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$out = [IO.Path]::GetFullPath($OutDirectory)
if (Test-Path -LiteralPath $out) { throw 'OutDirectory must not exist.' }
New-Item -ItemType Directory -Path $out | Out-Null
$publisher = 'CN=WineExplorer Signature Test'
$stage = Join-Path $out 'stage'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $Exe -Destination (Join-Path $stage 'tiny.exe')
[IO.File]::WriteAllBytes((Join-Path $stage 'logo.png'), [byte[]](0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a))
# a payload bigger than one 64 KiB block of the block map, stored deflated, so that a changed byte is a block mismatch
$random = New-Object Random 7
$data = New-Object byte[] 200000
for ($i = 0; $i -lt $data.Length; $i++) { $data[$i] = [byte]($random.Next(0, 16) + 65) }
[IO.File]::WriteAllBytes((Join-Path $stage 'data.bin'), $data)
$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10" IgnorableNamespaces="uap">
  <Identity Name="WineExplorer.SignatureTest" Publisher="$publisher" Version="1.0.0.0" ProcessorArchitecture="x64" />
  <Properties><DisplayName>Signature test</DisplayName><PublisherDisplayName>WineExplorer</PublisherDisplayName><Logo>logo.png</Logo></Properties>
  <Dependencies><TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0" /></Dependencies>
  <Resources><Resource Language="en-us" /></Resources>
  <Applications>
    <Application Id="Tiny" Executable="tiny.exe" EntryPoint="tiny.App">
      <uap:VisualElements DisplayName="Signature test" Description="Signature test" BackgroundColor="transparent" Square150x150Logo="logo.png" Square44x44Logo="logo.png" />
    </Application>
  </Applications>
</Package>
"@
[IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))
$cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature -FriendlyName 'wexappx signature test' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(10) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
try {
    Export-Certificate -Cert $cert -FilePath (Join-Path $out 'test.cer') | Out-Null
    $signed = Join-Path $out 'signed.appx'
    & "$tools\makeappx.exe" pack /nv /d $stage /p $signed /o | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'MakeAppx failed' }
    Copy-Item -LiteralPath $signed -Destination (Join-Path $out 'unsigned.appx')
    & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint $signed | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SignTool failed' }
    # a program signed with the same certificate: its signature is the material of the foreign-subject fixtures
    Copy-Item -LiteralPath $Exe -Destination (Join-Path $out 'signed-tiny.exe')
    & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint (Join-Path $out 'signed-tiny.exe') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SignTool failed on the program' }
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue
}

# the variants that break the package in one way each are made from signed.appx by tamper.py, byte for byte; verify-fixtures.ps1
# and sigtest.exe say what Windows thinks of them

Remove-Item -LiteralPath $stage -Recurse -Force
Get-ChildItem -LiteralPath $out | Select-Object Name, Length
