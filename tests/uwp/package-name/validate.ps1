# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-R0B independent results validator for PackageFullNameFromId observation probe.
#
# Validates captured JSONL record files (records_x64.jsonl and records_x86.jsonl).
# Expected return codes, input/output lengths, exact result strings, reference
# expected strings, and canary flags are derived independently from the case
# specification and captured native publisher constants, never by trusting
# record.expected or skipping checks when record.expected is empty.
#
# It rejects:
#   - missing files or empty inputs,
#   - malformed JSON lines (truncated records),
#   - missing, extra, or wrong-typed fields (enforcing real string/scalar/bool types),
#   - out-of-range integer scalars (INT32 for ret, UINT32 [0..4294967295] for lengths),
#   - unsupported schema or architecture, or omitted architecture,
#   - duplicate (arch, category, test) records,
#   - omitted or unexpected test cases per architecture,
#   - any discrepancy in ret, in_len, out_len, str, expected, sentinel_checked,
#     or sentinel_intact against the independent per-case specification.
#
# Exit codes: 0 = all records valid and conformant; 1 = one or more violations.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Files
)
$ErrorActionPreference = 'Stop'

if (-not $env:PATHEXT -or -not ($env:PATHEXT -match '\.EXE')) {
    $env:PATHEXT = '.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC;.CPL'
}

$schema = 'wex-uwp-package-name/1'
$architectures = @('x64', 'x86')
$requiredFields = @(
    'schema', 'arch', 'category', 'test', 'ret', 'in_len', 'out_len',
    'str', 'expected', 'sentinel_checked', 'sentinel_intact', 'notes'
)
$stringFields = @('schema', 'arch', 'category', 'test', 'str', 'expected', 'notes')
$nonEmptyStringFields = @('schema', 'arch', 'category', 'test', 'notes')

# Literal native Windows publisher ID mappings captured on Windows 11 (26300.9974)
$nativePublisherMap = [ordered]@{
    'publisher'                                                                        = '0mk95qz5wh294'
    'CN=Test'                                                                          = 'hz258y3tkez3a'
    'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US' = '8wekyb3d8bbwe'
    'AppX Test Publisher'                                                              = 'q1jy3mp9kw2c8'
    'MyPublisher123'                                                                   = 'j9x1bp7cpjcpy'
}

function Format-PackageFullName([string]$name, [string]$version, [string]$archToken, [string]$resourceId, [string]$publisherId) {
    return "${name}_${version}_${archToken}_${resourceId}_${publisherId}"
}

function New-SuccessSpec([string]$fullName, [int]$extraIn = 0, [bool]$canary = $false) {
    $reqLen = $fullName.Length + 1
    return [pscustomobject]@{
        ret              = [long]0
        na_len           = $false
        in_len           = [long]($reqLen + $extraIn)
        out_len          = [long]$reqLen
        str              = $fullName
        expected         = $fullName
        sentinel_checked = $canary
        sentinel_intact  = $canary
    }
}

function New-InsufficientBufferSpec([int]$inLen, [int]$reqLen, [bool]$canary) {
    return [pscustomobject]@{
        ret              = [long]122
        na_len           = $false
        in_len           = [long]$inLen
        out_len          = [long]$reqLen
        str              = ''
        expected         = ''
        sentinel_checked = $canary
        sentinel_intact  = $canary
    }
}

function New-InvalidParamSpec([bool]$naLen = $false) {
    return [pscustomobject]@{
        ret              = [long]87
        na_len           = $naLen
        in_len           = if ($naLen) { 'NA' } else { [long]0 }
        out_len          = if ($naLen) { 'NA' } else { [long]0 }
        str              = ''
        expected         = ''
        sentinel_checked = $false
        sentinel_intact  = $false
    }
}

# Build independent per-case specification table (all 29 cases)
$caseSpecs = [ordered]@{}

# 1. Buffer capacity cases (TestPackage 1.2.3.4 x86 res 1234567890abc)
$capFullName = Format-PackageFullName 'TestPackage' '1.2.3.4' 'x86' 'res' '1234567890abc'
$capReqLen   = $capFullName.Length + 1
$caseSpecs['buffer_capacity|null_buffer_len_0']        = New-InsufficientBufferSpec -inLen 0 -reqLen $capReqLen -canary $false
$caseSpecs['buffer_capacity|sentinel_len_0']           = New-InsufficientBufferSpec -inLen 0 -reqLen $capReqLen -canary $true
$caseSpecs['buffer_capacity|sentinel_len_1']           = New-InsufficientBufferSpec -inLen 1 -reqLen $capReqLen -canary $true
$caseSpecs['buffer_capacity|sentinel_len_req_minus_1'] = New-InsufficientBufferSpec -inLen ($capReqLen - 1) -reqLen $capReqLen -canary $true
$caseSpecs['buffer_capacity|exact_capacity']           = New-SuccessSpec -fullName $capFullName -extraIn 0 -canary $true
$caseSpecs['buffer_capacity|extra_capacity']           = New-SuccessSpec -fullName $capFullName -extraIn 10 -canary $true

# 2. Resource ID cases (TestPackage 1.0.0.0 x86 <resId> 1234567890abc)
$caseSpecs['resource_id|resource_id_null']                = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' '' '1234567890abc')
$caseSpecs['resource_id|resource_id_empty']               = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' '' '1234567890abc')
$caseSpecs['resource_id|resource_id_nonempty_res']        = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' 'res' '1234567890abc')
$caseSpecs['resource_id|resource_id_nonempty_resourceid'] = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' 'resourceid' '1234567890abc')

# 3. Publisher derivation cases (TestPackage 1.0.0.0 x86 res <derivedPubId>)
foreach ($pub in $nativePublisherMap.Keys) {
    $derivedId = $nativePublisherMap[$pub]
    $caseSpecs["publisher_derivation|derive_$pub"] = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' 'res' $derivedId)
}

# 4. Explicit publisher ID cases
$caseSpecs['publisher_id|explicit_13_chars']          = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' 'res' '1234567890abc')
$caseSpecs['publisher_id|explicit_12_chars']          = New-InvalidParamSpec -naLen $false
$caseSpecs['publisher_id|explicit_14_chars']          = New-InvalidParamSpec -naLen $false
$caseSpecs['publisher_id|both_publisher_and_id_null'] = New-InvalidParamSpec -naLen $false

# 5. Architecture cases (TestPackage 1.0.0.0 <archToken> res 1234567890abc)
$caseSpecs['architecture|arch_intel']   = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x86' 'res' '1234567890abc')
$caseSpecs['architecture|arch_amd64']   = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'x64' 'res' '1234567890abc')
$caseSpecs['architecture|arch_arm']     = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'arm' 'res' '1234567890abc')
$caseSpecs['architecture|arch_arm64']   = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'arm64' 'res' '1234567890abc')
$caseSpecs['architecture|arch_neutral'] = New-SuccessSpec -fullName (Format-PackageFullName 'TestPackage' '1.0.0.0' 'neutral' 'res' '1234567890abc')
$caseSpecs['architecture|arch_invalid'] = New-InvalidParamSpec -naLen $false

# 6. Parameter validation cases
$caseSpecs['parameters|null_id']     = New-InvalidParamSpec -naLen $false
$caseSpecs['parameters|null_length'] = New-InvalidParamSpec -naLen $true
$caseSpecs['parameters|null_name']   = New-InvalidParamSpec -naLen $false
$caseSpecs['parameters|empty_name']  = New-InvalidParamSpec -naLen $false

$expectedCases = @($caseSpecs.Keys)

$errors = @()
function AddError([string]$msg) { $script:errors += $msg }

function IsInt32Scalar($v) {
    if ($null -eq $v) { return $false }
    if ($v -is [bool] -or $v -is [string]) { return $false }
    if ($v -is [int] -or $v -is [long]) {
        $n = [long]$v
        return ($n -ge [long][int32]::MinValue -and $n -le [long][int32]::MaxValue)
    }
    return $false
}

function IsUInt32Scalar($v) {
    if ($null -eq $v) { return $false }
    if ($v -is [bool] -or $v -is [string]) { return $false }
    if ($v -is [int] -or $v -is [long]) {
        $n = [long]$v
        return ($n -ge 0L -and $n -le [long][uint32]::MaxValue)
    }
    return $false
}

$records = @()
foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file)) {
        AddError "file not found: $file"
        continue
    }
    $lines = @(Get-Content -LiteralPath $file)
    $lineNo = 0
    foreach ($line in $lines) {
        $lineNo++
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $obj = $null
        try {
            $obj = $line | ConvertFrom-Json
        } catch {
            AddError ("{0}:{1} malformed JSON: {2}" -f $file, $lineNo, $_.Exception.Message)
            continue
        }
        if ($null -eq $obj -or $obj -isnot [pscustomobject]) {
            AddError ("{0}:{1} JSON line is not an object" -f $file, $lineNo)
            continue
        }
        $records += [pscustomobject]@{
            File = $file
            Line = $lineNo
            Raw  = $obj
        }
    }
}

if ($records.Count -eq 0) {
    AddError 'no records found in the supplied files'
}

# Per-record type and independent conformance validation
foreach ($rec in $records) {
    $r = $rec.Raw
    $where = "$($rec.File):$($rec.Line)"

    $propNames = @($r.PSObject.Properties.Name)
    $missing = @()
    foreach ($f in $requiredFields) {
        if ($propNames -notcontains $f) {
            $missing += $f
        }
    }
    if ($missing.Count -gt 0) {
        AddError "$where missing field(s): $($missing -join ', ')"
        continue
    }
    $extra = @()
    foreach ($p in $propNames) {
        if ($requiredFields -notcontains $p) {
            $extra += $p
        }
    }
    if ($extra.Count -gt 0) {
        AddError "$where unexpected extra field(s): $($extra -join ', ')"
    }

    # Enforce real string types
    $stringTypeOk = $true
    foreach ($sf in $stringFields) {
        $val = $r.$sf
        if ($null -eq $val -or $val -isnot [string]) {
            AddError "$where field '$sf' must be a string (got $(if ($null -eq $val) { 'null' } else { $val.GetType().Name }))"
            $stringTypeOk = $false
        } elseif ($nonEmptyStringFields -contains $sf -and [string]::IsNullOrWhiteSpace($val)) {
            AddError "$where field '$sf' must be a non-empty string"
            $stringTypeOk = $false
        }
    }
    if (-not $stringTypeOk) {
        continue
    }

    if ($r.schema -cne $schema) {
        AddError "$where unsupported schema '$($r.schema)' (expected '$schema')"
    }
    if ($architectures -notcontains $r.arch) {
        AddError "$where unexpected architecture '$($r.arch)'"
    }
    if (-not (IsInt32Scalar $r.ret)) {
        AddError "$where ret must be an INT32 integer scalar (got '$($r.ret)' of type $(if ($null -eq $r.ret) { 'null' } else { $r.ret.GetType().Name }))"
    }
    if ($r.sentinel_checked -isnot [bool]) {
        AddError "$where sentinel_checked must be a boolean"
    }
    if ($r.sentinel_intact -isnot [bool]) {
        AddError "$where sentinel_intact must be a boolean"
    }

    $caseKey = "$($r.category)|$($r.test)"
    if (-not $caseSpecs.Contains($caseKey)) {
        AddError "$where unrecognized test case '$caseKey'"
        continue
    }

    $spec = $caseSpecs[$caseKey]

    # Independent return code check
    if ((IsInt32Scalar $r.ret) -and ([long]$r.ret -ne $spec.ret)) {
        AddError "$where case $caseKey ret=$($r.ret) != expected $($spec.ret)"
    }

    # Independent input/output length type, UINT32 bound, and value checks
    if ($spec.na_len) {
        if ($r.in_len -isnot [string] -or $r.in_len -cne 'NA') {
            AddError "$where case $caseKey in_len must be string 'NA' for missing length pointer (got '$($r.in_len)' of type $(if ($null -eq $r.in_len) { 'null' } else { $r.in_len.GetType().Name }))"
        }
        if ($r.out_len -isnot [string] -or $r.out_len -cne 'NA') {
            AddError "$where case $caseKey out_len must be string 'NA' for missing length pointer (got '$($r.out_len)' of type $(if ($null -eq $r.out_len) { 'null' } else { $r.out_len.GetType().Name }))"
        }
    } else {
        if (-not (IsUInt32Scalar $r.in_len)) {
            AddError "$where case $caseKey in_len must be a UINT32 integer scalar in [0..4294967295] (got '$($r.in_len)' of type $(if ($null -eq $r.in_len) { 'null' } else { $r.in_len.GetType().Name }))"
        } elseif ([long]$r.in_len -ne $spec.in_len) {
            AddError "$where case $caseKey in_len=$($r.in_len) != expected $($spec.in_len)"
        }

        if (-not (IsUInt32Scalar $r.out_len)) {
            AddError "$where case $caseKey out_len must be a UINT32 integer scalar in [0..4294967295] (got '$($r.out_len)' of type $(if ($null -eq $r.out_len) { 'null' } else { $r.out_len.GetType().Name }))"
        } elseif ([long]$r.out_len -ne $spec.out_len) {
            AddError "$where case $caseKey out_len=$($r.out_len) != expected $($spec.out_len)"
        }
    }

    # Independent result string and expected reference string checks
    if ($r.str -cne $spec.str) {
        AddError "$where case $caseKey str '$($r.str)' != independently expected '$($spec.str)'"
    }
    if ($r.expected -cne $spec.expected) {
        AddError "$where case $caseKey record.expected '$($r.expected)' != independently expected '$($spec.expected)'"
    }
    if ($spec.ret -eq 0 -and ($r.str.Length + 1) -ne $spec.out_len) {
        AddError "$where case $caseKey str length + 1 ($($r.str.Length + 1)) != expected out_len $($spec.out_len)"
    }

    # Independent checked-sentinel semantic checks
    if ($r.sentinel_checked -is [bool] -and $r.sentinel_checked -ne $spec.sentinel_checked) {
        AddError "$where case $caseKey sentinel_checked=$($r.sentinel_checked) != expected $($spec.sentinel_checked)"
    }
    if ($r.sentinel_intact -is [bool] -and $r.sentinel_intact -ne $spec.sentinel_intact) {
        AddError "$where case $caseKey sentinel_intact=$($r.sentinel_intact) != expected $($spec.sentinel_intact)"
    }
}

# Duplicate detection
$seen = @{}
foreach ($rec in $records) {
    $r = $rec.Raw
    if ($r.arch -isnot [string] -or $r.category -isnot [string] -or $r.test -isnot [string]) { continue }
    $key = "$($r.arch)|$($r.category)|$($r.test)"
    if ($seen.ContainsKey($key)) {
        AddError "$($rec.File):$($rec.Line) duplicate record for $key (first at $($seen[$key]))"
    } else {
        $seen[$key] = "$($rec.File):$($rec.Line)"
    }
}

# Architecture presence and per-architecture completeness
$presentArchs = @($records | ForEach-Object { $_.Raw.arch } | Where-Object { $_ -is [string] } | Sort-Object -Unique)
foreach ($a in $architectures) {
    if ($presentArchs -notcontains $a) {
        AddError "architecture '$a' has no records (omitted architecture)"
    }
}

foreach ($a in $presentArchs) {
    if ($architectures -notcontains $a) { continue }
    $byCase = @{}
    foreach ($rec in $records) {
        $r = $rec.Raw
        if ($r.arch -ne $a -or $r.category -isnot [string] -or $r.test -isnot [string]) { continue }
        $key = "$($r.category)|$($r.test)"
        $byCase[$key] = $rec
    }
    foreach ($c in $expectedCases) {
        if (-not $byCase.ContainsKey($c)) {
            AddError "[$a] omitted test case $c"
        }
    }
    foreach ($k in $byCase.Keys) {
        if ($expectedCases -notcontains $k) {
            AddError "[$a] unexpected test case $k"
        }
    }
}

Write-Host "records: $($records.Count)  architectures: $($presentArchs -join ', ')"
if ($errors.Count -eq 0) {
    Write-Host 'VALID: all records present, unique, well-formed and independently conformant'
    exit 0
}
Write-Host "INVALID: $($errors.Count) violation(s)"
$errors | ForEach-Object { Write-Host "  $_" }
exit 1
