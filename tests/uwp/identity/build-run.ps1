# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1-M2 build/run driver for the unpackaged package identity observation probe.
#
# Preview by default: prints the exact toolchain, build and run commands.
# -Execute builds x64 and x86 with the static CRT (/MT) in a fresh output
# directory and runs only these self-owned probes, each with a bounded wait,
# capturing the exit status, the raw JSONL records and their hashes.
#
# Output directory policy (fail-closed; this driver never deletes):
#   - By default a fresh unique directory out-<yyyyMMdd-HHmmss>-<pid> is
#     created under this test directory.
#   - -OutDirectory must name a fresh (nonexistent) directory that is a
#     direct child of this test directory and is named out-* (so the local
#     .gitignore covers it). Filesystem roots, the source directory, the
#     project root, foreign (outside-repo) destinations, nested destinations
#     and non-out-* names are refused, as are reparse points (symlinks/
#     junctions) anywhere in the destination's ancestor chain.
#   - Preexisting output state (including the captured out/ evidence) is
#     refused and left untouched; this driver never deletes or reuses an
#     existing directory.
#
# It uses the installed VS 2022 Build Tools and Windows SDK named by
# tests/uwp/u1_0/build.ps1 and BUILD_CHECK.md as the starting point, verifies
# the actual tools and records their versions. It performs no trust-store,
# certificate, package-installation or policy change, and it touches nothing
# outside its own fresh output directory. If the compiler or a run is
# unavailable it records the exact blocker and exits nonzero rather than
# installing tools or changing global settings.
#
# Exit codes: 0 = OK; 1 = a build or run did not succeed cleanly;
# 2 = toolchain unavailable; 3 = output destination refused;
# 4 = invalid parameter.
[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$OutDirectory,
    [int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'

# --- Validate the run bound: a finite positive sensible integer. ---
if ($TimeoutSeconds -le 0 -or $TimeoutSeconds -gt 300) {
    Write-Host "INVALID: -TimeoutSeconds must be a positive integer of at most 300 (got $TimeoutSeconds)"
    exit 4
}

$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
$sdk = '10.0.26100.0'
$sdkInclude = "C:\Program Files (x86)\Windows Kits\10\Include\$sdk"
$srcRoot = $PSScriptRoot
$src = Join-Path $srcRoot 'identity_probe.cpp'
$architectures = @('x64', 'x86')

# Bounded waits (milliseconds) for the execute phase.
$BuildTimeoutMs = 600000  # a single-file MSVC build takes seconds; 10 min is generous
$KillWaitMs     = 5000    # bounded wait for a killed process to actually exit
$ReadWaitMs     = 10000   # bounded wait for the probe's captured output

# --- Output destination policy (fail-closed; this driver never deletes). ---
# The destination must be a fresh (nonexistent) directory that is a direct
# child of this test directory and is named out-* (so the local .gitignore
# covers it). Filesystem roots, the source directory, the project root,
# foreign (outside-repo) destinations, nested destinations and non-out-*
# names are refused, as are reparse points (symlinks/junctions) in the
# destination's ancestor chain. Preexisting directories are refused, never
# deleted.
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
    # The destination must be a direct child of the test directory named
    # out-* so the local .gitignore (out-*/) covers it; nested or
    # differently named destinations are refused.
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
        if ($parent -eq $ancestor) { break }  # reached the volume root
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

# --- Verify the toolchain; record the exact blocker if unavailable. ---
$blockers = @()
if (-not (Test-Path $vs)) { $blockers += "vcvarsall.bat not found: $vs" }
if (-not (Test-Path $sdkInclude)) { $blockers += "SDK include root not found: $sdkInclude" }
if (-not (Test-Path $src)) { $blockers += "probe source not found: $src" }
if ($blockers.Count -gt 0) {
    Write-Host 'BLOCKED: toolchain unavailable; no fallback environment used.'
    $blockers | ForEach-Object { Write-Host "  $_" }
    exit 2
}

# --- Preview the exact commands. ---
Write-Host "vcvarsall : $vs"
Write-Host "SDK       : $sdk ($sdkInclude)"
Write-Host "source    : $src"
Write-Host "output    : $out (fresh, gitignored; this driver never deletes)"
Write-Host "run bound : $TimeoutSeconds s per probe (kill wait $([int]($KillWaitMs/1000)) s, read wait $([int]($ReadWaitMs/1000)) s)"
foreach ($arch in $architectures) {
    $buildDir = Join-Path $out "build\$arch"
    Write-Host "[$arch] build in $buildDir :"
    Write-Host "  call `"$vs`" $arch $sdk && cl /nologo /std:c++17 /EHsc /MT `"$src`" /link /OUT:identity_probe.exe kernel32.lib advapi32.lib windowsapp.lib"
    Write-Host "[$arch] run : $buildDir\identity_probe.exe"
}
if (-not $Execute) {
    Write-Host 'Preview only. Re-run with -Execute to build and run.'
    exit 0
}

# --- Create the fresh output directory (fail-closed; never delete). ---
try {
    New-Item -ItemType Directory -Path $out | Out-Null
} catch {
    Write-Host "REFUSED: could not create the fresh output directory $out : $_"
    exit 3
}

# --- Record the actual OS and tool versions. ---
try {
    $os = Get-CimInstance Win32_OperatingSystem
    $osInfo = [ordered]@{ caption = $os.Caption; version = $os.Version; build = $os.BuildNumber }
} catch {
    $osInfo = [ordered]@{ caption = [Environment]::OSVersion.Platform.ToString(); error = "$_" }
}

$manifest = [ordered]@{
    schema          = 'wex-uwp-identity-run/1'
    date            = (Get-Date).ToString('o')
    os              = $osInfo
    tools           = [ordered]@{ vcvars = $vs; sdk = $sdk }
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
        Write-Host "[$arch] REFUSED: could not create the build directory $buildDir : $_"
        $allOk = $false
        $manifest.architectures[$arch] = [ordered]@{
            build_exit = $null; built = $false; error = "could not create build directory"
        }
        continue
    }
    $exe      = Join-Path $buildDir 'identity_probe.exe'
    $buildLog = Join-Path $out "build_$arch.log"
    $recFile  = Join-Path $out "records_$arch.jsonl"
    $errFile  = Join-Path $out "stderr_$arch.txt"

    # Actual compiler and linker versions for this architecture.
    $clLine = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' >nul 2>&1 && cl /? 2>&1'
    $clVersion = (& cmd.exe /d /c $clLine) | Select-Object -First 1
    $linkLine = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' >nul 2>&1 && link /? 2>&1'
    $linkVersion = (& cmd.exe /d /c $linkLine) | Select-Object -First 1

    # Build with the static CRT. cmd redirects its own output to the log so
    # PowerShell never handles the native streams (a native stderr write would
    # otherwise surface as an ErrorRecord and trip $ErrorActionPreference).
    # The build is bounded: a build that cannot finish within the bound is
    # killed (its own process tree, targeted by PID) and recorded as a
    # failure, never a hang.
    $buildLine = 'call "' + $vs + '" ' + $arch + ' ' + $sdk + ' && cl /nologo /std:c++17 /EHsc /MT "' + $src + '" /link /OUT:identity_probe.exe kernel32.lib advapi32.lib windowsapp.lib'
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

    # Run only this self-owned probe, bounded, capturing exit and output. The
    # .NET Process API is used (rather than Start-Process -PassThru) because it
    # reliably reports ExitCode after a bounded wait. The kill wait and the
    # output reads are bounded as well: a probe that cannot be killed or whose
    # output cannot be read is a failed run, never a hang.
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.WorkingDirectory = $buildDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::ASCII
    $psi.StandardErrorEncoding = [System.Text.Encoding]::ASCII
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
        [System.IO.File]::WriteAllText($recFile, $stdoutText, [System.Text.Encoding]::ASCII)
    }
    if ($null -ne $stderrText) {
        [System.IO.File]::WriteAllText($errFile, $stderrText, [System.Text.Encoding]::ASCII)
    }

    $exeHash = (Get-FileHash -Algorithm SHA256 $exe).Hash
    $recHash = if (Test-Path $recFile) { (Get-FileHash -Algorithm SHA256 $recFile).Hash } else { $null }
    $recCount = if (Test-Path $recFile) { (Get-Content $recFile | Where-Object { $_.Trim() }).Count } else { 0 }

    Write-Host "[$arch] cl      : $clVersion"
    Write-Host "[$arch] link    : $linkVersion"
    Write-Host "[$arch] build   : exit $buildExit"
    Write-Host "[$arch] run     : exit $runExit timed_out=$timedOut kill_unconfirmed=$killUnconfirmed records=$recCount"
    Write-Host "[$arch] exe_sha256   : $exeHash"
    Write-Host "[$arch] rec_sha256   : $recHash"

    $manifest.architectures[$arch] = [ordered]@{
        cl               = $clVersion
        link             = $linkVersion
        build_exit       = $buildExit
        build_timed_out  = $buildTimedOut
        built            = $true
        exe              = "build\$arch\identity_probe.exe"
        exe_sha256       = $exeHash
        run_exit         = $runExit
        timed_out        = $timedOut
        kill_unconfirmed = $killUnconfirmed
        read_complete    = ($stdoutDone -and $stderrDone)
        records          = "records_$arch.jsonl"
        records_sha256   = $recHash
        record_count     = $recCount
        stderr           = "stderr_$arch.txt"
    }
    if ($runExit -ne 0 -or $timedOut -or $killUnconfirmed -or -not ($stdoutDone -and $stderrDone)) {
        $allOk = $false
    }
}

$manifestPath = Join-Path $out 'manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -Path $manifestPath -Encoding utf8
Write-Host "manifest: $manifestPath"

if (-not $allOk) {
    Write-Host 'RESULT: FAIL (a build or run did not succeed cleanly)'
    exit 1
}
Write-Host 'RESULT: OK (both architectures built and ran within the bound)'
exit 0
