<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Store Calculator 11.2607: the Windows Runtime classes it needs (gap list, 2026-11-21)

A static list, made without the package's code: the open-source Calculator (MIT) at the commit of its 11.2607 version
(9f186049, "Increment minor version from 2606 to 2607"; C++ view models in CalcViewModel, a C# UI, C# application view
model) was scanned for the Windows Runtime classes it constructs or calls statics of, against every runtime class of the
Windows SDK 10.0.26100 metadata (`winrt-uses.py`). The strings of the package's own metadata (`CalculatorApp.ViewModel.winmd`,
`GraphControl.winmd`, `TraceLogging.winmd`, read in the lab) name 14 SDK classes, all found by the source scan, so the
source matches the Store build. `gap-list.py` compares the list with the classes Wine registers (its modules'
`classes.idl`) and those XamlShim serves (its generated ABI). Classes only named or reached through other objects are not
counted (their instances come from APIs that are). A static list finds missing classes; a class that exists but has
stub methods (E_NOTIMPL) or behaves differently shows only at run time.


Regenerated 2026-11-21 against Wine 401183d1 (the classes.idl of its modules) and XamlShim 0.3.3 (its generated ABI):
54 classes are registered by Wine (twinapi.appcore 16, windows.globalization 10, wintypes 6, windows.ui 6,
windows.storage 4, windows.applicationmodel 4, windows.web 2, and one each of windows.ui.xaml,
windows.storage.applicationdata, windows.networking.connectivity, windows.graphics, threadpoolwinrt, iertutil), 34 are
served by XamlShim. Registered is not the same as complete: the Calculator's flows (modes, clicks, keys, date
calculation, settings with the dark theme) pass on 0.3.3, and `README.md` lists what was seen at run time.

## Still missing

| Class | Use | Where |
|---|---|---|
| Windows.ApplicationModel.Activation.SplashScreen | statics | Wine |
| Windows.UI.Xaml.Controls.SwipeControl | statics (properties) | XamlShim |
| Windows.UI.Xaml.DebugSettings | statics | XamlShim |
| Windows.UI.Xaml.Input.Pointer | statics | XamlShim |

None of the four is reached by the flows above. Since 2026-11-16 Wine has gained the clipboard classes
(Clipboard, DataPackage, StandardDataFormats, HtmlFormatHelper), DateTimeFormatter, CurrencyFormatter, Launcher,
ThreadPoolTimer, SystemNavigationManager, ViewModePreferences, FontWeights, JumpList, JumpListItem,
LimitedAccessFeatures and UserActivityChannel; XamlShim serves the 18 XAML classes of the earlier list and its
FontWeights forwarding.

## Next

Measure each of the four on Windows 11 first; a Wine class gets a test that passes there and on Wine (x64 and x86).
Re-run `gap-list.py` (it needs `winrt-uses.py`'s TSV of the open-source Calculator at the 11.2607 commit) after
Wine or XamlShim registers classes.
