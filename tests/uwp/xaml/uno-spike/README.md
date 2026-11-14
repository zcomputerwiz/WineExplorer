<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# XAML experiment, step 1: an Uno application on Wine (2026-11-14)

Question (`docs/uwp/PLAN_XAML.md`, owner's decision of 2026-11-14): can Uno, as a downloaded component, carry XAML
for UWP applications on Wine? Step 1: does an unchanged Uno desktop application (Skia renderer, Win32 host) run on Wine
at all? Step 2 (not done): a C++/WinRT XAML application shown through a small shim over Uno hosted in its process.

## How the application was made

On the Windows host with the .NET 10 SDK (installed per user with Microsoft's signed `dotnet-install.ps1`):

    dotnet new install Uno.Templates
    dotnet new unoapp -n UnoSpike -preset blank -platforms desktop -o UnoSpike
    (replace UnoSpike/MainPage.xaml.cs and UnoSpike/Platforms/Desktop/Program.cs with the files here; in
     App.xaml.cs, InitializeLogging: '#if DEBUG' -> '#if true', the "Uno" filter and the minimum level -> Debug,
     and call Program.Log("OnLaunched") first in OnLaunched)
    dotnet publish -c Release -f net10.0-desktop -r win-x64 --self-contained -o out-win-x64

The application logs to `%TEMP%\uno-spike.txt`: start, host, `OnLaunched`, the size of the page, of a `TextBlock` and
of a `Button` it adds, the button's position, and exits after about 10 s. `run-wine.sh` runs it under Wine in a fresh
prefix (made with `mscoree` and `mshtml` disabled, which the application itself must not have: CoreCLR loads its
ready-to-run assemblies through the loader, and a disabled `mscoree` makes them "module not found").

## Results

| | Windows 11 | Wine (Wine fe9776eb + df3f260e + 1052fab3) |
|---|---|---|
| x64 | starts, page 1008x601, text 129x20, button 84x33 | starts, page 1016x606, text 129x20, button 84x33, exits normally |
| x86 | fails: `icuuc77` not found | fails the same way |

The x86 failure is Uno's: its ICU package (`Uno.icu-win` 77.4.1) has only win-x64 and win-arm64 libraries and the
application is set to use an app-local ICU. Work for the Uno component (32-bit UWP applications need it).

What Wine needed (commit 1052fab3, `docs/STOCK_CHANGES.md`):

* A window class registered with one small instance value that is not a module handle must be found with another
  (Uno passes `Process.GetCurrentProcess().Handle`, a new handle at every call; `classinst.c` here shows Windows
  accepting it). Wine failed with error 1411, "class does not exist".
* `winlangdb.dll` (`EnsureLanguageProfileExists`) did not exist, and `bcp47langs.GetUserLanguages` returned success
  without a string.

Still reported by Uno on Wine, not blocking: the registry keys `Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`
and `Control Panel\Accessibility\HighContrast` and the text scale factor are missing (light theme and defaults are used).
