# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$')][string]$Name,
    [Parameter(Mandatory)][string]$Publisher,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$Version,
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [Parameter(Mandatory)][string]$WorkDirectory,
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$tools = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$build = Join-Path $PSScriptRoot "build\$Architecture"
$work = [IO.Path]::GetFullPath($WorkDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
if (-not $work.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($work) -notlike 'u1oracle-*') { throw 'WorkDirectory must be a new u1oracle-* directory under the user temp directory.' }
Write-Host "Create temporary signing certificate for $Publisher, export public certificate to $work, and build three signed packages."
Write-Host "`"$tools\makeappx.exe`" pack /d <stage> /p <case.appx> /o"
Write-Host "`"$tools\signtool.exe`" sign /fd SHA256 /sha1 <new-certificate-thumbprint> <case.appx>"
if (-not $Execute) { return }
if (Test-Path -LiteralPath $work) { throw 'WorkDirectory must not already exist.' }
if (-not (Test-Path -LiteralPath (Join-Path $build 'probe.exe')) -or -not (Test-Path -LiteralPath (Join-Path $build 'component.dll'))) { throw 'Build this architecture first.' }
if (Get-AppxPackage -Name $Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $work | Out-Null
$cert = $null
try {
    # Windows creates the private key through its certificate provider; export only the public certificate.
    $cert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -KeyUsage DigitalSignature -FriendlyName "U1 oracle $Name" -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(2) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3')
    $state = @{ Name=$Name; Publisher=$Publisher; Version=$Version; Architecture=$Architecture; Thumbprint=$cert.Thumbprint; WorkDirectory=$work }
    $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'state.json') -Encoding UTF8
    Export-Certificate -Cert $cert -FilePath (Join-Path $work 'oracle.cer') | Out-Null
    Add-Type -AssemblyName System.Drawing
    foreach ($case in @('positive','missing_class','missing_dll')) {
        $stage = Join-Path $work "stage-$case"
        New-Item -ItemType Directory -Path $stage | Out-Null
        Copy-Item -LiteralPath (Join-Path $build 'probe.exe') -Destination $stage
        if ($case -ne 'missing_dll') { Copy-Item -LiteralPath (Join-Path $build 'component.dll') -Destination $stage }
        foreach ($image in @(@('logo.png',150),@('small.png',44))) {
            $bitmap = New-Object System.Drawing.Bitmap ([int]$image[1]),([int]$image[1])
            try { $bitmap.Save((Join-Path $stage $image[0]), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
        }
        $server = if ($case -eq 'missing_class') { '' } else { '<Extensions><Extension Category="windows.activatableClass.inProcessServer"><InProcessServer><Path>component.dll</Path><ActivatableClass ActivatableClassId="WineExplorer.U1Oracle.Component" ThreadingModel="both" /></InProcessServer></Extension></Extensions>' }
        $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw
        foreach ($entry in @{ '@NAME@'=$Name; '@PUBLISHER@'=$Publisher; '@VERSION@'=$Version; '@ARCH@'=$Architecture; '@SERVER@'=$server }.GetEnumerator()) {
            $value = if ($entry.Key -eq '@SERVER@') { $entry.Value } else { [Security.SecurityElement]::Escape($entry.Value) }
            $manifest = $manifest.Replace($entry.Key, $value)
        }
        [IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))
        $package = Join-Path $work "$case.appx"
        & "$tools\makeappx.exe" pack /d $stage /p $package /o
        if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed for $case" }
        & "$tools\signtool.exe" sign /fd SHA256 /sha1 $cert.Thumbprint $package
        if ($LASTEXITCODE -ne 0) { throw "SignTool failed for $case" }
    }
    Write-Host "Public certificate: $work\oracle.cer. Arrange trust externally before running; this script does not change trust or policy."
} catch {
    if ($cert) { Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -DeleteKey -ErrorAction Continue }
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    throw
}
