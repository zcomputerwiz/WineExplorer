# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-R0B sentinel preservation and refusal checks for build-run.ps1 and fault-check.ps1.
#
# Every check invokes only the preview or refusal paths of the drivers:
# no build, no probe run, no deletion of preexisting data. The only
# operations are on disposable paths this script creates and owns under
# the test directory, which it cleans up at the end.
#
# Exit 0 = all checks passed; 1 = one or more checks failed; 2 = blocked.

[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

if (-not $env:PATHEXT -or -not ($env:PATHEXT -match '\.EXE')) {
    $env:PATHEXT = '.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC;.CPL'
}

$srcRoot = $PSScriptRoot
$buildRun = Join-Path $srcRoot 'build-run.ps1'
$faultCheck = Join-Path $srcRoot 'fault-check.ps1'

# Locate repository root
$repoRoot = $null
$node = Get-Item -LiteralPath $srcRoot
while ($node -and $node.FullName) {
    if (Test-Path (Join-Path $node.FullName '.git')) { $repoRoot = $node.FullName; break }
    $node = $node.Parent
}
if ($null -eq $repoRoot) {
    Write-Host 'BLOCKED: could not locate repository root (.git) above test directory'
    exit 2
}
$repoParent = [IO.Path]::GetDirectoryName($repoRoot)

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path $srcRoot "sentinel-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $work | Out-Null
} catch {
    Write-Host "BLOCKED: could not create disposable work area $work : $_"
    exit 2
}

$checks = @()
function Add-Check([string]$name, [bool]$ok, [string]$detail) {
    $script:checks += [pscustomobject]@{ name = $name; ok = $ok; detail = $detail }
}

function Invoke-BuildRun([hashtable]$argTable) {
    & $buildRun @argTable | Out-Null
    return $LASTEXITCODE
}
function Invoke-FaultCheck([hashtable]$argTable) {
    & $faultCheck @argTable | Out-Null
    return $LASTEXITCODE
}

# 1. Existing output directory refused, sentinel file intact
$existing = Join-Path $srcRoot "out-sentinel-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $existing | Out-Null
} catch {
    Write-Host "BLOCKED: could not create disposable existing directory $existing : $_"
    exit 2
}
$sentinelFile = Join-Path $existing 'sentinel.txt'
Set-Content -LiteralPath $sentinelFile -Value "sentinel-$stamp-$PID"
$sentinelBefore = (Get-FileHash -Algorithm SHA256 -LiteralPath $sentinelFile).Hash
$code = Invoke-BuildRun @{ OutDirectory = $existing }
$sentinelAfter = (Get-FileHash -Algorithm SHA256 -LiteralPath $sentinelFile).Hash
Add-Check 'build-run: existing output refused, sentinel intact' `
    (($code -eq 3) -and ($sentinelBefore -eq $sentinelAfter)) `
    "exit=$code sentinel_unchanged=$($sentinelBefore -eq $sentinelAfter)"

# 2. Source directory refused
$code = Invoke-BuildRun @{ OutDirectory = $srcRoot }
Add-Check 'build-run: source directory refused' ($code -eq 3) "exit=$code"

# 3. Project root refused
$code = Invoke-BuildRun @{ OutDirectory = $repoRoot }
Add-Check 'build-run: project root refused' ($code -eq 3) "exit=$code"

# 4. Filesystem root refused
$driveRoot = [IO.Path]::GetPathRoot($srcRoot)
$code = Invoke-BuildRun @{ OutDirectory = $driveRoot }
Add-Check 'build-run: filesystem root refused' ($code -eq 3) "exit=$code"

# 5. Foreign destination refused, nothing created
$foreign = Join-Path $repoParent "wex-uwp-package-name-foreign-$stamp-$PID"
$code = Invoke-BuildRun @{ OutDirectory = $foreign }
$foreignCreated = Test-Path -LiteralPath $foreign
Add-Check 'build-run: foreign destination refused, nothing created' `
    (($code -eq 3) -and (-not $foreignCreated)) "exit=$code created=$foreignCreated"

# 6. Junction destination refused, target intact
$junction = Join-Path $work 'link'
$junctionTarget = Join-Path $work 'link-target'
$junctionSupported = $true
try {
    New-Item -ItemType Directory -Path $junctionTarget | Out-Null
    New-Item -ItemType Junction -Path $junction -Target $junctionTarget | Out-Null
} catch {
    $junctionSupported = $false
}
if ($junctionSupported) {
    $code = Invoke-BuildRun @{ OutDirectory = (Join-Path $junction 'sub') }
    $targetIntact = Test-Path -LiteralPath $junctionTarget
    Add-Check 'build-run: junction destination refused, target intact' `
        (($code -eq 3) -and $targetIntact) "exit=$code target_intact=$targetIntact"
} else {
    Add-Check 'build-run: junction destination refused (SKIPPED: junctions unsupported)' $true 'skipped'
}

# 7. Nested destination refused, nothing created
$nested = Join-Path $work 'nested\out-nested'
$code = Invoke-BuildRun @{ OutDirectory = $nested }
$nestedCreated = Test-Path -LiteralPath $nested
Add-Check 'build-run: nested destination refused, nothing created' `
    (($code -eq 3) -and (-not $nestedCreated)) "exit=$code created=$nestedCreated"

# 8. Non-out-* direct child refused, work area intact
$code = Invoke-BuildRun @{ OutDirectory = $work }
$workIntact = Test-Path -LiteralPath $work
Add-Check 'build-run: non-out-* direct child refused, work area intact' `
    (($code -eq 3) -and $workIntact) "exit=$code work_intact=$workIntact"

# 9. -TimeoutSeconds 0 rejected (exit 4)
$code = Invoke-BuildRun @{ TimeoutSeconds = 0 }
Add-Check 'build-run: -TimeoutSeconds 0 rejected' ($code -eq 4) "exit=$code"

# 10. -TimeoutSeconds 301 rejected (exit 4)
$code = Invoke-BuildRun @{ TimeoutSeconds = 301 }
Add-Check 'build-run: -TimeoutSeconds 301 rejected' ($code -eq 4) "exit=$code"

# 11. Default preview OK, nothing created
$outDirsBefore = @(Get-ChildItem -LiteralPath $srcRoot -Directory | Where-Object { $_.Name -like 'out-*' })
$code = Invoke-BuildRun @{}
$outDirsAfter = @(Get-ChildItem -LiteralPath $srcRoot -Directory | Where-Object { $_.Name -like 'out-*' })
$created = @($outDirsAfter | Where-Object { $outDirsBefore.Name -notcontains $_.Name })
Add-Check 'build-run: default preview OK, nothing created' `
    (($code -eq 0) -and ($created.Count -eq 0)) "exit=$code created=$($created.Count)"

# 12. fault-check: junction input refused (exit 2)
if ($junctionSupported) {
    $code = Invoke-FaultCheck @{ Directory = $junction }
    Add-Check 'fault-check: junction input refused' ($code -eq 2) "exit=$code"
} else {
    Add-Check 'fault-check: junction input refused (SKIPPED: junctions unsupported)' $true 'skipped'
}

# 13. fault-check: foreign input refused (exit 2)
$code = Invoke-FaultCheck @{ Directory = $repoParent }
Add-Check 'fault-check: foreign input refused' ($code -eq 2) "exit=$code"

# 14. fault-check: missing records directory refused (exit 2)
$empty = Join-Path $work 'empty'
New-Item -ItemType Directory -Path $empty | Out-Null
$code = Invoke-FaultCheck @{ Directory = $empty }
Add-Check 'fault-check: missing record files refused' ($code -eq 2) "exit=$code"

# Cleanup
if (Test-Path -LiteralPath $junction) {
    [System.IO.Directory]::Delete($junction)
}
Remove-Item -LiteralPath $work -Recurse -Force
if (Test-Path -LiteralPath $existing) {
    Remove-Item -LiteralPath $existing -Recurse -Force
}

$allOk = $true
foreach ($c in $checks) {
    $status = if ($c.ok) { 'PASS' } else { 'FAIL' }
    if (-not $c.ok) { $allOk = $false }
    Write-Host ("{0}  {1}  ({2})" -f $status, $c.name, $c.detail)
}
if (-not $allOk) {
    Write-Host 'SENTINEL-CHECK: FAIL (one or more preservation/refusal checks failed)'
    exit 1
}
Write-Host 'SENTINEL-CHECK: PASS (all preservation and refusal checks passed)'
exit 0
