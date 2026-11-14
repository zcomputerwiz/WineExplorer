# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: write cxprobe\xamlshim.reg from the published component: ComponentPath (HKLM\Software\Wine\Xaml) and every class
# the component serves (XamlShim.exe --classes) registered to Wine's windows.ui.xaml.dll; what the setup does
$ErrorActionPreference = 'Stop'
$pad = Split-Path -Parent $MyInvocation.MyCommand.Path
$component = Join-Path $pad 'cxprobe\xamlshim'
$list = Join-Path $pad 'cxprobe\classes.txt'
if (Test-Path $list) { Remove-Item $list }
$p = Start-Process -FilePath (Join-Path $component 'XamlShim.exe') -ArgumentList '--classes', "`"$list`"" -Wait -PassThru
$classes = if (Test-Path $list) { Get-Content $list } else { @() }
if ($p.ExitCode -ne 0 -or -not $classes) { throw "XamlShim --classes failed: $($p.ExitCode)" }
$lines = @('Windows Registry Editor Version 5.00', '', '[HKEY_LOCAL_MACHINE\Software\Wine\Xaml]', '"ComponentPath"="C:\\xamlshim"', '')
foreach ($c in $classes) {
    $lines += "[HKEY_LOCAL_MACHINE\Software\Microsoft\WindowsRuntime\ActivatableClassId\$c]"
    $lines += '"DllPath"="C:\\windows\\system32\\windows.ui.xaml.dll"'
    $lines += '"ActivationType"=dword:00000000'
    $lines += '"Threading"=dword:00000000'
    $lines += ''
}
Set-Content -Path (Join-Path $pad 'cxprobe\xamlshim.reg') -Value $lines -Encoding ascii
"$($classes.Count) classes"
