# Official browser installer test plan (WoW64 Wine in QEMU)

## Scope and gates

Test the unmodified vendor installers in the WoW64 Wine runtime inside the two
QEMU guests. Never execute an installer on the Windows host. This is a test
plan, not an authorization to patch Wine. Record facts separately from
hypotheses; a failed installer is not a Wine regression until the installer,
prefix, network, and signature/hash are identified.

Before each run, snapshot/clone the guest and create a fresh prefix. Keep the
host-side corpus at `D:/DeepThought/ChromiumLab/installers/<name>/`; retain the
original file, a SHA-256 manifest, download URL, build/version, timestamp, and
signature/hash evidence. Use only official vendor URLs. Check Authenticode
publisher and validity in the guest (or with an offline verifier) and require a
vendor-published SHA-256 where the vendor publishes one. Do not modify, unpack,
or recompress the installer before testing.

Prerequisites: guest clock is sane; `wine --version` and WoW64 are recorded;
guest has at least 12 GiB free (the guest disk is 79 GiB); and old prefixes are
removed only after a snapshot and an explicit result export. The documented
cleanup location is `/home/debian`; verify the exact prefix before deleting.

For an internet-dependent stub, check from the guest with `curl -I
https://www.google.com/` and `curl -I https://www.microsoft.com/` (or the
available guest browser), then test DNS, HTTPS certificate validation, and
proxy variables/configuration. Record proxy URL/auth policy without putting
credentials in logs. If egress is unavailable, stop the stub test as
`blocked: guest network`, not as an installer failure.

## Work order and two-VM schedule

* VM A: continue the Google Chrome chain: enterprise MSI first at the known
  updater-service registry-DACL blocker, then consumer `ChromeSetup.exe` only
  after guest internet is proven; next Edge MSI and stub.
* VM B: Chromium continuous-build installer first; run per-user and then
  system-level modes from separate fresh prefixes; then repeat the shared
  validation fixture and, if capacity permits, Edge MSI.
* Preserve snapshots before every install/uninstall. Do not run two installers
  in one prefix unless the test explicitly says it is a browser-upgrade case.

## Installer matrix

### 1. Chromium continuous build

Download `mini_installer.exe` and the matching `chrome.7z` from the official
Chromium snapshots bucket only:
`https://storage.googleapis.com/chromium-browser-snapshots/Win_x64/<build>/`.
Read the official `LAST_CHANGE` value (or a build number selected from the
official continuous-build index), pin that numeric build, and fetch both files
from that same directory. Record the build number and both hashes. The expected
installer flow is the self-extracting `setup.exe` plus `chrome.7z`; do not
substitute a third-party Chromium package.

Per-user (expected): `mini_installer.exe --silent --do-not-launch-chrome`
(hypothesis: confirm the exact switches supported by this build; if unsupported,
use the installer’s documented silent switch and record it). Expected target is
`%LOCALAPPDATA%\\Chromium`, with `Software\\Chromium` user registry state and
no service. System-level (hypothesis to verify):
`mini_installer.exe --system-level --silent --do-not-launch-chrome`; expected
machine install/registry state and still no service. Confirm whether the
installer requires the adjacent `chrome.7z` or embeds/extracts it.

### 2. Google Chrome

Download the official enterprise MSI from Google’s Chrome Enterprise download
page and the consumer `ChromeSetup.exe` from the official Chrome download page;
record the exact redirected URL, version, signature, and vendor hash if given.

Enterprise MSI, system mode:
`msiexec /i "Z:\\corpus\\GoogleChromeEnterprise.msi" /qn /norestart`
(hypothesis: add the vendor-documented MSI properties only if the checked-in
analysis requires them). Capture the currently known blocker: updater-service
registry DACL access denied. Do not loosen ACLs as a test workaround; preserve
the failing trace and compare service creation/start, registry ownership, and
rollback. The MSI is the controlled offline/system baseline.

Consumer stub, per-user/default mode (hypothesis; verify in the analysis and
installer help): `ChromeSetup.exe /silent /install`. Run only with internet.
Contrast it with the MSI: the stub downloads payloads through Omaha/Chrome
updater, may create/update the updater service, performs network and elevation
decisions, and is sensitive to WinHTTP/WinINet, certificates, and proxy state.
Do not call stub success merely because setup exits 0.

### 3. Microsoft Edge

Download `MicrosoftEdgeEnterpriseX64.msi` and `MicrosoftEdgeSetup.exe` only
from Microsoft’s official Edge for Business/download pages. Record signature,
vendor SHA-256 where published, version, and final URL.

Enterprise MSI, system mode:
`msiexec /i "Z:\\corpus\\MicrosoftEdgeEnterpriseX64.msi" /qn /norestart`.
Stub, internet-dependent per-user/default mode (hypothesis; verify the current
Microsoft switch): `MicrosoftEdgeSetup.exe /silent /install`.
Expected differences are the MSI’s controlled system deployment versus the
stub’s MicrosoftEdgeUpdate service, scheduled tasks, and elevation service.
Record exact service/task names rather than assuming Chrome names.

## Acceptance and evidence

For every mode, capture: process exit code; install log; installed files and
versions; uninstall entry (DisplayName, version, uninstall command, scope);
services and scheduled tasks; shortcuts/start-menu entries; and browser launch.
Launch the installed browser and save a guest screenshot showing a known local
test page plus the browser chrome. Run every applicable `validate-*.py` fixture
from `D:/DeepThought/ChromiumLab/` and archive stdout/stderr, exit status, and
fixture version. Functional acceptance requires the fixture result and the
presented screenshot, not merely files or a zero installer exit code.

For Chromium specifically, assert `%LOCALAPPDATA%\\Chromium`, user
`Software\\Chromium`, no service, and uninstall behavior. For Chrome/Edge,
assert the documented updater/elevation services and tasks, their start state,
ACLs, and removal after uninstall. Test uninstall, reboot-equivalent service
cleanup, reinstall, and rollback to the pre-install snapshot.

## Logs, traces, and likely Wine surface

Start small: `WINEDEBUG=err+all,warn+all,fixme+service,fixme+msi` plus the
installer’s own log, then add one focused channel (`+reg`, `+process`, `+rpc`,
`+ole`, `+taskschd`, or `+winhttp`) only for a reproduced failure. Use separate
per-step files and timestamps; cap/rotate logs. Do not repeat the 12-GB trace
failure by enabling `+all` for a full install. Save `WINEPREFIX`, Wine build,
guest disk state, command line, and environment with each trace.

Likely touched areas are services; COM local servers/RPC; taskschd; MSI custom
actions; elevation; WinHTTP/WinINet; crypt/Authenticode; shell links/menu
builder; and WoW64 directory/registry redirection. These are investigation
surfaces, not proof that a component is defective.

Triage ownership: Qwen shell+harness for a narrowly reproduced Wine fix;
Antigravity for lab/VM, guest networking, snapshots, and disk infrastructure;
Codex for read-only review, evidence correlation, and acceptance wording. Any
Wine change requires a new fresh-prefix run and the same screenshot/fixture
checks; do not edit code as part of this test run.

## Results table (one row per fresh prefix and mode)

| Vendor/file/build | VM/prefix | Mode/command | SHA/signature | exit/log | files+uninstall | services/tasks | launch/screenshot | validate fixture | verdict/blocker |
|---|---|---|---|---|---|---|---|---|---|
| Chromium `mini_installer.exe` + `chrome.7z` |  | user/system |  |  |  | none expected |  |  |  |
| Google Chrome enterprise MSI |  | system |  |  |  | updater |  |  |  |
| Google `ChromeSetup.exe` |  | stub/user |  |  |  | Omaha/updater |  |  |  |
| Edge enterprise MSI |  | system |  |  |  | update/tasks/elevation |  |  |  |
| `MicrosoftEdgeSetup.exe` |  | stub/user |  |  |  | update/tasks/elevation |  |  |  |

## Open questions before execution

1. Which exact Chromium build should be pinned if `LAST_CHANGE` moves during
   the run, and is the desired system-level Chromium switch confirmed in the
   checked-in analysis?
2. What exact Google enterprise MSI filename/URL and current MSI properties are
   recorded in `CHROME_INSTALLER_ANALYSIS.md`, and has the registry-DACL failure
   already been isolated to service-account versus key ACL creation?
3. Which two QEMU guest snapshots/prefix paths and proxy configuration are the
   approved lab baseline, and which `validate-*.py` fixture is mandatory for
   each vendor?
