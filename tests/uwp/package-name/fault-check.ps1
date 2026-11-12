# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-R0B validator fault-check for PackageFullNameFromId.
#
# Fault-checks validate.ps1 against synthetic copies of real captured records.
# It takes a build-run.ps1 output directory (which holds records_x64.jsonl
# and records_x86.jsonl), makes a clean copy plus faulted copies under a fresh
# unique child <dir>\fault-<yyyyMMdd-HHmmss>-<pid>\, and verifies that the
# validator accepts the clean copy and rejects every faulted copy:
#   1. clean                       (expect accept)
#   2. missing                     (omitted record; expect reject)
#   3. truncated                   (mid-record cut; expect reject)
#   4. duplicated                  (appended duplicate record; expect reject)
#   5. altered_ret                 (wrong return code; expect reject)
#   6. empty_success_str           (ret=0 with empty str and expected; expect reject)
#   7. altered_expected_and_actual (both str and expected altered together; expect reject)
#   8. wrong_success_out_len       (ret=0 with wrong out_len=999999; expect reject)
#   9. short_capacity              (short in_len on capacity case; expect reject)
#  10. excess_capacity             (excess in_len/out_len on extra_capacity; expect reject)
#  11. wrong_type                  (string in_len on numeric case + UINT32 overflow; expect reject)
#  12. altered_param_na            (null_length in_len changed from "NA" to 0; expect reject)
#
# The input directory is read-only for this checker: the captured records are
# copied, never moved or deleted, and their hashes are verified unchanged at
# the end. Preexisting fault trees are never touched or deleted; every run
# creates its own fresh unique child.
#
# Exit codes:
#   0 = the validator behaved correctly on all copies and input evidence is intact
#   1 = validator did not behave as expected (or input evidence was modified)
#   2 = input directory was refused or unavailable

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Directory
)
$ErrorActionPreference = 'Stop'

if (-not $env:PATHEXT -or -not ($env:PATHEXT -match '\.EXE')) {
    $env:PATHEXT = '.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC;.CPL'
}

$srcRoot = $PSScriptRoot
$validator = Join-Path $srcRoot 'validate.ps1'

function Refuse([string]$reason) {
    Write-Host "REFUSED: $reason"
    exit 2
}

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

$inHash = @{}
foreach ($f in @($x64, $x86)) {
    $inHash[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$faultRoot = Join-Path $dirFull "fault-$stamp-$PID"
try {
    New-Item -ItemType Directory -Path $faultRoot | Out-Null
} catch {
    Refuse "could not create the fresh fault directory $faultRoot : $_"
}

$checks = @()

function Invoke-VariantCheck([string]$name, [bool]$expectAccept, [scriptblock]$mutateX64) {
    $dir = Join-Path $faultRoot $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $a = Join-Path $dir 'records_x64.jsonl'
    $b = Join-Path $dir 'records_x86.jsonl'
    Copy-Item -LiteralPath $x64 -Destination $a
    Copy-Item -LiteralPath $x86 -Destination $b
    & $mutateX64 $a

    $mutHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $a).Hash
    $mutated = ($mutHash -ne $inHash[$x64])

    $out = & $validator $a $b 2>&1
    $exitCode = $LASTEXITCODE
    $outText = ($out -join "`n")

    if ($expectAccept) {
        $ok = ($exitCode -eq 0) -and (-not $mutated)
        $label = "$name (expect accept)"
    } else {
        $ok = ($exitCode -ne 0) -and $mutated
        $label = "$name (expect reject)"
        if (-not $mutated) {
            $outText = "MUTATION NO-OP: mutated file hash matches original`n" + $outText
        }
    }

    $script:checks += [pscustomobject]@{
        name   = $label
        actual = $exitCode
        ok     = $ok
        detail = $outText
    }
}

# 1. Clean copy: validator must accept
Invoke-VariantCheck 'clean' $true { param($a) }

# 2. Missing record: omit the last x64 record
Invoke-VariantCheck 'missing' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 1) {
        $lines = $lines[0..($lines.Count - 2)]
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 3. Truncated: last x64 line cut mid-record
Invoke-VariantCheck 'truncated' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0) {
        $last = [string]$lines[-1]
        $cut = [math]::Max(1, [int]($last.Length / 2))
        $lines[-1] = $last.Substring(0, $cut)
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 4. Duplicated: first x64 record appended again
Invoke-VariantCheck 'duplicated' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0) { $lines += $lines[0] }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 5. Altered return code: change ret on first record
Invoke-VariantCheck 'altered_ret' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    if ($lines.Count -gt 0 -and $lines[0] -match '"ret":(-?\d+)') {
        $old = $Matches[1]
        $new = [string]([long]$old + 1)
        $lines[0] = [string]$lines[0] -replace [regex]::Escape('"ret":' + $old), ('"ret":' + $new)
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 6. Empty success string: resource_id_null has str="" and expected=""
Invoke-VariantCheck 'empty_success_str' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"resource_id_null"') {
            $lines[$i] = $lines[$i] -replace '"str":"[^"]*"', '"str":""'
            $lines[$i] = $lines[$i] -replace '"expected":"[^"]*"', '"expected":""'
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 7. Altered expected + actual together: derive_publisher has both str and expected changed to same wrong string
Invoke-VariantCheck 'altered_expected_and_actual' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"derive_publisher"') {
            $wrong = 'TestPackage_1.0.0.0_x86_res_zzzzzzzzzzzzz'
            $lines[$i] = $lines[$i] -replace '"str":"[^"]*"', ('"str":"' + $wrong + '"')
            $lines[$i] = $lines[$i] -replace '"expected":"[^"]*"', ('"expected":"' + $wrong + '"')
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 8. Wrong successful out_len: resource_id_null has out_len=999999
Invoke-VariantCheck 'wrong_success_out_len' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"resource_id_null"') {
            $lines[$i] = $lines[$i] -replace '"out_len":\d+', '"out_len":999999'
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 9. Short capacity: sentinel_len_req_minus_1 in_len changed from 41 to 40
Invoke-VariantCheck 'short_capacity' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"sentinel_len_req_minus_1"') {
            $lines[$i] = $lines[$i] -replace '"in_len":41', '"in_len":40'
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 10. Excess capacity: extra_capacity in_len changed from 52 to 53 and out_len left at 52
Invoke-VariantCheck 'excess_capacity' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"extra_capacity"') {
            $lines[$i] = $lines[$i] -replace '"in_len":52,"out_len":42', '"in_len":53,"out_len":52'
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 11. Wrong type & UINT32 overflow: string in_len on exact_capacity and UINT32 overflow on null_id
Invoke-VariantCheck 'wrong_type' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"exact_capacity"') {
            $lines[$i] = $lines[$i] -replace '"in_len":42', '"in_len":"42"'
        }
        if ($lines[$i] -match '"test":"null_id"') {
            $lines[$i] = $lines[$i] -replace '"out_len":0', '"out_len":4294967296'
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

# 12. Altered parameter NA / length: null_length in_len changed from "NA" to 0
Invoke-VariantCheck 'altered_param_na' $false {
    param($a)
    $lines = @(Get-Content -LiteralPath $a)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '"test":"null_length"') {
            $lines[$i] = $lines[$i] -replace '"in_len":"NA"', '"in_len":0'
            break
        }
    }
    Set-Content -LiteralPath $a -Value $lines -Encoding utf8
}

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
Write-Host 'FAULT-CHECK: PASS (clean accepted; all 11 faulted variants rejected; input evidence unchanged)'
exit 0
