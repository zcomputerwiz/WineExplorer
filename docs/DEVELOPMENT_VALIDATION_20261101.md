<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Unified development validation — 2026-11-01

Validation is in progress. This document does not yet accept Chromium or the desktop.

Inputs: WineExplorer development 4a722b72, Wine development 67317644,
wexcomp 94faa651. The preserved Wine feature b5e05301 adds the missing v11
cross-connection rendering client. Development still has the v10 client.
Master and the coordinator's worktrees remain unchanged.

The clean combined build exposed two assembly defects: os.rename could not
preserve pristine stock modules across Linux/Windows filesystems, and clean
Wine configure/header lists omitted project module/header registrations.
Assembly now preserves stock directories with shutil.move and registers the
project-owned modules and headers idempotently. The actual configure completes.
The runtime packager now requires and dereferences all six Wine TrueType fonts;
14 package fixtures pass on Linux without skips. The registration fixture passes.
The full production WoW64 build includes both PE architectures and the merged
wexhost Unix bridge. Regression-test binaries are disabled for this build;
focused validation is separate.

The pinned compositor was built from source. The existing render-role test,
using separately generated protocol metadata from Wine b5e05301, passed all
24 pixel/geometry cases with probe exit 0 and both owned processes stopped.
This is a headless pixman functional control, not a GPU/browser/security claim.
See evidence/development-render-role-20261101.json.

The existing 8 GiB R4 VM was booted for preflight. It had about 13 GiB free and
no active Wine session. Its owned shutdown completed with QMP acknowledgement,
guest shutdown, QEMU exit 0 and no fallback. No browser was launched in that
preflight. Prior evidence and runtimes were retained.

Next: finish the matched combined build; verify complete runtime fonts and
metadata; initialize a fresh owned prefix; exercise Explorer/taskbar/Start menu,
Notepad editing/maximize/restore and 32-bit process startup; run normal sandboxed
Chromium against the original loopback fixture and inspect HTTP, title/nonce,
network-service command, screenshot and exact cleanup. Then compare the v11
Wine feature in the same environment and merge only validated changes.

Private artifacts: D:/DeepThought/ChromiumLab/development-validation-20261101.
Build: /var/tmp/wex-development-validation-20261101 in D-backed Debian WSL.
