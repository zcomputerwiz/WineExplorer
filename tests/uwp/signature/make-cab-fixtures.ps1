# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds a small cabinet signed with a new self-signed test certificate (key discarded afterwards, nothing added to any trust
# store) and, with tamper-cab.py, copies that break it in one way each. Usage: make-cab-fixtures.ps1 -OutDirectory dir
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutDirectory)
$ErrorActionPreference = 'Stop'
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$out = [IO.Path]::GetFullPath($OutDirectory)
if (Test-Path -LiteralPath $out) { throw 'OutDirectory must not exist.' }
New-Item -ItemType Directory -Path $out | Out-Null
$random = New-Object Random 11
$data = New-Object byte[] 70000
for ($i = 0; $i -lt $data.Length; $i++) { $data[$i] = [byte]($random.Next(0, 16) + 65) }
$payload = Join-Path $out 'payload.bin'
[IO.File]::WriteAllBytes($payload, $data)
$cab = Join-Path $out 'signed.cab'
Push-Location $out
try { & makecab.exe /D CompressionType=LZX /D Cabinet=on /D Compress=on payload.bin signed.cab | Out-Null } finally { Pop-Location }
if (-not (Test-Path $cab)) { throw 'makecab failed' }
Copy-Item -LiteralPath $cab -Destination (Join-Path $out 'unsigned.cab')
$cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=WineExplorer Signature Test' -KeyUsage DigitalSignature `
    -FriendlyName 'wexappx signature test' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(10) `
    -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
try {
    Export-Certificate -Cert $cert -FilePath (Join-Path $out 'test.cer') | Out-Null
    & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint $cab | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'SignTool failed' }
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue
}
Remove-Item -LiteralPath $payload
Get-ChildItem -LiteralPath $out | Select-Object Name, Length
