# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Oracle run of the U1.1b presentation and input probe (application ViewPresent) on Windows: install the signed
# package made by package.ps1, activate ViewPresent through the U1.0 launcher, wait for the READY line the probe
# writes once it has presented frames, inject mouse and keyboard input into the foreground window, collect the output
# and remove the package. The certificate must already be trusted (not done here). The measurement is the output
# (present.txt); nothing is compared yet. The machine's mouse is moved and clicked and keys are typed: do not use the
# machine during the run (about 25 seconds).
[CmdletBinding()]
param([Parameter(Mandatory)][string]$WorkDirectory, [Parameter(Mandatory)][string]$ResultsDirectory, [ValidateSet('present','suspend','pkgapi')][string]$Mode = 'present', [switch]$Manual, [switch]$Execute)
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
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    public static System.Collections.Generic.List<string> Dump() {
        var list = new System.Collections.Generic.List<string>();
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var c = new System.Text.StringBuilder(256); GetClassName(h, c, 256);
            var t = new System.Text.StringBuilder(256); GetWindowText(h, t, 256);
            RECT r; GetWindowRect(h, out r);
            if (r.Right - r.Left > 800) list.Add(c + " [" + t + "] " + r.Left + "," + r.Top + " " + r.Right + "," + r.Bottom);
            return true;
        }, IntPtr.Zero);
        return list;
    }
    /* the visible application frame whose left edge is near x and whose client is as wide as the probe's bounds */
    public static IntPtr FindFrame(int x, int width) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var c = new System.Text.StringBuilder(256);
            GetClassName(h, c, 256);
            if (c.ToString() != "ApplicationFrameWindow") return true;
            RECT r; GetWindowRect(h, out r);
            if (Math.Abs(r.Left - x) <= 20 && Math.Abs((r.Right - r.Left) - width) <= 40) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
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
    /* an absolute move through SendInput (SetCursorPos does not produce pointer input for the application) */
    public static void MoveTo(int x, int y, int screenWidth, int screenHeight) {
        var i = new INPUT[1]; i[0].type = 0;
        i[0].u.mi.dx = (int)((x * 65535L) / (screenWidth - 1)); i[0].u.mi.dy = (int)((y * 65535L) / (screenHeight - 1));
        i[0].u.mi.dwFlags = 0x8001;
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
    $file = Join-Path $folder "view-$Mode.txt"
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    try { & $launcher ($installed.PackageFamilyName + '!View' + (Get-Culture).TextInfo.ToTitleCase($Mode)) $Mode 'unused' $installed.PackageFullName 2>&1 | ForEach-Object { Write-Host $_ } } catch { Write-Host "launcher: $($_.Exception.Message)" }
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
        # The composed window cannot be found with window enumeration or UI Automation, so the probe's own log is the
        # guard: the pointer is moved onto the bounds the probe reported, and only when the probe logged PointerEntered
        # for it is it clicked (a click brings the window to the front); only when it logged PointerPressed is anything
        # typed. Nothing is typed into another program.
        if ($Manual) { 'manual: the owner clicks into the window and types' | Set-Content -LiteralPath (Join-Path $results 'injected.txt'); $ready = $false }
        $line = Select-String -LiteralPath $file -Pattern '^INFO bounds now (\d+)x(\d+) at (-?\d+),(-?\d+)' | Select-Object -Last 1
        if ($Manual) { }
        elseif (-not $line) { Write-Host 'present: no bounds reported, no input injected'; $ready = $false }
        else {
            $g = $line.Matches[0].Groups
            $cx = [int]$g[3].Value + [int]([int]$g[1].Value / 2); $cy = [int]$g[4].Value + [int]([int]$g[2].Value / 2)
            "centre $cx,$cy" | Set-Content -LiteralPath (Join-Path $results 'injected.txt')
        }
    }
    function Seen([string]$pattern) { [bool](Select-String -LiteralPath $file -Pattern $pattern -Quiet) }
    if ($ready) {
        $sw = $screen.Width; $sh = $screen.Height
        [U1Input]::MoveTo($cx - 30, $cy - 20, $sw, $sh); Start-Sleep -Milliseconds 300
        [U1Input]::MoveTo($cx, $cy, $sw, $sh); Start-Sleep -Milliseconds 600
        if (-not (Seen 'PointerEntered')) { Write-Host 'present: the probe did not see the pointer, no click or keys'; $ready = $false }
    }
    if ($ready) {
        [U1Input]::MoveTo($cx + 40, $cy + 20, $sw, $sh); Start-Sleep -Milliseconds 300
        [U1Input]::Mouse(0x0002, 0); Start-Sleep -Milliseconds 150; [U1Input]::Mouse(0x0004, 0); Start-Sleep -Milliseconds 600     # left down, up
        if (-not (Seen 'PointerPressed')) { Write-Host 'present: the probe saw no click, no keys'; $ready = $false }
    }
    if ($ready) {
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
    $end = (Get-Date).AddSeconds(100)
    while ((Get-Date) -lt $end -and -not (Select-String -LiteralPath $file -Pattern 'Run returning|WATCHDOG|Uninitialize' -Quiet)) { Start-Sleep -Milliseconds 500 }
    Start-Sleep -Seconds 2
    if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination (Join-Path $results "$Mode.txt") } else { Write-Host "present: no output in $folder" }
} finally {
    if ($installed) { Remove-AppxPackage -Package $installed.PackageFullName -ErrorAction Stop }
}
