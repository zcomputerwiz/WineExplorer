# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Black-box probe of an undocumented export of the Windows library profapi.dll, ordinal 114: what does it take and return?
# Windows App SDK's FrameworkUdk calls it right after PackageFamilyNameFromFullName("Microsoft.WindowsNotepad_..._8wekyb3d8bbwe")
# gave "Microsoft.WindowsNotepad_8wekyb3d8bbwe". Each candidate call runs in its own child process (a wrong guess can crash
# only that process). Nothing is written but the result file. usage: powershell -ExecutionPolicy Bypass -File profapi114-probe.ps1
param([string]$Out = (Join-Path $PSScriptRoot 'profapi114-result.txt'), [int]$Ordinal = 114, [string]$Child = '')

$family = 'Microsoft.WindowsNotepad_8wekyb3d8bbwe'
$full = 'Microsoft.WindowsNotepad_11.2609.11.0_x64__8wekyb3d8bbwe'

$native = @'
using System;
using System.Runtime.InteropServices;
public static class N {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryExW(string n, IntPtr f, uint flags);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr GetProcAddress(IntPtr h, IntPtr ord);
  [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr F0();
  [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr F1(IntPtr a);
  [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr F2(IntPtr a, IntPtr b);
  [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr F3(IntPtr a, IntPtr b, IntPtr c);
  [UnmanagedFunctionPointer(CallingConvention.Winapi)] public delegate IntPtr F4(IntPtr a, IntPtr b, IntPtr c, IntPtr d);
}
'@

function Get-Proc {
    Add-Type -TypeDefinition $native -ErrorAction SilentlyContinue
    $h = [N]::LoadLibraryExW("$env:SystemRoot\System32\profapi.dll", [IntPtr]::Zero, 0)
    if ($h -eq [IntPtr]::Zero) { 'LoadLibrary failed, error ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error(); return $null }
    $p = [N]::GetProcAddress($h, [IntPtr]$Ordinal)
    if ($p -eq [IntPtr]::Zero) { "no export with ordinal $Ordinal"; return $null }
    return $p
}

function Show-Ptr($v) {
    $s = '0x{0:x}' -f $v.ToInt64()
    try {
        $t = [Runtime.InteropServices.Marshal]::PtrToStringUni($v)
        if ($t -and $t.Length -gt 1 -and $t.Length -lt 400 -and $t -match '^[\x20-\x7e]+$') { $s += "  (wide string at that address: '$t')" }
    } catch {}
    $s
}

if ($Child) {
    $p = Get-Proc
    if ($p -isnot [IntPtr]) { "FATAL: $p"; exit 90 }
    $m = [Runtime.InteropServices.Marshal]
    $sf = $m::StringToHGlobalUni($family); $sfull = $m::StringToHGlobalUni($full)
    $buf = $m::AllocHGlobal(8192); for ($i = 0; $i -lt 8192; $i += 8) { $m::WriteInt64($buf, $i, 0) }
    $len = $m::AllocHGlobal(16); $m::WriteInt32($len, 0, 260); $m::WriteInt32($len, 4, 0)
    $outp = $m::AllocHGlobal(16); $m::WriteInt64($outp, 0, 0)
    $z = [IntPtr]::Zero
    switch ($Child) {
        'none'        { $r = $m::GetDelegateForFunctionPointer($p, [N+F0]).Invoke() }
        'family'      { $r = $m::GetDelegateForFunctionPointer($p, [N+F1]).Invoke($sf) }
        'full'        { $r = $m::GetDelegateForFunctionPointer($p, [N+F1]).Invoke($sfull) }
        'family,&out' { $r = $m::GetDelegateForFunctionPointer($p, [N+F2]).Invoke($sf, $outp) }
        'family,buf'  { $r = $m::GetDelegateForFunctionPointer($p, [N+F2]).Invoke($sf, $buf) }
        'family,buf,&len' { $r = $m::GetDelegateForFunctionPointer($p, [N+F3]).Invoke($sf, $buf, $len) }
        'family,buf,len'  { $r = $m::GetDelegateForFunctionPointer($p, [N+F3]).Invoke($sf, $buf, [IntPtr]260) }
        'family,0,buf,&len' { $r = $m::GetDelegateForFunctionPointer($p, [N+F4]).Invoke($sf, $z, $buf, $len) }
        'zero,buf,&len'   { $r = $m::GetDelegateForFunctionPointer($p, [N+F3]).Invoke($z, $buf, $len) }
        default       { "unknown child $Child"; exit 91 }
    }
    'return value: ' + (Show-Ptr $r)
    'last Win32 error: ' + $m::GetLastWin32Error()
    'out pointer cell: ' + (Show-Ptr ($m::ReadIntPtr($outp)))
    'buffer as wide string: [' + $m::PtrToStringUni($buf) + ']'
    'length cell: ' + $m::ReadInt32($len, 0) + ' / ' + $m::ReadInt32($len, 4)
    exit 0
}

$cases = 'none', 'family', 'full', 'family,&out', 'family,buf', 'family,buf,&len', 'family,buf,len', 'family,0,buf,&len', 'zero,buf,&len'
$report = @()
$report += "profapi.dll ordinal $Ordinal probe, " + (Get-Date -Format s) + ", " + [Environment]::OSVersion.VersionString
$report += (Get-Item "$env:SystemRoot\System32\profapi.dll").VersionInfo.FileVersion
foreach ($c in $cases) {
    $report += ''
    $report += "=== arguments: ($c)"
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = (Get-Process -Id $PID).Path
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Ordinal $Ordinal -Child `"$c`""
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $pr = [Diagnostics.Process]::Start($psi)
    if (-not $pr.WaitForExit(30000)) { $pr.Kill(); $report += 'timed out (30 s)'; continue }
    $report += $pr.StandardOutput.ReadToEnd().TrimEnd()
    $e = $pr.StandardError.ReadToEnd().TrimEnd(); if ($e) { $report += "stderr: $e" }
    $report += ('exit code: 0x{0:x}' -f $pr.ExitCode)
}
$report | Set-Content -Path $Out -Encoding UTF8
"Done. Result file: $Out"
