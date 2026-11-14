# Known bugs (general Wine and WineExplorer)

Bugs that no feature session introduced. The system tier session owns this list (the owner's rule, 2026-11-14): other
sessions report here, the system tier session fixes or routes them and tells the reporter when one lands. Each entry:
who reported it, where it shows, what is known, the state.

| # | Bug | Reported | State |
|---|---|---|---|
| B1 | `user32:win`: 2 failures on x86_64, also on the older baseline (merged-a524eaf3) | UWP session, 2026-11-14 | to reproduce (`wine-tests.sh user32:win`) |
| B2 | `rpcrt4:server`: 2 `np_basic` failures, older than the UWP work | UWP session, 2026-11-14 | to reproduce |
| B3 | A new prefix has no `Themes\Personalize` light-theme values and no `Control Panel\Accessibility\HighContrast`; Uno and WinUI programs fall back to their own defaults | UWP session, 2026-11-14 | fixed in `wine.inf` (Windows' defaults, no clobber), branch `claude/wine-reuse`; a missing `TextScaleFactor` is Windows' own default |
| B4 | A per-machine MSI run by an administrator from the desktop fails silently (exit 1627, empty `/l*v` log, no message) | Integration session, 2026-11-14 | analysed; owner chose the consent prompt for administrators; see below |

## B4: per-machine MSI by an administrator

Seen on the master 3a08b9b7 image (ISO wex-vb, no enforced elevation), normal start: `msiexec /i C:\x.msi /qb
/l*v C:\x.log` as `wineuser` (in `sudo`, an XP administrator) returns 1627 with no window and an empty log. Wine's
`MSI_OpenPackageW` copies the package into `GetWindowsDirectory()\Installer\` (`dlls/msi/package.c`, about 1060);
`C:\windows` is the machine root (`root:wexsys` 2775), which only the system group writes, so `CreateFileW` fails and
the open ends with `ERROR_FUNCTION_FAILED` before anything is logged.

What is wrong, in two parts:

1. **No message and no log.** A failed open of the package must say so (Windows shows "This installation package
   could not be opened" and logs it). Small fix in `dlls/msi`: report and log the failure of the local copy.
2. **No way for an administrator to install per machine.** On Windows the per-machine part of an installation runs in
   the Windows Installer service as LocalSystem; the client only asks it. Wine's `msi` does everything in the
   caller's process. Without enforced elevation nobody, administrators included, writes the machine root (by design:
   C2, "the machine hive is not saved without enforcement"). With enforced elevation, an administrator's elevated
   process can. The fix that matches Windows: per-machine installation through session 0 (the `msiserver` service as
   SYSTEM, after a check that the caller is an administrator), or, smaller, `msiexec` asking the elevation broker
   when a package is per machine (Vista's installer detection).

**Owner's decision (2026-11-14): Vista's way.** An administrator gets the consent prompt for a per-machine
installation, as on Vista and later (not XP's silent install). So part 2 is: `msiexec` (and `msi` when a package is
per machine) asks the elevation broker, the administrator confirms in the consent dialog, and the installation runs
elevated. This needs enforced elevation; on an image without it, per-machine installation stays impossible, which is
one more reason for enforcement by default (`E1_PRODUCTION_PLAN.md`). Part 1 (message and log) is done first.

Part 1, 2026-11-14 (branch `claude/wine-reuse`): `MSI_OpenPackageW` reports a failed local copy, see
`STOCK_CHANGES.md`. The failing case itself is to be checked on an image (the Integration session's package).
