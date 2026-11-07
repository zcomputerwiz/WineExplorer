# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
if (-not $work.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($work) -notlike 'u1oracle-*') { throw 'WorkDirectory must be a u1oracle-* directory under the user temp directory.' }
Write-Host "Remove the certificate identified by $work\state.json and the owned work directory."
if (-not $Execute) { return }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work -or $state.Thumbprint -notmatch '^[0-9A-F]{40}$') { throw 'Invalid ownership state.' }
if (Get-AppxPackage -Name $state.Name) { throw 'A package with this name remains installed; inspect it before cleanup.' }
$certPath = "Cert:\CurrentUser\My\$($state.Thumbprint)"
if (Test-Path -LiteralPath $certPath) { Remove-Item -LiteralPath $certPath -DeleteKey }
Remove-Item -LiteralPath $work -Recurse -Force
