# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-M2 validator fault-check.
#
# Fault-checks validate.ps1 against synthetic copies of real captured records.
# It takes a build-run.ps1 output directory (which holds records_x64.jsonl
# and records_x86.jsonl), makes a clean copy plus three faulted copies
# (truncated, duplicate, altered) under a fresh unique child
# <dir>\fault-<yyyyMMdd-HHmmss>-<pid>\, and verifies that the validator
# accepts the clean copy and rejects each faulted copy. Each faulted copy
# keeps both architecture files so the specific fault - not a missing
# architecture - is what the validator must catch.
#
# The input directory is read-only for this checker: the captured records are
# copied, never moved or deleted, and their hashes are verified unchanged at
# the end. Preexisting fault trees (including the captured out\fault\) are
# never touched or deleted; every run creates its own fresh unique child. The
# input directory must resolve to a proper descendant of this test directory;
# filesystem roots, the source directory, the project root, foreign
# (outside-repo) destinations and reparse points (symlinks/junctions) in the
# path or any ancestor are refused.
#
# Exit 0 = the validator behaved correctly on all four copies; 1 = it did not
# (or the input evidence changed); 2 = the input was refused or unavailable.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Directory
)
$ErrorActionPreference = 'Stop'

$srcRoot = $PSScriptRoot
$validator = Join-Path $srcRoot 'validate.ps1'

function Refuse([string]$reason) {
    Write-Host "REFUSED: $reason"
    exit 2
}

# --- Input directory policy (fail-closed; the input evidence is never deleted). ---
$dirFull = [IO.Path]::GetFullPath($Directory)
$rootFull = [IO.Path]::GetFullPath($srcRoot)
$normDir = $dirFull.TrimEnd('\').ToUpperInvariant()
$normRoot = $rootFull.TrimEnd('\').ToUpperInvariant()

$driveRoot = [IO.Path]::GetPathRoot($dirFull).TrimEnd('\')
if ($normDir -eq $driveRoot.ToUpperInvariant()) {
    Refuse "filesystem root is not an allowed input directory: $dirFull"
}
if ($normDir -eq $normRoot) {
    Refuse "the source directory is not an allowed input directory: $dirFull"
}
if (-not $normDir.StartsWith($normRoot + '\')) {
    Refuse "input directory must be inside the test directory $rootFull (project root and foreign destinations are refused): $dirFull"
}
if (-not (Test-Path -LiteralPath $dirFull -PathType Container)) {
    Refuse "input directory does not exist: $dirFull"
}
$dirItem = Get-Item -LiteralPath $dirFull -Force
if ($dirItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    Refuse "input directory is a reparse point (symlink/junction): $dirFull"
}
$ancestor = $dirItem.Parent
while ($ancestor -and $ancestor.FullName) {
    if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Refuse "reparse point (symlink/junction) in the input directory's ancestor chain: $($ancestor.FullName)"
    }
    $ancestor = $ancestor.Parent
}

$x64 = Join-Path $dirFull 'records_x64.jsonl'
$x86 = Join-Path $dirFull 'records_x86.jsonl'
foreach ($f in @($validator, $x64, $x86)) {
    if (-not (Test-Path -LiteralPath $f)) {
        Refuse "required input not found: $f"
    }
}

# The input evidence is read-only: record its hashes now and verify them
# unchanged at the end.
$inHash = @{}
foreach ($f in @($x64, $x86)) {
    $inHash[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash
}

# --- Fresh unique fault child (never delete a preexisting fault tree). ---
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$faultRoot = Join-Path $dirFull "fault-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $faultRoot | Out-Null
} catch {
    Refuse "could not create the fresh fault directory $faultRoot : $_"
}

function MakeVariant([string]$name, [scriptblock]$mutateX64) {
    $dir = Join-Path $faultRoot $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $a = Join-Path $dir 'records_x64.jsonl'
    $b = Join-Path $dir 'records_x86.jsonl'
    Copy-Item -LiteralPath $x64 -Destination $a
    Copy-Item -LiteralPath $x86 -Destination $b
    & $mutateX64 $a
    return @($a, $b)
}

function RunValidator([string[]]$files) {
    $out = & $validator @files 2>&1
    return @{ exit = $LASTEXITCODE; output = ($out -join "`n") }
}

$checks = @()

# 1. Clean copy: the validator must accept it.
$clean = MakeVariant 'clean' { param($a) }
$r = RunValidator $clean
$checks += [pscustomobject]@{
    name = 'clean (expect accept)'; expected = 0; actual = $r.exit
    ok = ($r.exit -eq 0); detail = $r.output
}

# 2. Truncated: the last x64 line is cut mid-record (malformed JSON).
$trunc = MakeVariant 'truncated' {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0) {
        $last = [string]$lines[-1]
        $cut = [math]::Max(1, [int]($last.Length / 2))
        $lines[-1] = $last.Substring(0, $cut)
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}
$r = RunValidator $trunc
$checks += [pscustomobject]@{
    name = 'truncated (expect reject)'; expected = 1; actual = $r.exit
    ok = ($r.exit -ne 0); detail = $r.output
}

# 3. Duplicate: one complete x64 record is appended again.
$dup = MakeVariant 'duplicated' {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0) { $lines += $lines[0] }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}
$r = RunValidator $dup
$checks += [pscustomobject]@{
    name = 'duplicated (expect reject)'; expected = 1; actual = $r.exit
    ok = ($r.exit -ne 0); detail = $r.output
}

# 4. Altered: the first x64 record's status_dec is changed by one.
$alt = MakeVariant 'altered' {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0 -and $lines[0] -match '"status_dec":(-?\d+)') {
        $old = $Matches[1]
        $new = [string]([long]$old + 1)
        $lines[0] = [string]$lines[0] -replace [regex]::Escape('"status_dec":' + $old), ('"status_dec":' + $new)
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}
$r = RunValidator $alt
$checks += [pscustomobject]@{
    name = 'altered (expect reject)'; expected = 1; actual = $r.exit
    ok = ($r.exit -ne 0); detail = $r.output
}

# --- Verify the input evidence is byte-identical after the fault-check. ---
$inputIntact = $true
foreach ($f in @($x64, $x86)) {
    $after = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash
    if ($after -ne $inHash[$f]) {
        Write-Host "FAIL: input evidence changed: $f"
        $inputIntact = $false
    }
}

$allOk = $true
foreach ($c in $checks) {
    $status = if ($c.ok) { 'PASS' } else { 'FAIL' }
    if (-not $c.ok) { $allOk = $false }
    Write-Host ("{0}  {1}  (validator exit {2})" -f $status, $c.name, $c.actual)
    if (-not $c.ok) {
        $c.detail -split "`n" | ForEach-Object { Write-Host "      $_" }
    }
}

if (-not $inputIntact) {
    Write-Host 'FAULT-CHECK: FAIL (input evidence was modified)'
    exit 1
}
if (-not $allOk) {
    Write-Host 'FAULT-CHECK: FAIL (validator did not behave as expected)'
    exit 1
}
Write-Host 'FAULT-CHECK: PASS (clean accepted; truncated, duplicate and altered rejected; input evidence unchanged)'
exit 0
