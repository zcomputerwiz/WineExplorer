# WebView2 compatibility lab

The native C diagnostic host and Fixed Version runtime are installed in
`/home/wineuser/webview2-lab` on WineExplorer-VM. WebView2 initializes and
navigates, but rendering does not yet work. This is a compatibility investigation,
not a shell feature ready for integration.

## Upstream research (2026-10-09)

[WebView2Feedback #5720](https://github.com/MicrosoftEdge/WebView2Feedback/issues/5720)
is still open. Its three comments are from the reporter; no confirmed solution
or maintainer fix is posted. The live API reports its last update as 2026-10-08.
The report describes legacy SwiftShader arguments, WARP failures and a stalled
first frame. These are observations for the reported runtime versions, not proof
that all newer versions follow the same command-line path.

[DXVK #4933](https://github.com/doitsujin/dxvk/issues/4933) is also open and reports
the same DirectComposition creation error we observe. Installing DXVK alone is
not an established solution for this missing API.

[Wine bug 59370's discussion](https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/UDZKZTMP5WYISOAUDGEOMFOSU7X73JND/)
was closed as a duplicate of [58921](https://bugs.winehq.org/show_bug.cgi?id=58921),
not as fixed. A maintainer notes missing composition, cross-process surface and
sandbox features, and suggests an older Windows version or Chromium fallback
arguments for hosts that do not explicitly require composition. That suggestion
is not a verified fix here. The main bug's current page could not be read because
of its bot challenge. A Windows 8 experiment should use an isolated prefix or
per-application setting, rather than changing the desktop's global version.

### Proton GE lead

[GE-Proton11-7's official release notes](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)
report a working Forza Horizon 5 WebView2 login window after restoring
cross-process rendering. The release includes a substantial Wine-Wayland
child-process rendering series, plus X11 background-window and cursor fixes.
[The referenced commit](https://github.com/GloriousEggroll/proton-ge-custom/commit/16a62e92)
also removes the FH5-specific `noopwr` exception. This is a useful implementation
lead, rather than evidence that a single acceleration flag fixes all WebView2
hosts. The documented success is game-specific; our standalone host, runtime
155 and VirtualBox X11 environment have not been tested under GE.

Next compare the same probe/runtime in a separate GE/umu prefix, preserving the
desktop prefix. Record the actual runtime, arguments, graphics stack and display
backend. Inspect the Wine-Wayland cross-process surface patches and relevant
Proton launch defaults before selecting fixes to port. A Wayland-specific success
may require a separate test compositor and does not directly validate X11 fallback.

### Proton's own WebView2 setting (2026-10-09, positive result)

Valve's Proton 11.0 Wine (`e60892c`) still stubs `DCompositionCreateDevice*`. Its
only WebView2 handling is one `wine.inf` line, which reports Windows 7 to the
runtime process only (the host and the desktop keep their version):

```
HKCU,Software\Wine\AppDefaults\msedgewebview2.exe,"Version",,"win7"
```

That key set in the Wine 11.19 prefix on ProtonExplorer-VM (still the baseline Wine):

| Run directory | Arguments | Result |
| --- | --- | --- |
| `20261009T205918Z-win7-baseline-FBq74U` | logging only | GPU process exits 9 times; wined3d over VirtualBox GL offers no D3D feature level ANGLE accepts; zero frames |
| `20261009T210054Z-win7-swiftshader-UAE84h` | SwiftShader | `animation_progressing`, assessment exit 0, 829 frames in 19 s, late deltas 26-60, no DComp/WARP errors, no GPU restarts |
| `20261009T210126Z-win7-swiftshader-visual-bO5OcY` | SwiftShader, 60 s | Assessment exit 0, 3183 frames, steady ~60 fps. Visually checked: page, counter and moving square paint; background alternates; wheel scrolling repaints |

Under Windows 7 Chromium skips DirectComposition and presents without it, which
works with Wine's cross-process child windows. With `--enable-logging` the runtime
also opens a console window that stays above the probe. That console is a
diagnostic side effect; drop the flag in production.

Remaining failure without SwiftShader: VirtualBox's OpenGL is the limit (wined3d
cannot create a 4.4 context). It is not a Wine/Proton difference. On real GPUs, or with
DXVK over Mesa (lavapipe is installed in the VM), hardware ANGLE should be tested
separately.

## Minimal setup

The lab uses Microsoft's x64 SDK **1.0.4258.31** and Fixed Version runtime
**155.0.4283.45**, a MinGW C compiler, Python 3.11+ and `cabextract`. Debian's
existing compiler and Python were sufficient; only `cabextract` was added.
No .NET, runtime installer, updater, registry edits or prefix-wide DLL overrides
are required. SDK and runtime binaries stay outside Git.

From a Linux checkout, with Wine already installed:

```bash
sudo apt-get install --no-install-recommends gcc-mingw-w64-x86-64 cabextract python3
python3 scripts/setup-webview2-lab.py
```

For this Windows-hosted checkout, copy the sources into the guest first:

```powershell
scp -i D:/VirtualBox_VMs/vm_key -P 2222 scripts/setup-webview2-lab.py scripts/webview2-probe.c scripts/run-webview2-lab.sh scripts/analyze-webview2-run.py wineuser@127.0.0.1:webview2-lab/
scp -r -i D:/VirtualBox_VMs/vm_key -P 2222 scripts/webview2-compat wineuser@127.0.0.1:webview2-lab/
ssh -i D:/VirtualBox_VMs/vm_key -p 2222 wineuser@127.0.0.1 'python3 ~/webview2-lab/setup-webview2-lab.py'
```

The setup script gets the SDK from official NuGet and locates the requested
runtime on Microsoft's [download page](https://developer.microsoft.com/en-us/microsoft-edge/webview2).
It caches the archives, records their URLs and SHA-256 hashes, and verifies cached
downloads against that provenance. Once cached, rebuilding does not require the
runtime to remain listed on the download page. `--sdk-version`,
`--runtime-version` and `--root` permit version comparisons. An uncached runtime
must still be listed by Microsoft. The small `EventToken.h` shim handles SDK vs.
MinGW filename casing on Linux; it does not replace SDK interface declarations.

## Run and inspect

Run in the existing graphical Wine session, with the probe visible and unobscured.
Do not restart Wine or launch a competing desktop just for this test. In the guest:

```bash
export DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority
bash ~/webview2-lab/run-webview2-lab.sh baseline
bash ~/webview2-lab/run-webview2-lab.sh software
bash ~/webview2-lab/run-webview2-lab.sh swiftshader
```

Each run uses a fresh browser profile under `~/webview2-lab/runs`, opens a normal
Win32 window, navigates to an inline local page, and closes after 20 seconds.
The page has a moving square, alternating background, frame/timer counters,
typing, button and scrolling controls. Resize the window and inspect these
visually once rendering progresses. Each case is independent:

| Case | Additional arguments |
| --- | --- |
| baseline | Logging only |
| software | `--disable-gpu` |
| swiftshader | `--disable-gpu --enable-unsafe-swiftshader --use-angle=swiftshader` |

For longer or targeted experiments:

```bash
WEX_WEBVIEW2_SECONDS=60 bash ~/webview2-lab/run-webview2-lab.sh swiftshader
WEX_WEBVIEW2_BROWSER_ARGS='--disable-direct-composition' bash ~/webview2-lab/run-webview2-lab.sh composition-off
WEX_WEBVIEW2_WINEDEBUG='+timestamp,+pid,+tid,+dcomp,+d3d11,+dxgi' bash ~/webview2-lab/run-webview2-lab.sh swiftshader
```

`WEX_WEBVIEW2_LAB` selects a different lab root. Optional Chromium remote debugging
can be enabled through `WEX_WEBVIEW2_BROWSER_ARGS`; it is not enabled by default.
The baseline does not request disabling Chromium's sandbox. Experimental flags
are recorded verbatim and must be checked against the actual command line and
logs; requesting a flag does not establish that the runtime honors it.

Artifacts in each run directory:

- `host.log`: initialization/controller/navigation HRESULTs, browser version,
  requested arguments, actual Windows browser command line, bounds changes,
  process failure events, and one-second DOM/animation/visibility samples.
- `wine.log`: timestamped PID/TID Wine warnings, errors and missing API messages.
- `chromium.log`: Chromium's own verbose log; arguments are supplied explicitly
  through `ICoreWebView2EnvironmentOptions`.
- `processes.jsonl`: Linux PID/argv snapshots for this run's profile, including
  child renderer/GPU processes. Linux argv alone can omit broker-delivered flags;
  consult the Windows command line in `host.log` too.
- `tooling.json` and `session.txt`: runtime/SDK versions, download provenance,
  Wine version, display/prefix and requested arguments.
- `summary.json`: navigation result, first/last samples, late frame deltas and
  counts of known GL/WARP/D3D11/DirectComposition errors.
- `exit-code.txt`: host/timeout result; `assessment-exit-code.txt`: final launcher
  result after analysis.

Host exit zero only means sample callbacks arrived. The launcher returns zero
only for sustained late animation progression; stalled, zero, intermittent or
insufficient frames produce analyzer exit 4. Host failures or timeouts retain
their own nonzero result. Re-analyze an existing run with
`python3 ~/webview2-lab/analyze-webview2-run.py RUN_DIRECTORY`.
Neither callback success nor animation counts prove correct on-screen output.
Check the actual VM display, input, scrolling, resizing and visibility. An
occluded/minimized window can legitimately stop animation and invalidate a run.

## Initial results

Wine 11.19, Debian 13, VirtualBox SVGA, X11 session, Fixed runtime 155.0.4283.45:

| Run directory | Result | Observed error |
| --- | --- | --- |
| `20261009T183238Z-baseline` | Navigation succeeds, timers advance, zero animation frames | D3D11 WARP initialization fails |
| `20261009T183522Z-software` | Navigation succeeds, timers advance, zero animation frames | 24 WARP initialization failures |
| `20261009T183338Z-swiftshader` | Four startup animation frames, then all five late deltas zero | Two `DCompositionCreateDevice` failures, `E_NOTIMPL` |
| `20261009T184054Z-swiftshader-hm5GRg` | Final tooling rerun: three startup frames, then stall; assessment exit 4 | Two `DCompositionCreateDevice` failures, `E_NOTIMPL` |

The controlled baseline and SwiftShader runs predate the final wrapper's unique
directory suffix and assessment exit file; their logs and analyzer summaries
remain usable. Earlier `182949`/`183024` runs did not reliably apply logging
options and should not be used as controlled flag comparisons.

All sampled pages report `visibility: visible`. The baseline window was visibly
blank. Our runtime's recorded browser command line does not show the legacy
`--use-gl=swiftshader` argument from #5720, and its GL allowlist error was absent.
The failure overlaps that issue's rendering symptoms but is not an exact
reproduction of its legacy-switch claim.

The local Wine source at `~/src/wine/dlls/dcomp/device.c` confirms
`DCompositionCreateDevice`, `DCompositionCreateDevice2` and
`DCompositionCreateDevice3` return `E_NOTIMPL`. This is a concrete next API gap to
investigate; changing the VM cursor or adding .NET cannot supply composition.
Do not substitute successful dummy HRESULTs for actual composition surfaces.
Before integrating into Explorer, establish a Windows 11 positive control with
the same host/runtime, test a real presentation fallback, then verify continuous
painting and input in Wine. No Windows positive control or working presentation
path has been validated yet.

Tooling validation: the host builds with `-Wall -Wextra`; Python compilation and
`bash -n` pass. Analyzer checks cover zero frames, continuous progression,
startup-only frames followed by a stall, and insufficient samples. Two cached
setup reruns succeed, including one that uses the saved runtime provenance
without consulting the download page.
