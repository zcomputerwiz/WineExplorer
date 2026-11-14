<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# XAML probe: the smallest compiled XAML application (XAML experiment step 2b)

C++/CX, `Windows.UI.Xaml`: `App.xaml` and `MainPage.xaml` with a `TextBlock` (`Title`) and a `Button` (`Go`). The
application logs to `LocalState\xamlprobe.txt` what XAML laid out 1.5 s after the page loaded, then exits after about
6 s. Design notes and results: `docs/uwp/PLAN_XAML.md` (2b).

Windows oracle run (the U1 oracle certificate, publisher `CN=WineExplorer U1 Oracle`, must be trusted already):

    .\build.ps1 -Thumbprint <oracle certificate>      (VS 2022 Build Tools with the UWP workload)
    Add-AppxPackage build\package\XamlProbe_1.0.0.0_x64_Test\XamlProbe_1.0.0.0_x64.msix
    start shell:AppsFolder\WineExplorer.XamlProbe_neb1jjyzpw9ht!App

Expected output on Windows 11: `expected-windows.txt`. `Assets\` (blank logos) and `build\` are made by `build.ps1`.
