# Guest recovery and cleanup result — 2026-10-31

## Outcome

Parent performed the user-authorized maintenance directly. Existing VM retained; no replacement image or browser reinstall. Guest startup is recovered and persisted across a clean cold boot. Chromium and Edge were not run during maintenance; this result establishes the environment/startup prerequisite, not browser or token-security acceptance.

- Removed 26 exact obsolete October22 test prefix directories, reclaiming 49,936,887,808 bytes (46.51 GiB) before adding the recovered runtime and validation prefix. Final available space after recovery was 48,551,337,984 bytes (45.22 GiB), versus initial 2,620,297,216 bytes. Inodes were not exhausted.
- Preserved all parent result/log/screenshot directories, current candidate, working control/R5 runtime, original runtime, installed Edge/browser prefix, private browser payloads and compositor. Archived retired prefixes registry hives on host and verified SHA-256 before deletion. Checked canonical paths, device/inode identity and inactivity for the exact deletion allowlist. Retention/deletion manifests are in the private artifacts below.
- Installed the missing Linux runtime libraries and strace diagnostic tool. All 36 deployed Wine Unix modules now resolve their linked libraries; dpkg --audit is empty. No existing package upgrade was requested; dependencies were installed without recommendations. Initial apt indexes were stale and two security-package URLs returned404; a normal apt-get update followed by installation completed successfully. Exact versions and installer output are preserved.
- Reused the installed private headless labwc/Wayland compositor, verified project protocol and actual outer VirGL renderer on every startup validation. No guest Xvfb installation or replacement graphics stack.

## Actual initialization failure and repair

Restoring the compositor and freeing disk space alone did NOT resolve candidate initialization: the preserved runtime still timed out after100s. A12s narrow syscall trace of the exact owned wineboot/services/rundll32 processes exposed repeated source-directory searching for winmetadata, with rundll32/setupapi consuming83%CPU. The deployed tree contained no *.winmd files. Its loader/wine.inf explicitly lists ten WinmdFiles and uses WineSourceDirs WinmdFiles=winmd,include. The matching host build contains all ten under build/include, totaling111104bytes.

Created an isolated runtime copy, added only those ten hash-verified resources, and tested a fresh prefix. Initialization then exited0 in10.006s, followed by x64 and x86 console PEs each exit0/PASS. After dependency completion, a fresh prefix against the named recovered runtime initialized in8.188s and both architecture probes again passed. The observed setupapi error1812 for devmgmt.msc was not the terminal failure boundary. No speculative token patch was made.

The recovered runtime is an independent physical copy, not a set of hardlinks to a historical runtime. All1777 regular files have independent inodes relative to the initial hardlinked staging tree; full content manifest is preserved. Temporary staging copies and the two failed/superseded maintenance prefixes were removed. The validated prefix remains stopped and available.

## Canonical handoff

- Recovered runtime: `/home/debian/wex-g4/wine-recovered-r6-20261031/build/wow64`
- Validated clean prefix: `/home/debian/parent-maintenance-20261031-1793483167158/startup4/prefix`
- Executable recipe: `/home/debian/parent-maintenance-20261031-1793483167158/startup4.py`; choose a new output directory for another run, not its consumed output.
- Durable handoff: `/home/debian/wex-g4/wine-recovered-r6-20261031/RECOVERY_README.md` and `recovery-summary.json`
- Runtime source is the preserved ce87bac0 production baseline with added metadata, NOT f2e67abe or corrected e1a90b94 server. Actual server SHA-256 remains0323c006eab58eb9ba975675e2345750bf061aab53ec56954d82952ad13b67ab. Do not relabel this as a reconciled server A/B result.

Use existing OwnedWexcomp with /home/debian/wex-g4/labwc and wayland config. The recipe provides private XDG_RUNTIME_DIR/wayland-runtime, WAYLAND_DISPLAY=wex-shell-0, LABWC_WEX_SHELL_SOCKET=wex-shell-0, WEX_WAYLAND_NATIVE_WINDOWS=1, explicit VirGL and native winewayland driver; DISPLAY is absent intentionally. Initialization used recorded optional Mono/Gecko suppression and disabled winemenubuilder. Clone the stopped validated prefix for destructive tests. All linked libraries resolving is not a claim that every optional camera/scanner/OpenCL/media feature has been exercised.

On the clean cold boot, the existing validated prefix started the x64 PE in0.673s (exit0/PASS); x86 also exited0/PASS. Dpkg audit remained empty and no recovery/corruption error was emitted in the cold-boot health query. Protected original server/compositor/Edge binary hashes remained unchanged. Initial maintenance boot replayed the journal after the preceding workers forced shutdown; recovery completed.

Both parent maintenance runs independently recorded QMP capabilities/powerdown acknowledgements, guest SHUTDOWN, QEMU exit0, graceful_powerdown=true, fallback_used=false and QEMU/Xvfb cleanup. Final host process inventory contained no QEMU, launch.py, wineserver or Xvfb. Guest is stopped and maintenance ownership released after the handoff message.

## Evidence and next work

Private host evidence: `D:/DeepThought/ChromiumLab/parent-maintenance-20261031-1793483167158/`. Contains exact removal manifest, protected hashes, retired registry hive archives, full recovered runtime manifest, package versions/install logs, source scripts, failed and passing startup evidence, cold-boot health and shutdown-verification.json.

- retired-prefix-hives.tar.gz SHA256:3aa72184379789b03d39f95f79baf4c61feb1eb6b1eeba580dfac0ce8eafea75
- maintenance-evidence.tar.gz SHA256:112eaa76ed254f7cf1f327f51531181bd6333a17891553f311f23bbd8e7eb0fb
- coldboot-evidence.tar.gz SHA256:8da21d47c9e077623a1fbf6a0b41a8203c126977217a60ec11b349906942f1c4

Coordinator should correct the existing runtime packager/closure manifest to include all actual wine.inf required data, specifically include/*.winmd, and add a focused missing-resource preflight. Reuse this recovered environment for the already assigned corrected0x88 Mojo/native descriptor comparison, distinguish baseline and reconciled servers, then resume supported fixes/normal Chromium verification. No new VM or repeated startup-investigation assignment is needed. The user-authorized later master integration and Edge target remain separate.
