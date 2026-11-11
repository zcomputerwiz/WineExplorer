# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-M2 results validator for the unpackaged package identity observation
# probe.
#
# Validates one or more captured JSONL record files (normally both
# records_x64.jsonl and records_x86.jsonl from a build-run.ps1 output
# directory). It rejects:
#   - malformed JSON lines,
#   - missing or wrong-typed required fields,
#   - an unsupported schema version,
#   - a missing or unexpected architecture,
#   - duplicate (architecture, API, handle) records,
#   - omitted or extra API/handle cases within an architecture,
#   - inconsistent unpackaged identity observations (any unexpectedly packaged
#     result, a Win32 case not observed as APPMODEL_ERROR_NO_PACKAGE, or the
#     RtlQueryPackageIdentity case not observed as STATUS_NOT_FOUND or an
#     explicit NOT-RUN for a missing/unverified declaration).
#
# The expected unpackaged values are the documented Windows contract, kept as
# parameters so an observed discrepancy is surfaced, not silently absorbed.
# Observed Windows values and prospective Wine assertions are separate: this
# validator checks the captured Windows observations only.
#
# Exit 0 = all records valid and consistent; 1 = one or more violations.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$Files,
    [int]$ExpectedWin32NoPackage = 15700,        # APPMODEL_ERROR_NO_PACKAGE
    [int]$ExpectedNtNoPackage = -1073741275      # STATUS_NOT_FOUND (0xC0000225)
)
$ErrorActionPreference = 'Stop'

$schema = 'wex-uwp-identity/1'
$architectures = @('x64', 'x86')
$expectedCases = @(
    'GetCurrentPackageFullName|current',
    'GetCurrentPackageFamilyName|current',
    'GetCurrentPackagePath|current',
    'GetPackageFullName|process_pseudo',
    'GetPackageFamilyName|process_pseudo',
    'GetPackageFullName|process_opened',
    'GetPackageFamilyName|process_opened',
    'GetPackageFullNameFromToken|token_opened',
    'GetPackageFamilyNameFromToken|token_opened',
    'RtlQueryPackageIdentity|token_opened'
)
$rtlApi = 'RtlQueryPackageIdentity'
$requiredFields = @('schema', 'arch', 'api', 'handle', 'status_domain',
                    'status_dec', 'status_hex', 'in_len', 'out_len',
                    'result', 'reason')

$errors = @()
function AddError([string]$msg) { $script:errors += $msg }

function IsIntValue($v) {
    if ($null -eq $v) { return $false }
    if ($v -is [int] -or $v -is [long]) { return $true }
    if ($v -is [double]) { return ([math]::Floor($v) -eq $v) }
    return $false
}

function HexToUInt32([string]$s) {
    if ($s -notmatch '^0x[0-9a-fA-F]{8}$') { return $null }
    return [Convert]::ToUInt32($s.Substring(2), 16)
}

# --- Load and parse every record. ---
$records = @()
foreach ($file in $Files) {
    if (-not (Test-Path $file)) {
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
        $rec = [pscustomobject]@{
            File   = $file
            Line   = $lineNo
            Raw    = $obj
        }
        $records += $rec
    }
}

if ($records.Count -eq 0) {
    AddError 'no records found in the supplied files'
}

# --- Per-record field, schema, domain and hex-consistency checks. ---
foreach ($rec in $records) {
    $r = $rec.Raw
    $where = "$($rec.File):$($rec.Line)"

    $missing = @()
    foreach ($f in $requiredFields) {
        if (-not ($r.PSObject.Properties.Name -contains $f)) {
            $missing += $f
        }
    }
    if ($missing.Count -gt 0) {
        AddError "$where missing field(s): $($missing -join ', ')"
        continue
    }

    if ($r.schema -ne $schema) {
        AddError "$where unsupported schema '$($r.schema)' (expected '$schema')"
    }
    if ($r.arch -notin $architectures) {
        AddError "$where unexpected architecture '$($r.arch)'"
    }
    if ($r.status_domain -ne 'win32' -and $r.status_domain -ne 'ntstatus') {
        AddError "$where bad status_domain '$($r.status_domain)'"
    }
    if (-not (IsIntValue $r.status_dec)) {
        AddError "$where status_dec is not an integer"
    }
    if (-not (IsIntValue $r.in_len) -or [long]$r.in_len -lt 0) {
        AddError "$where in_len is not a non-negative integer"
    }
    if (-not (IsIntValue $r.out_len) -or [long]$r.out_len -lt 0) {
        AddError "$where out_len is not a non-negative integer"
    }
    $u = HexToUInt32 ([string]$r.status_hex)
    if ($null -eq $u) {
        AddError "$where status_hex '$($r.status_hex)' is not 0x plus 8 hex digits"
    } elseif (IsIntValue $r.status_dec) {
        $asUInt32 = [uint32][int32][long]$r.status_dec
        if ($u -ne $asUInt32) {
            AddError "$where status_hex '$($r.status_hex)' inconsistent with status_dec $($r.status_dec)"
        }
    }
}

# --- Duplicate (arch, api, handle) detection. ---
$seen = @{}
foreach ($rec in $records) {
    $r = $rec.Raw
    $key = "$($r.arch)|$($r.api)|$($r.handle)"
    if ($seen.ContainsKey($key)) {
        AddError "$($rec.File):$($rec.Line) duplicate record for $key (first at $($seen[$key]))"
    } else {
        $seen[$key] = "$($rec.File):$($rec.Line)"
    }
}

# --- Architecture presence and per-architecture case completeness. ---
$presentArchs = @($records | ForEach-Object { $_.Raw.arch } | Sort-Object -Unique)
foreach ($a in $architectures) {
    if ($presentArchs -notcontains $a) {
        AddError "architecture '$a' has no records (omitted architecture)"
    }
}

foreach ($a in $presentArchs) {
    if ($a -notin $architectures) { continue }
    $byCase = @{}
    foreach ($rec in $records) {
        if ($rec.Raw.arch -ne $a) { continue }
        $key = "$($rec.Raw.api)|$($rec.Raw.handle)"
        if (-not $byCase.ContainsKey($key)) { $byCase[$key] = @() }
        $byCase[$key] += $rec
    }
    foreach ($case in $expectedCases) {
        if (-not $byCase.ContainsKey($case)) {
            AddError "[$a] omitted case $case"
        }
    }
    foreach ($key in $byCase.Keys) {
        if ($expectedCases -notcontains $key) {
            AddError "[$a] unexpected case $key"
        }
    }
}

# --- Consistent unpackaged identity observations. ---
foreach ($rec in $records) {
    $r = $rec.Raw
    $where = "$($rec.File):$($rec.Line)"
    if ($r.result -eq 'unexpected_packaged') {
        AddError "$where process unexpectedly has package identity (inconsistent unpackaged observation)"
        continue
    }
    if ($r.api -eq $rtlApi) {
        if ($r.status_domain -ne 'ntstatus') {
            AddError "$where RtlQueryPackageIdentity must use the ntstatus domain"
        }
        if ($r.result -eq 'no_package') {
            if ([long]$r.status_dec -ne $ExpectedNtNoPackage) {
                AddError "$where RtlQueryPackageIdentity no_package status $($r.status_dec) != expected $ExpectedNtNoPackage"
            }
        } elseif ($r.result -eq 'not_run') {
            if ($r.reason -ne 'declaration_unverified' -and $r.reason -ne 'export_missing') {
                AddError "$where RtlQueryPackageIdentity not_run reason '$($r.reason)' is not an accepted explicit NOT-RUN"
            }
        } else {
            AddError "$where RtlQueryPackageIdentity result '$($r.result)' is not no_package or not_run"
        }
    } else {
        if ($r.status_domain -ne 'win32') {
            AddError "$where $($r.api) must use the win32 domain"
        }
        if ($r.result -ne 'no_package') {
            AddError "$where $($r.api)/$($r.handle) result '$($r.result)' is not the consistent unpackaged observation"
        } elseif ([long]$r.status_dec -ne $ExpectedWin32NoPackage) {
            AddError "$where $($r.api)/$($r.handle) status $($r.status_dec) != expected $ExpectedWin32NoPackage"
        }
    }
}

# --- Report. ---
Write-Host "records: $($records.Count)  architectures: $($presentArchs -join ', ')"
if ($errors.Count -eq 0) {
    Write-Host 'VALID: all records present, unique, well-formed and consistently unpackaged'
    exit 0
}
Write-Host "INVALID: $($errors.Count) violation(s)"
$errors | ForEach-Object { Write-Host "  $_" }
exit 1
