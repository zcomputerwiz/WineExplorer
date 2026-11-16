<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Lab helper scripts of the UWP track

Copies of the scratch scripts that drove the owned U1 guest (saved 2026-11-13 so that they are not lost with the
session's temporary folder). They are not a maintained harness: paths of the session's scratch folder are written
into `lab.sh` and `u11-fixture.sh` and have to be changed to where the scripts are run from. `docs/uwp/RESUME.md`
says how they are used.

* Host side (Git Bash on Windows): `lab.sh` (`ssh`, `push`, `delta`, `fixture`), `u11-fixture.sh`.
* Guest side (pushed to `claude/` in the guest workspace): the `guest-*.sh` scripts, `fastbuild*.sh`,
  `test-wexappx.sh` and the rest.

## Calculator runs: quick iteration and fresh verification (2026-11-16)

Two ways to run the Store Calculator (lab copy only) on the guest's Wine build with a XamlShim build:

* **Fresh run** (`calc-run.sh [WINEDEBUG]`, about 6 minutes): a new prefix, `wineboot -i`, the uwp-xaml component from
  a release zip (`XAMLSHIM_ZIP=...`) through its install code, the framework packages, the package through wexappx,
  then the launch. It is the verification: run it before a commit that a run is meant to back, after a Wine change
  that registers classes, and before a release.
* **Quick iteration** (`iterate.sh [--no-build] [WINEDEBUG]`, about 1 to 2 minutes): `shim-quick.ps1` publishes
  XamlShim incrementally on the host (the ABI is generated again only when `WinmdGen/classes.txt` or the generator
  changed) and `quick-pack.py` puts the files that differ from the base release zip in `quick.tar`; on the guest,
  `calc-quick.sh` copies the base prefix, unpacks those files over the installed component (registering the classes
  again when `classes.txt` changed, `quick-reg.py`), launches, and stops once the shim log has been quiet for `QUIET`
  seconds after OnLaunched (at most `MAX`). It prints the shim's failures and Wine's errors and stubs.
  What the window shows: the X screenshot (`quick.png`) is black inside the window frame, as the screen does not show
  Uno's window surface; the shim reads the window's pixels back through GDI 5 s after placing it, and `iterate.sh`
  fetches them as `calc-window.png` in the scratchpad. `XAMLSHIM_RENDER=software|opengl|vulkan` forces Uno's
  rendering backend. `INPUT=clicks|keys` runs `calc-input.exe` (built in the guest from `calc-input.c` with
  `x86_64-w64-mingw32-gcc -O2 -o calc-input.exe calc-input.c -luser32`) beside the Calculator: after 12 s it types
  7 + 5 = (8 s between the steps, as the shim's readbacks, one every 8 s, are), the run's summary lists the display
  text of each readback and `calc-window-all.png` shows them side by side. Wine classes newer than the base prefix need `REGCLASSES="wintypes.dll=Class,Class other.dll=..."`
  (the DLL name with `.dll`).
  `iterate.sh --prep XAMLSHIM_ZIP` makes the base prefix (`calc-prep.sh`, about 5 minutes) from a release zip; make it
  again after a new release or a Wine change that registers classes, or pass `WINEBOOT_UPDATE=1` for a run.
  Wine modules rebuilt with `modbuild.sh` take effect in both, as the prefixes run on the build tree.

Both launch the Calculator as the Start menu does, without arguments, through `applaunch.c` (built in the guest with
`x86_64-w64-mingw32-gcc -municode -O2 -o applaunch.exe applaunch.c -lole32 -luuid`): the Calculator reads non-empty
launch arguments as a mode number. `lab.sh wait 'command' [SECONDS]` runs a guest command without the controller's
55-second limit, so a run is waited for in one call.
