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
} finally {
    Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue
}

# the variants: zip members are rewritten by hand so that exactly one thing differs from signed.appx
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Copy-Package($name, [scriptblock]$edit) {
    $path = Join-Path $out $name
    Copy-Item -LiteralPath $signed -Destination $path
    & $edit $path
}
function Patch-Bytes($path, [scriptblock]$edit) {
    $bytes = [IO.File]::ReadAllBytes($path)
    & $edit $bytes
    [IO.File]::WriteAllBytes($path, $bytes)
}
function Find-Bytes([byte[]]$haystack, [byte[]]$needle) {
    for ($i = 0; $i -le $haystack.Length - $needle.Length; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $needle.Length; $j++) { if ($haystack[$i + $j] -ne $needle[$j]) { $ok = $false; break } }
        if ($ok) { return $i }
    }
    return -1
}
# tiny.exe changed inside its block: patch the stored/deflated bytes of the member (the .text of the exe is stored when
# deflate does not help; read the member offset from the central directory by opening the zip)
Copy-Package 'bad-block.appx' {
    param($p)
    Patch-Bytes $p { param($b)
        # logo.png is stored: its eight bytes are in the archive as they are
        $at = Find-Bytes $b ([byte[]](0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a))
        if ($at -lt 0) { throw 'no stored logo.png in the package to change' }
        $b[$at + 6] = [byte]0x1b
    }
}
# the block map text changed (a hash digit): the signed digest of AppxBlockMap.xml no longer fits
Copy-Package 'bad-blockmap.appx' {
    param($p)
    $zip = [IO.Compression.ZipFile]::Open($p, 'Update')
    try {
        $entry = $zip.GetEntry('AppxBlockMap.xml')
        $reader = New-Object IO.StreamReader($entry.Open()); $text = $reader.ReadToEnd(); $reader.Close()
        $entry.Delete()
        $new = $zip.CreateEntry('AppxBlockMap.xml')
        $writer = New-Object IO.StreamWriter($new.Open(), (New-Object Text.UTF8Encoding $false))
        $writer.Write($text.Replace('Size="', 'Size ="')); $writer.Close()
    } finally { $zip.Dispose() }
}
# a member added after signing (the central directory digest changes)
Copy-Package 'extra-member.appx' {
    param($p)
    $zip = [IO.Compression.ZipFile]::Open($p, 'Update')
    try { $e = $zip.CreateEntry('extra.txt'); $w = New-Object IO.StreamWriter($e.Open()); $w.Write('added after signing'); $w.Close() } finally { $zip.Dispose() }
}
# the manifest publisher changed (the identity no longer matches the signer)
Copy-Package 'other-publisher.appx' {
    param($p)
    $zip = [IO.Compression.ZipFile]::Open($p, 'Update')
    try {
        $entry = $zip.GetEntry('AppxManifest.xml')
        $reader = New-Object IO.StreamReader($entry.Open()); $text = $reader.ReadToEnd(); $reader.Close()
        $entry.Delete()
        $new = $zip.CreateEntry('AppxManifest.xml')
        $writer = New-Object IO.StreamWriter($new.Open(), (New-Object Text.UTF8Encoding $false))
        $writer.Write($text.Replace('CN=WineExplorer Signature Test', 'CN=Someone Else')); $writer.Close()
    } finally { $zip.Dispose() }
}
# signature file removed
Copy-Package 'no-signature.appx' {
    param($p)
    $zip = [IO.Compression.ZipFile]::Open($p, 'Update')
    try { $zip.GetEntry('AppxSignature.p7x').Delete() } finally { $zip.Dispose() }
}
# the p7x damaged (one byte of the PKCS #7 changed)
Copy-Package 'bad-p7x.appx' {
    param($p)
    $zip = [IO.Compression.ZipFile]::Open($p, 'Update')
    try {
        $entry = $zip.GetEntry('AppxSignature.p7x')
        $s = $entry.Open(); $ms = New-Object IO.MemoryStream; $s.CopyTo($ms); $s.Close()
        $bytes = $ms.ToArray(); $bytes[$bytes.Length - 20] = $bytes[$bytes.Length - 20] -bxor 0xff
        $entry.Delete()
        $new = $zip.CreateEntry('AppxSignature.p7x'); $w = $new.Open(); $w.Write($bytes, 0, $bytes.Length); $w.Close()
    } finally { $zip.Dispose() }
}

# what Windows says about each (the certificate is in no trust store, so the good one is "untrusted root")
$report = @()
foreach ($file in Get-ChildItem -LiteralPath $out -Filter *.appx | Sort-Object Name) {
    $text = & "$tools\signtool.exe" verify /pa /v $file.FullName 2>&1 | Out-String
    $report += "== $($file.Name) (exit $LASTEXITCODE)"
    $report += ($text -split "`r?`n" | Where-Object { $_ -match 'Error|error|Successfully|SignTool|Issued|Hash|No signature' } | Select-Object -First 8)
}
$report | Set-Content -LiteralPath (Join-Path $out 'signtool-report.txt') -Encoding UTF8
Remove-Item -LiteralPath $stage -Recurse -Force
Get-ChildItem -LiteralPath $out | Select-Object Name, Length
