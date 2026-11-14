<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# XAML probe: the smallest compiled XAML application (XAML experiment step 2b)

C++/CX, `Windows.UI.Xaml`: `App.xaml`, `MainPage.xaml` with a `TextBlock` (`Title`) and a `Button` (`Go`), and
`SecondPage.xaml`. The application logs to `LocalState\xamlprobe.txt` what XAML laid out 1.5 s after the page loaded,
navigates to `SecondPage` (after dropping its timer), which reports the back stack depth and, 3 s later, whether
`MainPage`'s destructor has run, then exits. On Windows it has not: `MainPage`'s `Loaded` lambda and `Click` handler
capture `this`, a reference cycle through the XAML objects that a C++ application has no collector for. (Output lines
"clicked" appear only if someone presses the button during the run.) Design notes and results: `docs/uwp/PLAN_XAML.md` (2b).

Windows oracle run (the U1 oracle certificate, publisher `CN=WineExplorer U1 Oracle`, must be trusted already; since
2026-11-14 the 10-year one, thumbprint `E0F4CA62442CBDFD948AD938739BEE8B2932C233`, in the owner's `CurrentUser\My`
and trusted in `LocalMachine\TrustedPeople`):

    .\build.ps1 -Thumbprint <oracle certificate>      (VS 2022 Build Tools with the UWP workload)
    Add-AppxPackage build\package\XamlProbe_1.0.0.0_x64_Test\XamlProbe_1.0.0.0_x64.msix
    start shell:AppsFolder\WineExplorer.XamlProbe_neb1jjyzpw9ht!App

Expected output on Windows 11: `expected-windows.txt`. `Assets\` (blank logos) and `build\` are made by `build.ps1`.
