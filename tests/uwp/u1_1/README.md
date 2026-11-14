<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.1 UWP view lifecycle probe

`view.cpp` is a native UWP application (C++/WinRT, MSVC, `/APPCONTAINER`, `EntryPoint="view.App"`) that calls
`CoreApplication.Run` with an `IFrameworkViewSource` of its own and checks, from inside the callbacks, what
`docs/uwp/DESIGN_LIFECYCLE.md` describes. The **same unchanged binaries** run on Windows 11 and on Wine.

A UWP launch passes no command line: the activated application id selects the mode and the output goes to
`%LOCALAPPDATA%\view-MODE.txt` (the package's own folder inside the AppContainer on Windows).

| Application id | mode | |
|---|---|---|
| `View` | `positive` | multithreaded apartment, ends with `CoreApplication.Exit` |
| `ViewReturn` | `return` | multithreaded apartment, returns from `Run` |
| `ViewSta` | `sta` | single-threaded apartment: `Run` fails |

`view.exe unpackaged OUT X` is a control for Wine only (Windows ends such a process with a fast fail).

* `build.ps1 [-Architecture x64|x86] -Execute` builds `build\<arch>\view.exe` (`vswhere` must be on `PATH`:
  `C:\Program Files (x86)\Microsoft Visual Studio\Installer`).
* `package.ps1` builds and signs one package with a two-day certificate (a new one, or `-CertificateThumbprint` of
  one it made before); `run.ps1` installs it, activates the three applications through the U1.0 `launcher.exe`,
  compares the output with `expected-MODE.txt` (PASS and FAIL lines, sorted) and removes the package. Trusting the
  certificate is a separate step the owner takes: **machine** TrustedPeople, an elevated `Import-Certificate`.
* `run-wine-lifecycle.sh WINE_BUILD U1_0_DIR U1_1_DIR RESULTS_DIR`: fresh prefix per architecture, development
  registration (`../u1_0/register-wine.py`), the same activations, the same comparison.
* `expected-*.txt` are what Windows 11 produced.

## Evidence

2026-11-12. Windows 11 Pro build 26300 (x64 host, x86 package under WoW64), MSVC 19.44, SDK 10.0.26100.0, signed with
a throwaway certificate trusted by the owner in TrustedPeople for the run (to be removed afterwards).

* **Windows**: three runs each on x64 and x86: `positive` 34 checks, `return` 32, `sta` 3 all match. Findings are in
  `DESIGN_LIFECYCLE.md`; the full TRACE and INFO output of the exploratory runs is in
  `..\WineExplorer-local` *(not copied here)*.
* **Wine** (candidate `u11-2` of `codex/uwp-runtime`, owned U1 guest, fresh prefix per architecture): `positive` 34,
  `return` 32, `sta` 3 and the unpackaged control all pass on x64 and x86 with the same `view.exe` bits.
  The U1.0 fixture (all checks, 14 server refusals), the 61 + 59 install checks (including installing the UWP package
  with `wexappx` and activating it), windows.ui (12, 82), twinapi (73), windows.storage (599) and kernel32 version
  (541) pass on both architectures; `rpcrt4_test server` has only its two known failures.

## U1.1b presentation and input probe (Windows, started)

Application `ViewPresent` (mode `present`), run by `run-present.ps1` (it takes a screenshot at READY and moves and
types only into a window it has checked; it needs an unlocked desktop that nobody uses for about 40 seconds).

2026-11-12, x64, unlocked desktop (output in `..\WineExplorer-local`, not copied):

* `window.Activate()` **does** show the window when the desktop is unlocked: `Visible=1` after about 80 ms, then
  `WindowActivated` (CodeActivated) and `InputEnabled=1` at about 140 ms. No `SizeChanged`. `Bounds` stay 1200x900 and
  the window opens in a cascading position (the origin moved between runs). The earlier "Activate raises nothing, the
  window stays invisible" measurements (the `window_stays_invisible_after_activate` and `activate_raises_no_window_events`
  checks) were made with a locked desktop and have to be reread: **with the desktop locked** `Visible` stays 0 and
  `Suspending` is raised after about 5 s (confirmed again by this probe's first run).
* `D3D11CreateDevice` and `IDXGIFactory2::CreateSwapChainForCoreWindow` succeed: 1200x900, B8G8R8A8 (87), 2 buffers,
  flip-sequential, `DXGI_SCALING_NONE`; five `Present(1, 0)` calls return `S_OK` at about 110 ms intervals.
* **Input** (the owner clicked and typed by hand into the window, `run-present.ps1 -Manual`; this tool's shell
  cannot inject input, and the frame opens behind the foreground window). Mouse (`PointerDeviceType` Mouse, pointer id
  1 throughout):
  * `PointerEntered` and the first `PointerMoved` are raised together (same millisecond) when the pointer enters the
    window; `PointerExited` when it leaves. Positions are in the window's client coordinates, DIPs, origin at the
    client's top-left (title bar not included); they can touch 0 at the edges. Moves arrive about every 16 ms.
  * Left button: `PointerPressed` (`IsLeftButtonPressed` true), `PointerReleased` (false). Right button: its own
    `PointerPressed`/`PointerReleased` pair (left flag false). `KeyModifiers` 0 with no key held.
  * `PointerWheelChanged`: `MouseWheelDelta` +-120 per notch (negative for the first direction scrolled), position
    unchanged.
  * A click on an inactive window raises `WindowActivated` with state `PointerActivated` before `PointerPressed`;
    losing the foreground raises `WindowDeactivated`; getting it back without a click raises `WindowActivated`
    (`CodeActivated`). Input events only arrive while the window has the pointer or the focus.
  Keyboard: `KeyDown` then, for a character key, `CharacterReceived` (same millisecond), then `KeyUp`.
  `a`: vk 65 scan 30, char 97; `Shift`: vk 16 scan 42 (no char, repeats while held with `WasKeyDown` true and
  `RepeatCount` 1 per event); `Shift+b`: vk 66 scan 48, char 66 (`B`); `Enter`: vk 13 scan 28, char 13; `Left`: vk 37 scan
  75, `IsExtendedKey` true, no char; `F5`: vk 116 scan 63, no char. First `KeyDown` has `WasKeyDown` false, `KeyUp`
  has `WasKeyDown` true and `IsKeyReleased` true.
  No `SizeChanged` and no `VisibilityChanged(false)` at any time (the window was never resized or covered completely).
  The full output is in `..\WineExplorer-local\uwp-u11b-present-windows.txt` *(not copied here)*.
* The presented swap chain content shows white in `screen-at-ready.png` (GDI `CopyFromScreen` does not capture the
  composed flip-model surface). The owner watched the window: the five frames cycled quickly and ended on purple,
  which is the last clear colour (0.4, 0.3, 0.6). The swap chain works; screenshots cannot verify what is drawn.

## Not asserted (observed, varies or races on Windows)

`PreviousExecutionState` (0 and 3 seen), anything after `CoreApplication.Exit` or after `Run` returns (the process may
end at any point), whether `ProcessOneAndAllPending` waits with nothing queued, the exit code of the ended process,
`Bounds` (1200x900 at 0,33 on Windows). Not done: presentation and input, suspend and resume (U1.1b, U1.1c).

## U1.1b on Wine

2026-11-13, candidate `u11-2` of `codex/uwp-runtime` (70b6c0e3 plus merges of Wine `development`), owned U1 guest, Xvfb,
mesa llvmpipe (Wine built with OpenGL, EGL and Vulkan headers), fresh prefix per architecture: the lifecycle modes
(32, 30, 3 checks), the unpackaged control and `ViewPresent` (29 checks: window events, key and pointer events with
`inject.exe`, `CreateSwapChainForCoreWindow`, five presented frames, pixel read back B=153 G=76 R=102 after the last
clear) pass on x64 and x86 with the unchanged `view.exe` bits. Without a 3D device the swap chain and pixel checks
are skipped (28 checks). `run-wine-lifecycle.sh` runs it with `PRESENT=1` under `xvfb-run`.

## StorageFolder, StorageFile and FileIO (U1.5, measured on Windows, passes on Wine)

2026-11-13, application `ViewStorage` (run with `run-mode.ps1 -Mode storage`): 59 checks pass on Windows 11 (list in
`expected-storage.txt`, raw output in `..\WineExplorer-local`). What Windows does, beyond the obvious:

* `CreateFileAsync`: default option is `FailIfExists` (`0x800700B7` when present); `GenerateUniqueName` makes `a (2).txt`
  (the first copy is numbered 2); `OpenIfExists` and `ReplaceExisting` return the file; an empty name is an error; **a name
  with a backslash is a relative path and the folders in between are created**.
* `GetFileAsync`/`GetFolderAsync`/`GetFileFromPathAsync` of something missing: `0x80070002`; a relative path to
  `GetFileFromPathAsync`: `0x80070057`; a folder path given to it fails; `TryGetItemAsync` of a missing name returns null.
* `GetFilesAsync` and `GetFoldersAsync` list only files or folders, sorted by name; `GetItemsAsync` lists both.
* `DisplayName` of `a.txt` is `a.txt`; `ContentType` of `.txt` is `text/plain`.
* `FileIO.WriteTextAsync` writes UTF-8 **without** a BOM by default; `UnicodeEncoding.Utf16LE` writes the BOM (`FFFE`);
  `WriteLinesAsync` ends every line with CRLF; `ReadTextAsync` strips a UTF-8 BOM and detects a UTF-16 BOM; an empty
  file reads as an empty string; `PathIO` on a missing file fails (`0x80070002`), it does not create it.
* `RenameAsync` onto an existing name fails (`0x800700B7`); `CopyAsync(folder)` keeps the name and fails on a collision;
  `CopyAsync(folder, name)` and `MoveAsync(folder, name)` work and `MoveAsync` updates the object's `Name` and `Path`;
  `DeleteAsync` of a deleted file is `0x80070002`; deleting a folder removes its content.
* `GetFileFromApplicationUriAsync` takes `ms-appdata:///local/...` and `ms-appx:///...` (the package folder).

Wine, 2026-11-13, candidate `u11-2` of `codex/uwp-runtime`: the same `view.exe` bits pass all 59 checks on x64 and
x86 (`run-wine-lifecycle.sh` runs application `ViewStorage` after the application data runs). What is in Wine now:

* `dlls/windows.storage/folder.c`: one object for a file or a folder (a path). `StorageFolder`: create, get, try-get,
  list, rename, delete (with content), basic properties. `StorageFile`: type, content type, copy, move, replace,
  rename, delete, basic properties, display name. Statics: `GetFolderFromPathAsync`, `GetFileFromPathAsync`,
  `GetFileFromApplicationUriAsync` (`ms-appdata:///local|roaming|temp/...`, `ms-appx:///...`).
* `dlls/windows.storage/fileio.c`: `FileIO` and `PathIO` (text, lines, buffer, bytes; the three encodings).
* `dlls/wintypes/datareader.c`: `DataReader.FromBuffer` and the read methods.
* Reused from Wine: the asynchronous-operation framework of `windows.storage`, `Buffer` of `wintypes`, the vectors of
  `windows.media.speech` (copied to `dlls/windows.storage/vector.c`). Upstream Wine (checked 2026-11-13) has no
  `StorageFile`, `FileIO` or `DataReader`.

Not done: thumbnails, `Properties`, `GetParentAsync`, queries
(`CreateFileQuery`), streamed files, `KnownFolders`, pickers. The operations finish on the calling thread (the
returned asynchronous object is already completed); the recycle bin is not used by `DeleteAsync`.

### File streams (measured on Windows, pass on Wine)

The same application then measures the streams of a file: 28 more checks (87 in `expected-storage.txt`), which pass
on Windows 11 in two runs and on Wine x64 and x86 (commit 9484da1a: `dlls/windows.storage/filestream.c`, the write
methods of `DataWriter` and `DataReader.LoadAsync` in `dlls/wintypes`). What Windows does:

* `OpenAsync(ReadWrite)`: `CanRead` and `CanWrite`; a second `ReadWrite` stream of the same file is a sharing violation
  (`0x80070020`); `Read` streams and `FileIO` reads work beside it. `OpenAsync(Read)`: `CanWrite` is false,
  `WriteAsync` gives `0x80070005`, setting `Size` returns success and changes nothing; a `ReadWrite` stream and `FileIO`
  writes work beside it. A deleted file gives `0x80070002`.
* `DataWriter.StoreAsync` on the stream returns the byte count and the bytes are on disk at once; `FlushAsync` gives
  true. `DataReader.LoadAsync(100)` with 8 bytes left returns 8.
* `Size(4)` truncates and leaves `Position` where it was (8). `GetOutputStreamAt` and `GetInputStreamAt` have their own
  position and leave the stream's alone. `CloneStream` starts at 0 and can write. A read past the end gives an empty
  buffer; a count over the buffer's capacity is `0x80070057`.
* After `Close`, `Size` gives `0x80000013`; a second `Close` is fine.
* `OpenReadAsync` gives a read stream with the content type of the file (`text/plain` for `.txt`, empty for `.bin`);
  `OpenSequentialReadAsync` gives an input stream.
* `OpenTransactedWriteAsync`: the stream starts with the content of the file at position 0; the file changes at
  `CommitAsync` (writing `new` over `12xy` gives `newy`).

The check `application_ids_are_all_applications_of_the_package` of `ViewPkgapi` counts the applications of the
manifest: it was 7 and is 8 since `ViewStorage` was added (a probe correction, measured again on Windows: 39 and 59
checks pass).

## Package resources (U1.6, measured on Windows, passes on Wine)

2026-11-14, application `ViewResources` (run with `run-mode.ps1 -Mode resources`): 43 checks pass on Windows 11, x64
twice and x86 (list in `expected-resources.txt`). The package has a `resources.pri` that `package.ps1` builds with
MakePri from `resources/Strings` (en-US and de-DE `.resw` files) and two images (`Assets/pic.scale-100.png`,
`Assets/pic.scale-200.png`); the `packaging` element of the MakePri configuration is removed so that every language
and scale stays in one file (with it, MakePri splits them into resource-pack files that a single package never loads).
`build/payload` keeps `resources.pri` and the images for the Wine runs (`register-wine.py stage --payload`).

What Windows does:

* `ResourceLoader` names are relative to its map (`Resources` by default): `GetString(L"Resources/Greeting")` is empty,
  `GetString(L"/Resources/Greeting")` is the string. Lookups ignore case. A missing name gives an empty string; an empty
  name is `0x80070057`; a loader for a map that is not there fails with `0x80073B1F`.
* MakePri turns the dots of a `.resw` name into slashes: `Button.Content` is found as `Button/Content`.
* `GetStringForUri` and `GetStringForReference` take `ms-resource:///Resources/Greeting`, `ms-resource:Greeting` (relative
  to the map) and `ms-resource://<package name>/Resources/Greeting`.
* The main map holds the strings and every file of the package (`Files/...`, 10 entries here), its URI is
  `ms-resource://<package name>/`, `AllResourceMaps` has one map named after the package. `GetValue` of a missing name
  returns a null candidate (no error); `GetSubtree` of a missing scope returns null.
* A file candidate's value is the full path in the package folder; `Files/Assets/pic.png` is found without the scale in the
  name; `GetValueAsFileAsync` gives that file.
* Candidates are listed best first for the default context; a string candidate's qualifier `Language=en-US` is the
  default (the default language of the PRI).
* Contexts: `Language=de-DE` picks `Hallo`, a resource without a German string falls back to the default language;
  `de` and `de-AT` match `de-DE`; `fr-FR;de-DE` is a list; `ja-JP` gives the default. `Scale=200` picks the 200 image,
  150 also picks 200 (the next bigger one), 400 picks 200 (the biggest). An unknown qualifier can be set.
* The default context has 12 qualifiers (`Language`, `Contrast=standard`, `Scale` empty, `HomeRegion`, `TargetSize`,
  `LayoutDirection`, `Theme`, `AlternateForm`, `DXFeatureLevel`, `Configuration`, `DeviceFamily=Desktop`, `Custom`).
* `ResourceContext.GetForViewIndependentUse()` is one object for the process: a qualifier set through it changes what
  every `ResourceLoader` returns. `ResourceManager.DefaultContext` and `new ResourceContext()` are separate.

Wine, 2026-11-14 (commit fe9776eb of `codex/uwp-runtime`, merged to Wine `development`): the same bits pass all 43
checks on x64 and x86, with and without a display. The code is in `dlls/windows.applicationmodel` (`pri.c`,
`resources.c`); `Package.Id` was a stub before and is now implemented (the probe reads the package name through it).

Prior art used for the Wine side: the PRI layout described in "Package Resource Index File Format" of PriTools
(https://github.com/chausner/PriTools, Apache-2.0, a description written from analysis of files; no code taken), checked
against the files MakePri made with a Python reader before the C code was written; the map and vector code of Wine
(`dlls/wintypes/map.c`, `dlls/windows.media.speech/vector.c`). Microsoft's MRT Core (WindowsAppSDK, MIT) is the
reference implementation; not used.
