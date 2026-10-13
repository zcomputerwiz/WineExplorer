# P3b subsystem extraction

Shared resources and themeui accepted October 13, 2026. Other planned subsystem extractions
are pending; P4 repository restructure follows their acceptance.

`src/dlls/wexres` owns the icon/bitmap packs, namespace lookup and resource
templates formerly in wexshell. `src/include/wexres.h` defines the exports.
The shell keeps its manifest and file-description metadata. Resource calls
use the shared DLL's immutable module identity; window class/code instances
remain owned by the shell. Start menu style crosses the boundary as a value,
through a shell-local adapter, rather than a pointer to taskbar settings.
Resource images exist once. Feature dialog templates will move to their
respective DLLs when those features are extracted.

Build entries and header installation are retained in the Wine fork. The
implementation and shared header are project-owned, linked by assemble.sh.
Asset generation, the public asset manifest and private overlay VM sync point
at wexres. The original audit pin and historical P2 assembly ledger are intact.

## Acceptance evidence

Build VM: `/srv/protonexplorer/wex-refactor-p3b`, separate from P3 and installed
runtimes. Both DLLs build alone; the complete 64-bit Wine build passes.
Logs are `/srv/protonexplorer/p3b-delayed-check.log` and `p3b-smoke.log`.

- Four Xvfb captures against accepted P3: Classic Start, two-column Start,
  Explorer, Turn Off. Each has zero changed pixels in the 800x570 comparison
  area (the changing taskbar clock is excluded).
- Tray layout probe: two visible applet icons, zero overlaps/out-of-bounds.
- Eight native-dialog placement checks pass, including oversized and
  offscreen-owner dialogs.
- Versioned shell API/startup probe: zero failures with the plugin present;
  plugin-disabled run retains the stock tray and passes.

Screenshots/logs stay in ignored build output. The live dev session still uses
the accepted P3 build; this isolated step has not installed a new runtime.
No new ISO/install test is claimed for this resource extraction. P4 requires
a fresh clone, complete build/install, ISO and interactive installer test.

## Remaining extraction order

msgina, stobject COM applets and network/wireless UI, wexsearch,
then wexbrowse. Keep clock/taskband/Alt+Tab in wexshell. Every split must pass
its feature check and visual comparison; optional modules must not prevent
shell startup when absent. Record any seam that is too wide instead of
forcing it across DLLs.

## Theme engine acceptance

`src/dlls/themeui` owns theme_engine.c. Its single interface is
`src/include/wextheme.h`; callers receive copied settings, with explicit Start
menu style and wallpaper-refresh operations. Display Properties calls the same
engine directly for Themes/Appearance Apply. The existing shell IPC command
remains compatible. This required engine uses ordinary imports; its dependent
shell libraries remain delayed, preserving graphics startup order.

The split exposed a Classic palette persistence gap: SetSysColors changed
temporary values, but the shell's reload read the previous persistent colors.
The engine now imports the theme's Colors section into HKCU with its other
registry sections. This is the only intentional behavior correction in this
step.

The standalone themeui/shell/desk.cpl builds and complete Wine build pass.
Four screenshots again match accepted P3 with zero changed pixels above the
clock strip. `scripts/refactor/check-theme.sh` passes its UI/API probe with
zero failures: state snapshots cannot mutate DLL globals; missing files are
rejected; the Themes page discovers a new fixture, applies it, and refreshes
its list after a rename. The real shell background pixel is the fixture's
RGB(12,34,56), confirming cross-process rendering. Logs:
`/srv/protonexplorer/p3b-theme-final2.log`, `p3b-theme-trace.log`, and
`wex-refactor-p3b/checks/theme/result.txt`.
