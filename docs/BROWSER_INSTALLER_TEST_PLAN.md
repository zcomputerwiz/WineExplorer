# Browser installer test plan (WoW64 Wine in QEMU)

## Scope and evidence boundary

This is a static, execution-ready plan. The inspected repository and
`D:/DeepThought/ChromiumLab` contain evidence for one Google Chrome enterprise
MSI path and for launching already-present Chromium/Edge browser payloads. They
do not contain a verified Chromium continuous-build installer download, a
Chrome consumer stub download, or an Edge installer download. Those rows must
remain unverified until the corresponding files and provenance are recorded.

Do not run installers or VMs as part of this document review. For a future run,
use an unmodified vendor file, a fresh WoW64 prefix, a preserved guest snapshot,
and archive the file hash, signature evidence, exact command, exit status, MSI
log, installed files, registry/services/tasks, and a screenshot of the
presented browser pixels.

## Verified lab baseline

The checked-in lab launcher uses the reviewed overlay
`system-native-identity-r4-20261024.qcow2`, with a 16 GiB guest, four vCPUs,
virtio networking, user-mode NAT, and host-forwarded SSH
`127.0.0.1:2235 -> guest:22`. The launcher comment says NAT is intentional for
Debian provisioning. It does not prove general guest HTTPS egress, DNS, proxy
configuration, or certificate success.

The lab’s validation scripts create fresh prefixes below their run output:
`/home/debian/development-validation-20261101/<run>/prefix` for the generic
Chromium and Chrome-MSI fixtures, and the Edge fixtures use the same
`<run>/prefix` convention below `/home/debian/development-validation-20261101`.
The MSI tracing scripts separately use:

* `/home/debian/msi-svc-r4-20261105/prefix`
* `/home/debian/msi-fixval-20261105/prefix`

The scripts set `WINEARCH=win64`, use the WoW64 build at
`/home/debian/development-validation-20261101/memory-runtime2/build/wow64`,
and enable the private-loopback check. The browser fixture URL actually used by
the validators is generated as
`http://127.0.0.1:8765/index.html#nonce=<random-token>`; it is a local fixture,
not vendor network validation. No proxy URL or proxy credentials are recorded
in the inspected lab scripts/log summaries. General guest network state is
therefore unverified.

## Fixture-to-browser mapping

| Fixture | Verified purpose and browser | Verified install prerequisite |
|---|---|---|
| `development-validation-20261101/validate.py` | Launches an already-present Chromium `chrome.exe` from `chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win/chrome.exe`; validates the local fixture and captures `chromium.png`. | No installer; the payload is pre-existing. |
| `wine-11.21-integration-20261101/validate.py` | Same style of pre-existing Chromium launch, using the separate Wine-11.21 validation root. | No installer; the payload is pre-existing. |
| `msi-trace/validate-gchrome.py` | Runs Google Chrome enterprise MSI, expects `C:\Program Files\Google\Chrome\Application\chrome.exe`, launches it, checks the local fixture, and captures `chromium.png`. | `googlechromestandaloneenterprise64.msi`. |
| `edge-dev-r1-20261101/validate-edge.py` | Launches an already-present Edge payload at `.../edge-install-r1-v2-20261022/prefix/drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe`; validates the local fixture. | No installer; the payload is pre-existing. |
| `edge-dev-r2-20261101/validate-edge2.py` | Same Edge validation pattern with the r2 validation root and the same recorded pre-existing Edge payload path. | No installer; the payload is pre-existing. |

The fixture scripts record `fresh_prefix=true`, `private_loopback`, browser
arguments, browser liveness, fixture result, and screenshot output. They do not
establish that an installer downloaded or installed the browser.

## Installer matrix and verified commands

### Google Chrome enterprise MSI

The verified file name is `googlechromestandaloneenterprise64.msi`, stored in
`D:/DeepThought/ChromiumLab/chrome-dl/` and copied into the guest through the
development-validation path. The repository analysis records product version
`156.0.8078.80`, product code
`{50354927-E098-35AA-A0B1-3226D8CB43B6}`, and MSI custom action
`DoInstall`. The actual lab command is:

```text
msiexec.exe /i Z:\home\debian\development-validation-20261101\googlechromestandaloneenterprise64.msi /qn /l*v Z:\home\debian\<run>\msi.log
```

The lab analysis records the embedded install command as `--silent
--install="..." --installsource=enterprisemsi --appargs="..."`; do not turn
that embedded command into a consumer-stub command. The MSI is system-level in
the recorded `installerdata` (`system_level=true`).

Observed results in both the service trace and fixed-combase replay are not
installation passes: `DoInstall`/`InstallFinalize` returns 1627; the fixed
replay exits 91; `chrome.exe` is absent from the expected application
directory. The fixed replay does show
`GoogleUpdaterInternalService157.0.8089.0` registered and stopped, and traces
`StartService returned error 0 (ok)`, but it does not show a successful COM
class registration or browser installation.

The registry/ACL issue is not closed. The analysis identifies a `WRITE_DAC`
requirement and leaves the SYSTEM-token/default-DACL ownership candidates open.
The lab contains `reg-acl-probe-x86.exe` and `reg-acl-probe-x64.exe`, but the
inspected MSI outputs do not provide a completed probe result that isolates the
failure to the service account versus key ACL creation. Record it as
`unresolved registry-DACL blocker`; do not loosen ACLs as a test workaround.

### Chromium continuous-build installer

The plan previously named `mini_installer.exe`, `chrome.7z`, `LAST_CHANGE`,
and `--silent --do-not-launch-chrome`/`--system-level`, but none of those
files, build values, download URLs, or switch checks is present in the inspected
lab. The exact per-user and system-level commands are therefore unverified. Do
not claim an expected install directory, registry state, or absence of services
until a specific official build and both matching files are recorded.

### Google Chrome consumer stub

No `ChromeSetup.exe`, download URL, or stub help/output is present in the
inspected repo/lab. `/silent /install`, Omaha behavior, updater creation, and
the proposed per-user mode are unverified. The test row is blocked until the
file and its official provenance are recorded; guest HTTPS/DNS/proxy readiness
must also be separately verified.

### Microsoft Edge MSI and stub

No Edge installer MSI, `MicrosoftEdgeSetup.exe`, official download URL, or
installer switch evidence is present in the inspected repo/lab. The Edge
validator scripts only launch a pre-existing `msedge.exe`; they do not validate
an Edge installer. The proposed `/silent /install`, service names, scheduled
tasks, and system/per-user behavior are unverified.

## Future execution gates

For every installer that becomes available, record the exact filename, official
download URL as actually used, version/build, SHA-256, signature result, guest
snapshot identifier, fresh-prefix path, proxy configuration (without secrets),
and network checks. Keep vendor installers unmodified.

For the Chrome MSI, run the existing `validate-gchrome.py` only after the MSI
step and archive its JSON/stdout, stderr, exit status, fixture result, and
`chromium.png`. For pre-existing Chromium and Edge payloads, use only the
browser-specific fixtures in the mapping table; do not label those runs
installer validation.

Acceptance requires both the validator result and a screenshot showing the
presented browser pixels. A zero installer exit code, an installed file, or a
service entry alone is insufficient. Uninstall, reboot-equivalent cleanup,
reinstall, and rollback need separate fresh-prefix/snapshot evidence.

## Open questions — answered from the checked evidence

1. **Chromium build and switches:** no continuous-build number, `LAST_CHANGE`,
   matching `mini_installer.exe`/`chrome.7z`, or supported silent/system-level
   switch is recorded. This remains unverified.
2. **Google MSI identity and properties:** the verified filename is
   `googlechromestandaloneenterprise64.msi`; the recorded MSI invocation is
   `/i ... /qn /l*v ...`; the embedded custom-action properties are the
   `--silent --install=... --installsource=enterprisemsi --appargs=...` form
   described above. The registry-DACL failure is not isolated to either the
   service account or key ACL creation.
3. **Snapshots, prefixes, proxy, and mandatory fixtures:** the launcher overlay,
   guest NAT/SSH setup, prefix roots, and fixture-to-browser mapping are listed
   above. No approved snapshot pair or proxy configuration is recorded, and no
   fixture exists in the lab for the absent Chromium/Chrome-stub/Edge-installer
   files. Those items remain unverified.

## Corrections

* Removed unsupported claims about Chromium continuous-build files, build
  selection, installer switches, Chrome/Edge stub switches, URLs, and service
  expectations.
* Replaced generic fixture language with the verified mapping of each
  `validate-*.py` script to a pre-existing Chromium, Chrome-MSI, or Edge
  payload.
* Added the real lab overlay, NAT/SSH, fresh-prefix roots, local fixture URL,
  and the distinction between local-loopback validation and internet access.
* Recorded the verified Chrome MSI filename, command, product identity, 1627/91
  results, and the still-unresolved registry-DACL status.
* Answered all open questions with verified facts or an explicit unverifiable
  status.
