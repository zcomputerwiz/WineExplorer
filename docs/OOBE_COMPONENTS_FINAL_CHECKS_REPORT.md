<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent OOBE components repair and current acceptance

Date: 2026-10-18. Branch: `codex/oobe-final-correctness`.
Final source-suite rerun and publication checkpoint: 2026-10-19.
Base: Gemini `625effa88630fd201da0856093a74a744beefcab`, candidate
`5bc13c9ce182eb0114af80372fc748a519ee32b2`. First repair: `a151ceca`.
Wine remains `ed5fe967ed471ca524666631ad70dabbed6af076`; no graphics,
token-security, compositor or proprietary assets were imported.

**Decision: repaired functional cases below pass; full fresh-image and visual
acceptance remains incomplete. Do not merge to master on the old report.**
The previous report is archived in `archive/OOBE_COMPONENTS_GEMINI_CLAIMS_20261018.md`.
Its PASS cells and recovery-command claims are not independent evidence.

## Repairs

- Added a real root-admin reconciliation CLI instead of direct journal editing.
  It excludes simultaneous backend actions, holds exclusive coordinator and
  maintenance leases, checks an idle matching machine Wine server with a bounded
  wait, requires an inspection note, validates private journal ownership/mode,
  and atomically records FAILED with audit metadata. It never guesses SUCCESS
  or claims rollback. Live coordinators, missing/completed IDs, unsafe journals,
  busy servers, missing inspection and recovery-required markers remain blocked.
- Install later now also refuses an unresolved UNKNOWN OOBE journal. It cannot
  bypass that maintenance gate by avoiding the wizard.
- Replaced the fallback linking both regedit architectures to ../regedit.exe.
  Missing or empty launchers are repaired from the corresponding installed
  Wine x86/x64 PE. The known legacy wrong-architecture link is migrated; valid
  images are retained. Nonempty corrupt/unknown images and untrusted installed
  sources fail closed, rather than being overwritten by a broad DLL fix.
- The Gate E fixture exercises this production recovery API, stops/joins its
  own coordinator before restart, and waits for actual listening readiness.
  It no longer edits a journal and labels that a production recovery command.
- Corrected all ten inherited Core Fonts package hashes and added the omitted
  Arial Black package. Validation now compares the full pinned corefonts
  dependency download manifest, not filenames/hashes found anywhere in a script.
- Replaced categorical/stale disabled reasons with actual pending validation.
  No additional component is enabled merely because a download/extraction works.
- Added an offline capture audit that checks actual PNG dimensions and uses a
  white-text OCR pass to reject login screens labeled as desktops. It deliberately
  does NOT infer theme/backend/state or certify XP parity from a filename.

## Independent results and scope

All source tests use the exact repaired files in an isolated native Debian copy.
Root-owned fixtures are private, not the real machine state. See
`evidence/oobe-final-correctness-20261018.json` for retained results/hashes.

| Check | Result | Boundary |
| --- | --- | --- |
| Runtime repairs | 57 discovered, 56 pass, 1 explicit live-Wine skip | Not full feature acceptance |
| Terms/cache | 11 pass | Synthetic document/provenance fixtures |
| Runtime artifact/defaults | 14 pass | Synthetic extraction/defaults |
| Unix bridge | 5 pass | Production bridge seam, not real OOBE navigation |
| New recovery/regedit guards | 14 pass | Production functions/CLI in private fixtures |
| Capture audit tests | 4 pass | Synthetic snapshots/OCR fixtures |
| Gate E scenarios | 3 pass | Mock installer; real coordinator/recovery API |
| Native OOBE PE helper | 54 assertions per x86/x64, both pass | Installed product Wine in owned clone; not full wizard navigation |
| Fresh built-Wine prefix | Boot 0; cmd x86/x64 exit 37; no empty PE files | New owned prefix, not installed image |
| Fresh installed-Wine prefix | Same results in clone | Existing installed image, fresh prefix |
| Full machine/template setup | Exit 0, no empty system32/syswow64/winsxs PE files observed | New isolated machine/template paths in clone |
| Deliberately empty x86 regedit | Correct i386 link; import 0; query 0/value 0x25 | Actual installed Wine, new disposable prefix only |
| Existing Gate G captures | 11/21 rejected | 4 falsely labeled 800x600; login-screen captures; not a passing visual matrix |

Native helper commands (both are ordinary main programs, NOT -municode):

```
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror scripts/test_oobe_native.c -o oobe-native64.exe
i686-w64-mingw32-gcc -Wall -Wextra -Werror scripts/test_oobe_native.c -o oobe-native32.exe
```

The installed-runtime probe is reproducible with
`scripts/tests/check-fresh-machine-bootstrap.py --wine /usr/local/bin/wine
--wineserver /usr/local/bin/wineserver --exercise-regedit BACKEND_SOURCE
--native-probe32 PROBE32 --native-probe64 PROBE64 --output-dir NEW_DIRECTORY`.
It creates/removes its own prefix/display and bounds command/server waits.
WSL requires private mount AND network namespaces and an owned /tmp; the guest
has its own sockets. No existing prefix or installed Wine PE is overwritten.

Gemini's broad claim that fresh WINELINKFAKEDLLS bootstrap necessarily creates
empty x86 DLLs was NOT reproduced: built Wine, installed Wine and full isolated
machine setup passed that check. The inherited manually repaired donor is not
fresh-ISO evidence. Do not replace arbitrary DLLs based on that unproven diagnosis.
An ignored winemenubuilder startup error 126 was logged during full setup despite
its disable override; it is retained, not labeled a clean desktop pass.

## Actual recovery procedure

For an interrupted operation, first leave/finish or stop the owning OOBE stage and
coordinator normally; do not kill an active installer. Inspect logs, actual installed
files/receipt and protected snapshots. Establish that the machine prefix is idle.
Then an authenticated administrator may invoke:

```
sudo /usr/local/bin/wex-components --reconcile-operation EXACT_OPERATION_ID \
  --reconcile-action FAILED --reconcile-note 'Describe the completed inspection; no secrets'
```

The operation must be UNKNOWN or interrupted RUNNING. A live coordinator or
machine server prevents this operation. A protected recovery marker prevents
it too: explicit snapshot restoration/verification remains necessary, and this
command deliberately does NOT remove that marker or discard snapshots. Fixing a
journal is not restoring files. A safe retry still revalidates terms and packages.
There is no GUI snapshot-recovery wizard; this limitation is not reclassified PASS.
The native failure dialog still offers its generic Retry/Cancel text for blocked
recovery states. Backend guards prevent the retry from bypassing recovery, but
administrator-specific recovery guidance remains a usability follow-up.

## Components still requiring work

See `OOBE_COMPONENTS_PACKAGE_STATUS.md` for observed package identities, primary
sources, terms findings and exact per-component gates. Only vcrun2022 remains
enabled as a candidate. Core Fonts is demonstrably available; Tahoma's historical
rights need verification; SDK compiler packaging/terms need binding; XACT needs
complete matched terms and architecture proof; PhysX needs a reviewed modern
recipe/package and functional tests on our Wine. Missing standalone DOCX is an
implementation limitation, not evidence of a legal prohibition.

The user authorized downloading/installing needed components in disposable labs
and confirmed prior Winetricks terms acceptance. That does not authorize bundling
vendor files/legal documents or production auto-accept. No such files are in Git.
Product-policy decision pending: explicit download-for-review for embedded terms,
with separate acceptance before execution/install. No shipping bypass was added.

## Remaining acceptance gates

- Rebuild a fresh ISO from this exact branch and audit inside squashfs/package
  contents, not only ISO filenames. Reinstall on a NEW disposable disk.
- Repeat full unprivileged OOBE review/decline/accept/progress/finish, empty-cache
  later-launch authenticated preparation, declined elevation, offline skip,
  normal-user runtime loading and unchanged user override precedence.
- Capture actual Classic/Open Luna and X11/Wayland sessions at both actual
  resolutions, before/after installation. Cover locked/unlocked taskbar, toolbar
  handles/overflow, tray expanded/collapsed/disabled, keyboard and terms layout.
  Reaching a login screen after killing explorer is not theme acceptance.
- Verify protected-state restoration/recovery on installed fixtures and revisit
  the startup error above; preserve any inherited versus new differences.

## Failed attempts, workarounds and environment state

- An attempted follow-up native-dialog patch was not executed: the automatic
  approval service hit its usage limit. After the user's continue request,
  approved reads resumed. No untested dialog changes were included.
- A final documentation read used `Get-Content STOCK_CHANGES.md` at the root and
  failed because that file does not exist; read `docs/STOCK_CHANGES.md` instead.
- A resumed WSL loop through `wsl ... -- sh -lc` lost its dollar arguments and
  failed `cp: missing destination file operand`. No source was copied by that
  failed loop. Passing the script via `--exec sh -lc` preserved arguments; the
  exact repaired files were then synced and the final suites rerun.

- This managed parent has an uninitialized Wine gitlink. Searching
  wine/dlls/ntdll/unix/loader.c and wine/programs/wineboot/wineboot.c returned
  OS error 3. Read the exact pinned native build source instead; no gitlink change.
- New private-journal checks first rejected the old 0644 test fixture with
  PermissionError: Untrusted OOBE operation journal; the coordinator readiness
  assertion then failed. Changed only that fixture to the production 0600 mode;
  the unsafe-journal negative test remains and final suite passes.
- Initial DirectX download candidate
  https://download.microsoft.com/download/1/1/C/11C60B77-2A2C-4AD9-9DFE-5AC9C5F9FE78/directx_Jun2010_redist.exe
  returned curl (22), HTTP 404. The documented historical Microsoft URL under
  8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14 succeeded, with a DIFFERENT hash
  from the historical pinned mirror. It was NOT substituted into production.
- The first resource probe guessed ADVEULA; wrestool returned 0 but EMPTY output.
  Inspected its actual resource list and extracted LICENSE, language 1033.
  Empty output is not accepted as terms. NVIDIA's installer is not a CAB:
  cabextract reported no valid cabinets; no extracted-terms/installation claim.
- The fresh-setup clone probe intentionally used cmd exit 37; vmtest maps any
  nonzero guest status to host exit 1. The dedicated JSON probe independently
  records and checks the expected 37 for both architectures.
- Gemini's reported -municode native build failed with undefined reference to
  wWinMain. Actual source defines main; dropping that switch linked both PEs.
- Initial OCR at original resolution missed white login text. Added the explicit
  threshold/3x pass used by vmtest; rerun rejects the actual login captures.
  The audit exits 1 intentionally on this rejected evidence; it is not a failed
  repaired-code assertion. The old branch also had trailing-whitespace failures;
  edited fixtures/reports were normalized before publication.
- The first whitespace-format patch used overlapping consecutive header hunks
  and failed `Failed to find expected lines` in the archived report. No partial
  archive edit occurred; one bounded header-block hunk succeeded.
- QMP ACPI shutdown did not finish within vmtest's 45-second deadline:
  `guest did not shut down cleanly; disk inspection must not proceed`.
  Requested `sync; systemctl poweroff --no-block` through the owned clone's root
  console. The serial peer closed before its command sentinel and vmtest raised
  ConnectionResetError (Errno 104). Subsequent PID checks confirmed our QEMU and
  transfer server were gone; no forced global termination or donor shutdown was
  used. An offline qcow2 consistency check is recorded separately; it is not a
  claim that filesystem recovery or the fresh-image acceptance gates passed.

Native lab root: /var/tmp/wex-oobe-final-20261018. It holds source, evidence,
private vendor downloads/legal inspection and an independent 30-GiB disk clone.
The donor VM/disk, its credentials and other workers' checkouts/builds were not
modified or reset. Our clone and loopback-only transfer server are stopped after
checks; disk, private fixture paths and evidence are retained for review.
No master merge is claimed or performed.
