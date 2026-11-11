# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-M2R sentinel preservation/refusal checks for build-run.ps1 and
# fault-check.ps1.
#
# Every check invokes only the preview or refusal (fail) paths of the drivers:
# no build, no probe run, no deletion of preexisting data. The only
# destructive operations are on the exact disposable paths this script creates
# and owns under the test directory, which it removes again at the end. The
# fresh fault-<timestamp>-<pid> child that fault-check.ps1 creates under the
# captured out/ is left in place (gitignored under out/); this script never
# infers ownership of a path from it appearing since a snapshot, and never
# deletes anything it did not create itself.
#
# build-run.ps1 checks (preview mode):
#   1.  an existing output directory (a direct out-* child, with a sentinel
#       file) -> refused (3), file intact
#   2.  the source directory -> refused (3)
#   3.  the project (repo) root -> refused (3)
#   4.  a filesystem root -> refused (3)
#   5.  a foreign (outside-repo) destination -> refused (3), nothing created
#   6.  a reparse point (junction) destination -> refused (3), target intact
#   7.  a nested destination (not a direct child) -> refused (3), nothing
#       created
#   8.  a direct child not named out-* -> refused (3), work area intact
#   9.  -TimeoutSeconds 0 -> invalid (4)
#  10.  -TimeoutSeconds 301 -> invalid (4)
#  11.  default preview -> OK (0), no output directory created
# fault-check.ps1 checks:
#  12. a reparse point (junction) input directory -> refused (2)
#  13. a foreign (outside-repo) input directory -> refused (2)
#  14. an existing directory without the record files -> refused (2)
#  15. the captured out/ -> PASS (0), the preexisting out\fault\ tree is
#      preserved byte-for-byte; the fresh unique fault child created by the
#      run is left in place (gitignored under out/) and is never deleted by
#      this script.
#
# Exit 0 = all checks passed; 1 = one or more checks failed; 2 = blocked
# (could not set up the disposable work area).
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$srcRoot = $PSScriptRoot
$buildRun = Join-Path $srcRoot 'build-run.ps1'
$faultCheck = Join-Path $srcRoot 'fault-check.ps1'
$outDir = Join-Path $srcRoot 'out'

# Repo root: walk up from the test directory to the directory holding .git.
$repoRoot = $null
$node = Get-Item -LiteralPath $srcRoot
while ($node -and $node.FullName) {
    if (Test-Path (Join-Path $node.FullName '.git')) { $repoRoot = $node.FullName; break }
    $node = $node.Parent
}
if ($null -eq $repoRoot) {
    Write-Host 'BLOCKED: could not locate the repository root (.git) above the test directory'
    exit 2
}
$repoParent = [IO.Path]::GetDirectoryName($repoRoot)

# Disposable owned work area under the test directory (gitignored).
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work = Join-Path $srcRoot "sentinel-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $work | Out-Null
} catch {
    Write-Host "BLOCKED: could not create the disposable work area $work : $_"
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

# --- 1. Existing output directory: refused, sentinel file intact. ---
# A direct out-* child of the test directory (gitignored), created and owned
# by this invocation; removed again in the cleanup below.
$existing = Join-Path $srcRoot "out-sentinel-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $existing | Out-Null
} catch {
    Write-Host "BLOCKED: could not create the disposable existing-output directory $existing : $_"
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

# --- 2. The source directory: refused. ---
$code = Invoke-BuildRun @{ OutDirectory = $srcRoot }
Add-Check 'build-run: source directory refused' ($code -eq 3) "exit=$code"

# --- 3. The project (repo) root: refused. ---
$code = Invoke-BuildRun @{ OutDirectory = $repoRoot }
Add-Check 'build-run: project root refused' ($code -eq 3) "exit=$code"

# --- 4. A filesystem root: refused. ---
$driveRoot = [IO.Path]::GetPathRoot($srcRoot)
$code = Invoke-BuildRun @{ OutDirectory = $driveRoot }
Add-Check 'build-run: filesystem root refused' ($code -eq 3) "exit=$code"

# --- 5. A foreign (outside-repo) destination: refused, nothing created. ---
$foreign = Join-Path $repoParent "wex-uwp-identity-foreign-$stamp-$PID"
$code = Invoke-BuildRun @{ OutDirectory = $foreign }
$foreignCreated = Test-Path -LiteralPath $foreign
Add-Check 'build-run: foreign destination refused, nothing created' `
    (($code -eq 3) -and (-not $foreignCreated)) "exit=$code created=$foreignCreated"

# --- 6. A reparse point (junction) destination: refused, target intact. ---
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
    # A nonexistent child under the junction: nested (and under a reparse
    # point), so it must be refused; the junction's target stays intact.
    # (Under the direct-child policy the nested rule fires first; the reparse
    # rule remains defense-in-depth and is exercised by the fault-check input
    # check below.)
    $code = Invoke-BuildRun @{ OutDirectory = (Join-Path $junction 'sub') }
    $targetIntact = Test-Path -LiteralPath $junctionTarget
    Add-Check 'build-run: junction destination refused, target intact' `
        (($code -eq 3) -and $targetIntact) "exit=$code target_intact=$targetIntact"
} else {
    Add-Check 'build-run: junction destination refused (SKIPPED: junctions unsupported)' $true 'skipped'
}

# --- 7. A nested destination (not a direct child): refused, nothing created. ---
$nested = Join-Path $work 'nested\out-nested'
$code = Invoke-BuildRun @{ OutDirectory = $nested }
$nestedCreated = Test-Path -LiteralPath $nested
Add-Check 'build-run: nested destination refused, nothing created' `
    (($code -eq 3) -and (-not $nestedCreated)) "exit=$code created=$nestedCreated"

# --- 8. A direct child not named out-*: refused, work area intact. ---
$code = Invoke-BuildRun @{ OutDirectory = $work }
$workIntact = Test-Path -LiteralPath $work
Add-Check 'build-run: non-out-* direct child refused, work area intact' `
    (($code -eq 3) -and $workIntact) "exit=$code work_intact=$workIntact"

# --- 9/10. Invalid run bounds: rejected. ---
$code = Invoke-BuildRun @{ TimeoutSeconds = 0 }
Add-Check 'build-run: -TimeoutSeconds 0 rejected' ($code -eq 4) "exit=$code"
$code = Invoke-BuildRun @{ TimeoutSeconds = 301 }
Add-Check 'build-run: -TimeoutSeconds 301 rejected' ($code -eq 4) "exit=$code"

# --- 11. Default preview: OK, no output directory created. ---
$outDirsBefore = @(Get-ChildItem -LiteralPath $srcRoot -Directory | Where-Object { $_.Name -like 'out-*' })
$code = Invoke-BuildRun @{}
$outDirsAfter = @(Get-ChildItem -LiteralPath $srcRoot -Directory | Where-Object { $_.Name -like 'out-*' })
$created = @($outDirsAfter | Where-Object { $outDirsBefore.Name -notcontains $_.Name })
Add-Check 'build-run: default preview OK, nothing created' `
    (($code -eq 0) -and ($created.Count -eq 0)) "exit=$code created=$($created.Count)"

# --- 12. fault-check: a reparse point (junction) input directory: refused. ---
if ($junctionSupported) {
    $code = Invoke-FaultCheck @{ Directory = $junction }
    Add-Check 'fault-check: junction input refused' ($code -eq 2) "exit=$code"
} else {
    Add-Check 'fault-check: junction input refused (SKIPPED: junctions unsupported)' $true 'skipped'
}

# --- 13. fault-check: a foreign (outside-repo) input directory: refused. ---
$code = Invoke-FaultCheck @{ Directory = $repoParent }
Add-Check 'fault-check: foreign input refused' ($code -eq 2) "exit=$code"

# --- 14. fault-check: an existing directory without the record files: refused. ---
$empty = Join-Path $work 'empty'
New-Item -ItemType Directory -Path $empty | Out-Null
$code = Invoke-FaultCheck @{ Directory = $empty }
Add-Check 'fault-check: missing record files refused' ($code -eq 2) "exit=$code"

# --- 15. Captured out/: fault-check PASS, preexisting fault/ preserved. ---
$oldFault = Join-Path $outDir 'fault'
if (-not (Test-Path -LiteralPath $oldFault)) {
    Add-Check 'fault-check: captured out/ fault tree preserved (SKIPPED: no captured out/fault/)' $true 'skipped'
} else {
    $before = @{}
    Get-ChildItem -LiteralPath $oldFault -Recurse -File | ForEach-Object {
        $key = $_.FullName.Substring($outDir.Length).TrimStart('\')
        $before[$key] = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
    }
    $faultNamesBefore = @(Get-ChildItem -LiteralPath $outDir -Directory | Where-Object { $_.Name -like 'fault-*' } | ForEach-Object { $_.Name })
    $code = Invoke-FaultCheck @{ Directory = $outDir }
    $faultDirsAfter = @(Get-ChildItem -LiteralPath $outDir -Directory | Where-Object { $_.Name -like 'fault-*' })
    $newDirs = @($faultDirsAfter | Where-Object { $faultNamesBefore -notcontains $_.Name })
    $after = @{}
    Get-ChildItem -LiteralPath $oldFault -Recurse -File | ForEach-Object {
        $key = $_.FullName.Substring($outDir.Length).TrimStart('\')
        $after[$key] = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
    }
    $preserved = ($before.Count -eq $after.Count)
    if ($preserved) {
        foreach ($k in $before.Keys) {
            if (-not $after.ContainsKey($k) -or $after[$k] -ne $before[$k]) { $preserved = $false; break }
        }
    }
    # The fresh fault-<timestamp>-<pid> child created by the run is left in
    # place (gitignored under out/). It is a small diagnostic; this script
    # never infers ownership from a path appearing since a snapshot and never
    # deletes anything it did not create itself.
    Add-Check 'fault-check: captured out/ PASS, preexisting fault/ preserved' `
        (($code -eq 0) -and $preserved) "exit=$code preserved=$preserved new_children=$($newDirs.Count)"
}

# --- Cleanup: remove the exact disposable paths this invocation created. ---
# Remove the junction first (Directory.Delete removes only the link, not the
# target), then the rest of the work tree, then the existing-output directory
# (a single exact path proven created by this invocation).
if (Test-Path -LiteralPath $junction) {
    [System.IO.Directory]::Delete($junction)
}
Remove-Item -LiteralPath $work -Recurse -Force
if (Test-Path -LiteralPath $existing) {
    Remove-Item -LiteralPath $existing -Recurse -Force
}

# --- Report. ---
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
Write-Host 'SENTINEL-CHECK: PASS (all preservation/refusal checks held)'
exit 0
