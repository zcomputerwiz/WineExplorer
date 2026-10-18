<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order for Gemini: optional downloads during OOBE

Date: 2026-10-17. Status: ready for implementation, not implemented by this document.

## Outcome

During Setup's Out-of-Box Experience, offer the user an optional, skippable
step to download and install supported components that cannot be preloaded or
bundled. Show the selected vendors' actual terms and obtain explicit acceptance
before downloading their redistributable payloads. Install accepted components
into the shared machine root, not the temporary setup prefix. Leave a working
installed system when the user skips, is offline, declines, or encounters an
installation failure. Offer the same supported operation later through Windows
Components, without maintaining two installation engines.

This is implementation work, not just a design or a button that launches the
current chooser. The default image must not require an administrator to manually
create vendor-license text files before this OOBE feature can work.

## Starting point and branch

Start a separate `gemini/oobe-components` branch from `codex/fonts-runtimes`,
including this work order. Its minimum repair baseline is `f072c1b8`, containing
implementation commit `80df0b04`. Verify those commits are present before
starting. Do not start from
the rejected `gemini/fonts-runtimes` branch (`dfd42ab0`). If the repair has since
been merged into master, use that reviewed descendant instead and record the
actual base SHA. Do not reset an occupied/dirty checkout to change its base.

The repair is not yet merge-ready: this work must retain its protections and
must not represent its source-build smoke tests as full installed-ISO acceptance.
Read these completely before editing:

- `CLAUDE.md`, `docs/FONTS_RUNTIMES.md`, and
  `docs/reviews/fonts-runtimes-20261017.md`;
- `docs/FONTS_RUNTIMES_WORKORDER.md` for original requirements, but treat its
  historical status and environment instructions as superseded by the repair
  report and this work order;
- the relevant setup, OOBE, shared-prefix and host-bridge source below.

Preserve unrelated local edits, particularly the existing `.gitignore` change.
Commit coherent working steps. Do not merge or push master, force-push, start
another agent, or message another chat. Supply the completed branch and report
for review; this work order does not dispatch a Gemini chat automatically.

## Current code and pitfalls to inspect

- `src/programs/oobe/oobe.c`, `oobe.rc`, `resource.h`, `Makefile.in` implement
  Welcome, Updates, Connectivity, Users, Password and Finish pages. The page
  array is currently sized for six pages. Resize it safely when adding pages.
- `OobeFinishProc` calls `ProvisionUsers()`. Trace its completion marker and
  error handling before changing when OOBE is considered complete.
- `installer/scripts/wex-oobe.sh` runs `oobe.exe` as **wexsetup**, using
  `/etc/skel/.wine`. It waits for `stage-oobe-done`, then tears down the display,
  removes the temporary account/setup files and starts the display manager.
  Do not signal completion while an install, worker, or consent interaction is
  still running; do not lose the results during cleanup.
- `installer/systemd/wex-oobe.service` starts that runner as a system service.
  The setup operator may not yet have a provisioned administrator account.
- The runner currently recursively chowns `/etc/wineexplorer` to wexsetup.
  The repaired installer requires root-owned trusted license files under
  `/etc/wineexplorer/component-licenses`. **Fix this ownership conflict**:
  constrain setup-writable state to its own directory, or relocate trusted
  state consistently. Do not weaken license ownership checks to make OOBE work.
- `scripts/wex-components.py` contains the supported-component policy,
  consent/installer split, sanitized root execution, maintenance lock, protected
  builtin recovery and success receipts. Its current root-owned text-file
  requirement is a safe gate, not the finished OOBE license-retrieval workflow.
- `scripts/setup-machine-root.sh`, `installer/package-wine.sh`,
  `installer/wex-components.desktop` and `src/dlls/wexhost/` provide integration
  points. Prefer a narrowly scoped existing host/D-Bus mechanism over spawning
  arbitrary shell commands from Wine. Inspect existing authorization rather
  than assuming that a D-Bus method or a completion-marker file proves consent.

## User experience

1. Add a native page consistent with the existing XP-style OOBE. Do not place
   a standalone Zenity window over the wizard as the finished experience.
   Position it after connectivity is known and before the final completion
   page; adapt account provisioning order only as needed for safe authorization.
2. Explain that these are optional compatibility downloads, not required to
   finish setup. Show useful names, purpose, publisher, known size/unknown-size
   status, and any reboot requirement. Do not invent size or progress values.
3. Offer only `vcrun2022`, `corefonts`, `tahoma`, `d3dcompiler_47`, `xact`, and
   `physx`. All start unchecked. Explain that open replacement fonts and Wine's
   builtin compiler remain the defaults; selecting proprietary fonts/compiler
   is an explicit optional choice, not a repair needed for the shell to work.
4. Provide explicit Review terms, Accept and install, and Skip/install later
   paths. Every selected component needs its applicable terms; accepting one
   publisher/package must not accept the others. Back/Next and changed
   selections must preserve or invalidate consent correctly.
5. Do not fetch anything merely because OOBE starts or the page becomes active.
   An explicit Review terms action may retrieve license documents/metadata;
   explain that network access. Redistributable binaries must not be fetched
   until the corresponding terms have been displayed and accepted.
6. Offline, unavailable terms, TLS/download errors and unsupported package
   versions must give an honest explanation and allow Skip. Keep the wizard
   responsive. Show real per-component state/progress and failures, with Retry
   or Continue without this component. Never display unconditional success.
7. Cancellation must be safe. If an MSI cannot be safely interrupted, explain
   that and wait for that bounded operation to reach a safe point instead of
   killing it, the whole wineserver, or the setup session indiscriminately.
8. Finish summarizes installed, declined, skipped and failed components and
   explains how to open Windows Components later. Setup cannot require a
   network connection or successful optional installation to complete.

## License and download handling

Implement automatic acquisition of authentic full vendor terms using curated
official sources. Inspect the **actual installed Debian winetricks recipes**:
pair terms with the precise package version, architecture, download URL and
integrity metadata those recipes will use. Do not accept terms for an older
package and then let a mutable latest URL deliver a different product/version.
Verify support for all six verbs in the packaged winetricks version. If a
particular verb cannot be safely version/terms-paired, disable it with a clear
reason and report that gap; do not silently pass it through to winetricks.

Store only freely redistributable code/metadata in the repository and ISO.
Do not commit vendor documents or proprietary payloads/cache contents. Retrieve
those on the user's installed system, following their explicit actions. Handle
the actual document format: an HTML navigation page, generic notice, link alone,
truncated extraction, or unreadable embedded document is not the full terms.
Preserve readable headings/clauses and any material linked terms. Do not assert
blanket licensing permission; flag unresolved applicability or distribution
questions for review rather than bypassing them.

The privileged side independently verifies the trusted metadata, retrieved
document digest and exact package identity. Bind consent to that identity and
digest; any changed terms or package requires renewed acceptance. A caller-
writable text file, arbitrary URL, hash or "accepted=true" flag alone must not
authorize a root download. Secure cached terms/metadata against symlink swaps,
TOCTOU changes and unsafe ownership. Do not log passwords or other credentials.

OOBE completion must not delete the trusted terms, receipts or install results
needed by the later Components launcher. Store only the audit data needed to
identify the component/version, terms digest, action and outcome. Unattended
setup must skip these downloads by default; no blanket unattended EULA consent
or production consent-bypass switch is authorized by this work order.

## Privilege and installation contract

Do not add wexsetup to sudo or make a broad passwordless/pkexec exception.
Normal post-install use must retain administrator authentication and deny
limited users. For OOBE, use the root-owned setup coordinator with a tightly
scoped, stage-limited protocol to the unprivileged wizard. Restrict caller
identity, operation and request format; validate every request at the privileged
boundary. Do not expose arbitrary commands, environment variables, prefixes,
verbs, file paths or download URLs. Revoke the setup-only authority on completion
and on aborted setup, and reject replay after that stage ends.

Reuse/refactor the repaired installer policy rather than duplicating it:

- Destination is `/var/lib/wineexplorer/machine`; shared cache is
  `/var/cache/winetricks`, accessed through the privileged helper. Never install
  into `/etc/skel/.wine`, wexsetup's profile, or the first user's private prefix.
- Serialize maintenance; do not block on the logged-in user's desktop/server
  or rewrite shared files from the ordinary per-user graphics switch.
- Preserve the never-override policy for core Wine DLLs, including indirect
  `ucrtbase`, `msxml*`, `mf*` and AppDefaults/override side effects. Retain
  recovery snapshots on failure; do not claim complete MSI rollback if only
  protected files/keys are restored. Check the success path as well as failures.
- Verify installed redistributables work from both administrator and limited
  split prefixes, in both architectures where applicable. Do not assume that
  root-prefix HKCU settings automatically reach every user's HKCU. Publish only
  narrowly required component settings without changing unrelated preferences.
- Preserve machine-root ownership, permissions/default ACLs, shared hives,
  prefix templates, native builtin defaults and unrelated user overrides.
- Persist per-component results for safe retry/resume; never re-download or
  reinstall an already verified completed component just for revisiting a page.
  Interrupted work must not be mistaken for a successful receipt.

Do not change the Wine source pin, theme engine, Luna art, graphics architecture
or Wayland session policy for this task. Small setup/host-bridge changes directly
required for OOBE are in scope; explain them. Document any unavoidable stock
Wine change in `docs/STOCK_CHANGES.md`. Preserve upstream license headers and
add GPL-2.0-or-later notices to new project files.

## Verification and acceptance

Run the repair's existing 22 regression tests and add focused tests for the new
page/controller, request authorization, package/terms pairing and state machine.
Fixtures may use obviously synthetic terms and fake downloads/installers, but
must be clearly labeled, isolated, and impossible to enable as a production
consent bypass. Do not fabricate a user's acceptance to run real proprietary
install tests: obtain actual human review/acceptance, or report that test blocked.

Required cases:

- No network access on opening OOBE/page; metadata only after Review; no payload
  before acceptance. Decline, no selection, offline, missing/malformed terms,
  changed terms/package, TLS failure and rejected hash all download no payload.
- Mixed selection with separate accept/decline decisions; Back/Next, duplicate
  requests, failed installer exit, retry, interrupted setup/reboot and resume.
  No false success, wizard hang, forced network requirement or lost outcome.
- An ordinary limited account, another local user, expired setup stage, forged
  request, replayed consent, caller-controlled path/URL and symlinked state cannot
  invoke privileged installs. Normal administrator authentication still works.
- Native page layout/keyboard/tab order at 800x600 and the standard setup
  resolution, Classic and Open Luna. No clipped controls or illegible text.
  Use the XP VM as a visual reference where useful; do not copy or trace its art.
- Build combined WoW64 (`--enable-archs=i386,x86_64`), test the OOBE/installer
  path and a 32-bit regression. Never substitute a 64-bit-only build.
- Fresh installed ISO: exercise the real OOBE step; skip successfully offline;
  test an accepted component online when human consent is available; then use
  it from administrator/limited prefixes in X11 and Wayland. Verify default
  Wine Mono, Gecko, open fonts and the graphics switch still work.
- Confirm trusted metadata/terms ownership before, during and after OOBE;
  the wexsetup cleanup leaves no privileged endpoint, account permission or
  broad authorization behind. Logs and recoverable snapshots remain accessible
  only as intended. Do not power off/reconfigure unrelated VMs or kill unrelated
  Wine/display processes.

Use disposable prefixes/displays/namespace resources and clean up only your own
processes. Follow current workspace/VM instructions; do not reuse another agent's
WSL distro or checkout. Ask before unavailable infrastructure or new external
authority is needed. No globally destructive acceptance runner or `killall`.

## Deliverables and review gate

Deliver the implementation branch, reproducible tests and
`docs/OOBE_COMPONENTS_ACCEPTANCE.md`. Record the base/tip SHAs, build/package
versions, precise commands/results, real human-consent test status, known gaps,
failures with exact causes/workarounds, ISO size change and retained VM/process/
prefix state. Include only open-art screenshots from the actual tested install;
no Windows logos, proprietary wallpaper or copied Gemini captures.

Update `docs/FONTS_RUNTIMES.md` and `docs/HANDOFF.md` to reflect what actually
works and what remains pending. Do not mark the feature complete because mock
tests pass, or the wizard merely launches a chooser. Explicitly distinguish
source-build tests, fixture tests and installed-ISO acceptance. Leave merge and
publication to review; do not merge the repair or this OOBE branch into master.
