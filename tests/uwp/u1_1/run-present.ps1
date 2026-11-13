# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Oracle run of the U1.1b presentation and input probe (application ViewPresent) on Windows: install the signed
# package made by package.ps1, activate ViewPresent through the U1.0 launcher, wait for the READY line the probe
# writes once it has presented frames, inject mouse and keyboard input into the foreground window, collect the output
# and remove the package. The certificate must already be trusted (not done here). The measurement is the output
# (present.txt); nothing is compared yet. The machine's mouse is moved and clicked and keys are typed: do not use the
# machine during the run (about 25 seconds).
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory, [switch]$Execute)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
$results = [IO.Path]::GetFullPath($ResultsDirectory)
if ($results.StartsWith($work.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $results -eq $work) { throw 'ResultsDirectory must be outside WorkDirectory.' }
Write-Host 'Add-AppxPackage, ActivateApplication (launcher.exe) for ViewPresent, inject input, read the output, Remove-AppxPackage.'
if (-not $Execute) { return }
if (Test-Path -LiteralPath $results) { throw 'ResultsDirectory must not already exist.' }
$state = Get-Content -LiteralPath (Join-Path $work 'state.json') -Raw | ConvertFrom-Json
if ($state.WorkDirectory -ne $work) { throw 'Work directory does not match package state.' }
if (Get-AppxPackage -Name $state.Name) { throw 'A package with this name is already installed for this user.' }
New-Item -ItemType Directory -Path $results | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class U1Input {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION u; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder text, int max);
    delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder text, int max);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(int x, int y);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    /* the visible CoreWindow of the process with this image name (the probe's own window) */
    public static IntPtr FindCoreWindow(string process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var c = new System.Text.StringBuilder(256);
            GetClassName(h, c, 256);
            if (c.ToString() != "Windows.UI.Core.CoreWindow") return true;
            uint pid; GetWindowThreadProcessId(h, out pid);
            try { if (System.Diagnostics.Process.GetProcessById((int)pid).ProcessName == process) { found = h; return false; } } catch { }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static void Key(ushort vk, bool up, bool extended) {
        var i = new INPUT[1]; i[0].type = 1; i[0].u.ki.wVk = vk;
        i[0].u.ki.dwFlags = (up ? 2u : 0u) | (extended ? 1u : 0u);
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Mouse(uint flags, uint data) {
        var i = new INPUT[1]; i[0].type = 0; i[0].u.mi.dwFlags = flags; i[0].u.mi.mouseData = data;
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@

function Tap([int]$vk, [switch]$extended) { [U1Input]::Key($vk, $false, [bool]$extended); Start-Sleep -Milliseconds 80; [U1Input]::Key($vk, $true, [bool]$extended); Start-Sleep -Milliseconds 150 }

$launcher = Join-Path $PSScriptRoot "..\u1_0\build\$($state.Architecture)\launcher.exe"
$installed = $null
try {
    Add-AppxPackage -Path (Join-Path $work 'view.appx') -ErrorAction Stop
    $matches = @(Get-AppxPackage -Name $state.Name)
    if ($matches.Count -ne 1) { throw 'Cannot identify the installed package uniquely.' }
    $installed = $matches[0]
    $folder = Join-Path $env:LOCALAPPDATA "Packages\$($installed.PackageFamilyName)\AC"
    $file = Join-Path $folder 'view-present.txt'
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    & $launcher ($installed.PackageFamilyName + '!ViewPresent') present 'unused' $installed.PackageFullName 2>&1 | ForEach-Object { Write-Host $_ }
    $deadline = (Get-Date).AddSeconds(20)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path -LiteralPath $file) -and (Select-String -LiteralPath $file -Pattern '^READY' -Quiet)) { $ready = $true; break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) { Write-Host 'present: no READY line' }
    else {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $screen = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bitmap = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($screen.Left, $screen.Top, 0, 0, $bitmap.Size)
        $bitmap.Save((Join-Path $results 'screen-at-ready.png'))
        $graphics.Dispose(); $bitmap.Dispose()
        # find the probe's window; a click on it brings it to the front. Never type into some other program: inject
        # only when the probe's window is what is under the click point and then in front.
        # the window has no title; the probe reports where it is (Bounds), and what is at the centre of that must be
        # an application frame window of that rectangle
        $line = Select-String -LiteralPath $file -Pattern '^INFO bounds now (\d+)x(\d+) at (-?\d+),(-?\d+)' | Select-Object -Last 1
        if (-not $line) { Write-Host 'present: no bounds reported, no input injected'; $ready = $false }
        else {
            $g = $line.Matches[0].Groups
            $cx = [int]$g[3].Value + [int]([int]$g[1].Value / 2); $cy = [int]$g[4].Value + [int]([int]$g[2].Value / 2)
            $hwnd = [U1Input]::GetAncestor([U1Input]::WindowFromPoint($cx, $cy), 2)
            $class = New-Object System.Text.StringBuilder 256
            [void][U1Input]::GetClassName($hwnd, $class, 256)
            "centre $cx,$cy window class '$($class.ToString())'" | Set-Content -LiteralPath (Join-Path $results 'injected.txt')
            if ($class.ToString() -ne 'ApplicationFrameWindow') { Write-Host "present: '$($class.ToString())' is at the probe's centre, no input injected"; $ready = $false }
        }
    }
    if ($ready) {
        [void][U1Input]::SetCursorPos($cx, $cy); Start-Sleep -Milliseconds 300
        [U1Input]::Mouse(0x0002, 0); Start-Sleep -Milliseconds 100; [U1Input]::Mouse(0x0004, 0); Start-Sleep -Milliseconds 800   # activating click
        $front = [U1Input]::GetForegroundWindow()
        $class = New-Object System.Text.StringBuilder 256
        [void][U1Input]::GetClassName($front, $class, 256)
        if ($class.ToString() -ne 'ApplicationFrameWindow' -or $front -ne $hwnd) { Write-Host "present: foreground is '$($class.ToString())' after the click, no input injected"; $ready = $false }
    }
    if ($ready) {
        [void][U1Input]::SetCursorPos($cx, $cy); Start-Sleep -Milliseconds 300
        [void][U1Input]::SetCursorPos($cx + 40, $cy + 20); Start-Sleep -Milliseconds 300
        [U1Input]::Mouse(0x0002, 0); Start-Sleep -Milliseconds 150; [U1Input]::Mouse(0x0004, 0); Start-Sleep -Milliseconds 300     # left down, up
        [U1Input]::Mouse(0x0008, 0); Start-Sleep -Milliseconds 150; [U1Input]::Mouse(0x0010, 0); Start-Sleep -Milliseconds 300     # right down, up
        [U1Input]::Mouse(0x0800, 120); Start-Sleep -Milliseconds 300                                                              # wheel up
        Tap 0x41                                                                                                                 # a
        [U1Input]::Key(0x10, $false, $false); Tap 0x42; [U1Input]::Key(0x10, $true, $false); Start-Sleep -Milliseconds 150       # Shift+b
        Tap 0x0D                                                                                                                 # Enter
        Tap 0x25 -extended                                                                                                       # Left
        Tap 0x74                                                                                                                 # F5
        "mouse: move, left click, right click, wheel up; keys: a, Shift+b, Enter, Left, F5" | Add-Content -LiteralPath (Join-Path $results 'injected.txt')
    }
    # the probe ends 12 s after READY
    $end = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $end -and -not (Select-String -LiteralPath $file -Pattern 'Run returning|WATCHDOG|Uninitialize' -Quiet)) { Start-Sleep -Milliseconds 500 }
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $results 'present.txt') } else { Write-Host "present: no output in $folder" }
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop }
}
