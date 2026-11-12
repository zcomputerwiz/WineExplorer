# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-R0B build/run driver for PackageFullNameFromId observation probe.
#
# Preview by default: prints toolchain, build and run commands.
# -Execute builds x64 and x86 with the static CRT (/MT) in a fresh output
# directory and runs each with a bounded wait, capturing JSONL records and hashes.
#
# Output directory policy (fail-closed; this driver never deletes):
#   - By default a fresh unique directory out-<yyyyMMdd-HHmmss>-<pid> is
#     created under this test directory.
#   - -OutDirectory must name a fresh (nonexistent) directory that is a
#     direct child of this test directory and is named out-* (so the local
#     .gitignore covers it). Filesystem roots, the source directory, the
#     project root, foreign destinations, nested destinations and non-out-*
#     names are refused, as are reparse points (symlinks/junctions) anywhere
#     in the destination's ancestor chain.
#   - Preexisting output state is refused and left untouched; this driver never
#     deletes or reuses an existing directory.
#
# Exit codes:
#   0 = OK (both architectures built and ran cleanly)
#   1 = a build or run did not succeed cleanly (or nonzero probe exit)
#   2 = toolchain unavailable
#   3 = output destination refused
#   4 = invalid parameter

[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$OutDirectory,
    [int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'

if (-not $env:PATHEXT -or -not ($env:PATHEXT -match '\.EXE')) {
    $env:PATHEXT = '.COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC;.CPL'
}

if ($TimeoutSeconds -le 0 -or $TimeoutSeconds -gt 300) {
    Write-Host "INVALID: -TimeoutSeconds must be a positive integer of at most 300 (got $TimeoutSeconds)"
    exit 4
}

$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$sdk = '10.0.26100.0'
$sdkInclude = "C:\Program Files (x86)\Windows Kits\10\Include\$sdk"
$srcRoot = $PSScriptRoot
$src = Join-Path $srcRoot 'package_name_probe.cpp'
$architectures = @('x64', 'x86')

$BuildTimeoutMs = 600000  # 10 min
$ToolTimeoutMs  = 30000   # 30 s bounded wait for tool banner query
$KillWaitMs     = 5000    # bounded wait for killed process
$ReadWaitMs     = 10000   # bounded wait for async pipe read

function Invoke-BoundedBanner([string]$cmdLine, [string]$workDir, [string]$bannerPattern) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = '/d /c ' + $cmdLine
    $psi.WorkingDirectory = $workDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $p = [System.Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    $done = $p.WaitForExit($ToolTimeoutMs)
    if (-not $done) {
        try { & taskkill.exe /F /T /PID $p.Id *>&1 | Out-Null } catch { }
        $p.WaitForExit($KillWaitMs) | Out-Null
        $outTask.Wait($ReadWaitMs) | Out-Null
        $errTask.Wait($ReadWaitMs) | Out-Null
        return [pscustomobject]@{ ok = $false; banner = 'TIMED_OUT' }
    }
    $outOk = $outTask.Wait($ReadWaitMs)
    $errOk = $errTask.Wait($ReadWaitMs)
    if (-not ($outOk -and $errOk)) {
        return [pscustomobject]@{ ok = $false; banner = 'READ_TIMED_OUT' }
    }
    $lines = @($errTask.Result, $outTask.Result) |
        Where-Object { -not [string]::IsNullOrEmpty($_) } |
        ForEach-Object { $_ -split "\r?\n" } |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne '' }
    $match = $lines | Where-Object { $_ -match $bannerPattern } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($match)) {
        return [pscustomobject]@{ ok = $false; banner = 'MISSING' }
    }
    return [pscustomobject]@{ ok = $true; banner = $match }
}

function Resolve-OutputDestination([string]$requested) {
    $result = [pscustomobject]@{ ok = $false; path = $null; reason = $null }
    if ([string]::IsNullOrWhiteSpace($requested)) {
        $result.reason = 'no output directory supplied'
        return $result
    }
    $full = [IO.Path]::GetFullPath($requested)
    $rootFull = [IO.Path]::GetFullPath($srcRoot)
    $normFull = $full.TrimEnd('\').ToUpperInvariant()
    $normRoot = $rootFull.TrimEnd('\').ToUpperInvariant()

    $driveRoot = [IO.Path]::GetPathRoot($full).TrimEnd('\')
    if ($normFull -eq $driveRoot.ToUpperInvariant()) {
        $result.reason = "filesystem root is not an allowed output destination: $full"
        return $result
    }
    if ($normFull -eq $normRoot) {
        $result.reason = "the source directory is not an allowed output destination: $full"
        return $result
    }
    if (-not $normFull.StartsWith($normRoot + '\')) {
        $result.reason = "output destination must be inside the test directory $rootFull (project root and foreign destinations are refused): $full"
        return $result
    }
    $parent = [IO.Path]::GetDirectoryName($full).TrimEnd('\').ToUpperInvariant()
    if ($parent -ne $normRoot) {
        $result.reason = "output destination must be a direct child of the test directory $rootFull (nested destinations are refused): $full"
        return $result
    }
    $leaf = [IO.Path]::GetFileName($full)
    if ($leaf -notlike 'out-*') {
        $result.reason = "output destination must be named out-* so the local .gitignore covers it: $full"
        return $result
    }
    if (Test-Path -LiteralPath $full) {
        $result.reason = "output destination already exists; this driver never deletes or reuses preexisting directories: $full"
        return $result
    }
    $ancestor = [IO.Path]::GetDirectoryName($full)
    while ($ancestor) {
        $item = Get-Item -LiteralPath $ancestor -Force -ErrorAction SilentlyContinue
        if ($null -ne $item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            $result.reason = "reparse point (symlink/junction) in the output destination's ancestor chain: $ancestor"
            return $result
        }
        $parent = [IO.Path]::GetDirectoryName($ancestor)
        if ($parent -eq $ancestor) { break }
        $ancestor = $parent
    }
    $result.ok = $true
    $result.path = $full
    return $result
}

if ($OutDirectory) {
    $dest = Resolve-OutputDestination $OutDirectory
} else {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dest = Resolve-OutputDestination (Join-Path $srcRoot "out-$stamp-$PID")
}
if (-not $dest.ok) {
    Write-Host "REFUSED: $($dest.reason)"
    exit 3
}
$out = $dest.path

$blockers = @()
if (-not (Test-Path $vs)) { $blockers += "vcvarsall.bat not found: $vs" }
if (-not (Test-Path $sdkInclude)) { $blockers += "SDK include root not found: $sdkInclude" }
if (-not (Test-Path $src)) { $blockers += "probe source not found: $src" }
if ($blockers.Count -gt 0) {
    Write-Host 'BLOCKED: toolchain unavailable; no fallback environment used.'
    $blockers | ForEach-Object { Write-Host "  $_" }
    exit 2
}

Write-Host "vcvarsall : $vs"
Write-Host "SDK       : $sdk ($sdkInclude)"
Write-Host "source    : $src"
Write-Host "output    : $out (fresh, gitignored; this driver never deletes)"
Write-Host "run bound : $TimeoutSeconds s per probe (kill wait $([int]($KillWaitMs/1000)) s, read wait $([int]($ReadWaitMs/1000)) s)"

foreach ($arch in $architectures) {
    $buildDir = Join-Path $out "build\$arch"
    Write-Host "[$arch] build in $buildDir :"
    Write-Host "  call `"$vs`" $arch $sdk && cl /nologo /std:c++17 /EHsc /MT `"$src`" /link /OUT:package_name_probe.exe kernel32.lib advapi32.lib bcrypt.lib"
    Write-Host "[$arch] run : $buildDir\package_name_probe.exe"
}

if (-not $Execute) {
    Write-Host 'Preview only. Re-run with -Execute to build and run.'
    exit 0
}

try {
    New-Item -ItemType Directory -Path $out | Out-Null
} catch {
    Write-Host "REFUSED: could not create the fresh output directory $out : $_"
    exit 3
}

try {
    $os = Get-CimInstance Win32_OperatingSystem
    $reg = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction SilentlyContinue
    $ubr = if ($null -ne $reg -and $null -ne $reg.UBR) { [int]$reg.UBR } else { $null }
    $fullBuild = if ($null -ne $ubr) { "$($os.BuildNumber).$ubr" } else { $os.BuildNumber }
    $osInfo = [ordered]@{
        caption         = $os.Caption
        version         = $os.Version
        build           = $os.BuildNumber
        ubr             = $ubr
        full_build      = $fullBuild
        display_version = if ($null -ne $reg) { $reg.DisplayVersion } else { $null }
    }
} catch {
    $osInfo = [ordered]@{ caption = [Environment]::OSVersion.Platform.ToString(); error = "$_" }
}

$srcHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $src).Hash

$manifest = [ordered]@{
    schema          = 'wex-uwp-package-name-run/1'
    date            = (Get-Date).ToString('o')
    os              = $osInfo
    tools           = [ordered]@{ vcvars = $vs; sdk = $sdk }
    source          = $src
    source_sha256   = $srcHash
    timeout_seconds = $TimeoutSeconds
    out_directory   = $out
    architectures   = [ordered]@{}
}

$allOk = $true
foreach ($arch in $architectures) {
    $buildDir = Join-Path $out "build\$arch"
    try {
        New-Item -ItemType Directory -Path $buildDir | Out-Null
    } catch {
        Write-Host "[$arch] REFUSED: could not create build directory $buildDir : $_"
        $allOk = $false
        $manifest.architectures[$arch] = [ordered]@{
            build_exit = $null; built = $false; error = "could not create build directory"
        }
        continue
    }

    $exe      = Join-Path $buildDir 'package_name_probe.exe'
    $buildLog = Join-Path $out "build_$arch.log"
    $recFile  = Join-Path $out "records_$arch.jsonl"
    $errFile  = Join-Path $out "stderr_$arch.txt"

    $clPattern   = '^Microsoft \(R\) C/C\+\+ Optimizing Compiler Version \d+(\.\d+)+'
    $linkPattern = '^Microsoft \(R\) Incremental Linker Version \d+(\.\d+)+'
    $clLine      = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' >nul 2>&1 && cl /?'
    $clRes       = Invoke-BoundedBanner $clLine $buildDir $clPattern
    $clVersion   = $clRes.banner
    $linkLine    = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' >nul 2>&1 && link /?'
    $linkRes     = Invoke-BoundedBanner $linkLine $buildDir $linkPattern
    $linkVersion = $linkRes.banner
    $bannerOk    = ($clRes.ok -and $linkRes.ok)

    $buildLine = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' && cl /nologo /std:c++17 /EHsc /MT "' + $src + '" /link /OUT:package_name_probe.exe kernel32.lib advapi32.lib bcrypt.lib'
    $buildLineLogged = '( ' + $buildLine + ' ) > "' + $buildLog + '" 2>&1'
    $buildPsi = New-Object System.Diagnostics.ProcessStartInfo
    $buildPsi.FileName = 'cmd.exe'
    $buildPsi.Arguments = '/d /c ' + $buildLineLogged
    $buildPsi.WorkingDirectory = $buildDir
    $buildPsi.UseShellExecute = $false
    $buildPsi.CreateNoWindow = $true

    $buildProc = [System.Diagnostics.Process]::Start($buildPsi)
    $buildExited = $buildProc.WaitForExit($BuildTimeoutMs)
    $buildTimedOut = -not $buildExited
    if ($buildTimedOut) {
        try { & taskkill.exe /F /T /PID $buildProc.Id *>&1 | Out-Null } catch { }
        $buildProc.WaitForExit($KillWaitMs) | Out-Null
        $buildExit = -1
    } else {
        $buildExit = $buildProc.ExitCode
    }

    $built = (Test-Path $exe) -and ($buildExit -eq 0)
    if (-not $built) {
        Write-Host "[$arch] BUILD FAILED (exit $buildExit timed_out=$buildTimedOut); log: $buildLog"
        $allOk = $false
        $manifest.architectures[$arch] = [ordered]@{
            cl = $clVersion; link = $linkVersion
            build_exit = $buildExit; build_timed_out = $buildTimedOut
            built = $false
        }
        continue
    }

    # Bounded execution with concurrent asynchronous pipe reads
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.WorkingDirectory = $buildDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    $exited = $proc.WaitForExit($TimeoutSeconds * 1000)
    $timedOut = -not $exited
    $killUnconfirmed = $false
    if ($timedOut) {
        try { $proc.Kill() } catch { }
        if (-not $proc.WaitForExit($KillWaitMs)) { $killUnconfirmed = $true }
    }
    $stdoutDone = $stdoutTask.Wait($ReadWaitMs)
    $stderrDone = $stderrTask.Wait($ReadWaitMs)
    $stdoutText = if ($stdoutDone) { $stdoutTask.Result } else { $null }
    $stderrText = if ($stderrDone) { $stderrTask.Result } else { $null }
    $runExit = if ($proc.HasExited) { $proc.ExitCode } else { $null }

    if ($null -ne $stdoutText) {
        [System.IO.File]::WriteAllText($recFile, $stdoutText, [System.Text.Encoding]::UTF8)
    }
    if ($null -ne $stderrText) {
        [System.IO.File]::WriteAllText($errFile, $stderrText, [System.Text.Encoding]::UTF8)
    }

    $exeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $exe).Hash
    $recHash = if (Test-Path -LiteralPath $recFile) { (Get-FileHash -Algorithm SHA256 -LiteralPath $recFile).Hash } else { $null }
    $recCount = if (Test-Path -LiteralPath $recFile) { (Get-Content -LiteralPath $recFile | Where-Object { $_.Trim() }).Count } else { 0 }

    Write-Host "[$arch] cl           : $clVersion"
    Write-Host "[$arch] link         : $linkVersion"
    Write-Host "[$arch] build        : exit $buildExit"
    Write-Host "[$arch] run          : exit $runExit timed_out=$timedOut kill_unconfirmed=$killUnconfirmed records=$recCount"
    Write-Host "[$arch] exe_sha256   : $exeHash"
    Write-Host "[$arch] rec_sha256   : $recHash"

    $manifest.architectures[$arch] = [ordered]@{
        cl               = $clVersion
        link             = $linkVersion
        build_command    = $buildLine
        build_exit       = $buildExit
        build_timed_out  = $buildTimedOut
        built            = $true
        exe              = "build\$arch\package_name_probe.exe"
        exe_sha256       = $exeHash
        run_command      = $exe
        run_exit         = $runExit
        timed_out        = $timedOut
        kill_unconfirmed = $killUnconfirmed
        read_complete    = ($stdoutDone -and $stderrDone)
        records          = "records_$arch.jsonl"
        records_sha256   = $recHash
        record_count     = $recCount
        stderr           = "stderr_$arch.txt"
    }

    # Nonzero exit, timeout, incomplete read, or missing/timed-out tool banner fails the driver
    if ($runExit -ne 0 -or $timedOut -or $killUnconfirmed -or -not ($stdoutDone -and $stderrDone) -or -not $bannerOk) {
        Write-Host "[$arch] RUN FAILED: exit=$runExit timed_out=$timedOut kill_unconfirmed=$killUnconfirmed read_complete=$($stdoutDone -and $stderrDone) banner_ok=$bannerOk"
        $allOk = $false
    }
}

$manifestPath = Join-Path $out 'manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "manifest: $manifestPath"

if (-not $allOk) {
    Write-Host 'RESULT: FAIL (a build or run did not succeed cleanly)'
    exit 1
}
Write-Host 'RESULT: OK (both architectures built and ran cleanly within the bound)'
exit 0
