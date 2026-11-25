<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Optional components: actual blockers, not blanket policy prohibitions

Research/check date: 2026-10-18. Branch: `codex/oobe-final-correctness`.
Follow-up 2026-10-19: see `OOBE_PARENT_PACKAGE_TERMS_RESEARCH.md`. The exact
SDK bootstrap now binds its own license to both compiler cabinets, and the
DirectX package contains a separate extractable end-user-runtime agreement.
The Tahoma recipe identifies IE 5.5 SP2 as the historical parent release; its
matching agreement remains unverified. These findings supersede the missing-
terms provenance observations below, but do not enable the three components.
Read OOBE_COMPONENTS_FINAL_CHECKS_REPORT.md for the independent acceptance scope.
This document is engineering research, not a blanket license-rights opinion.
Availability, terms applicability, installation and functional acceptance are
separate gates. A Winetricks verb is useful prior art, not a licensing grant.

## Current shipping policy

| Component | Status | Actual remaining problem |
| --- | --- | --- |
| vcrun2022 | Enabled candidate only | Exact 14.44.35211.0 x86/x64 extraction/artifacts are implemented; full rebuilt-image OOBE/later-launch and desktop acceptance still required |
| corefonts | Enabled | All 11 package payloads and 30 TrueType fonts verified against pinned SHA-256 evidence. Offline staging into winetricks cache and execution under /usr/bin/unshare --net prevents network access. Pre-existing font symlinks in drive_c/windows/Fonts are purged prior to winetricks font copy to prevent open-font symlink overwrite in /usr/share/fonts. Setup machine root guard protects fonts against symlink replacement. Font registration verified in HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts. |
| tahoma | Disabled pending rights/terms verification | Verified historical CAB contains the two fonts, but Core Fonts EULA does not automatically cover this separate IE language pack; exact applicable terms/use rights remain unresolved |
| d3dcompiler_47 | Enabled | First-party Microsoft Windows 8.1 Standalone SDK cabinet distribution integrated. License RTF extracted statically from sdksetup.exe Burn manifest (u1, 64,354 bytes -> 18,295-byte plain text, SHA-256 5b575dc8...); offline staging and unshared net execution extracts 32-bit (syswow64) and 64-bit (system32) d3dcompiler_47.dll PEs with exact SHA-256 verification. Machine root DLL overrides protected to retain Wine builtin default globally while enabling native compiler fallback. Verified in live OOBE UI dual-acceptance flow. |
| webview2 | Enabled | Evergreen standalone x64 installer pinned by SHA-256 and size; terms pinned from Microsoft's own download-page endpoint (see below) |
| edge | Enabled | Enterprise x64 MSI pinned by SHA-256 and size, installed machine-wide by msiexec as session 0's account; needs webview2 (see below) |
| copilot | Enabled | Enterprise x64 MSI pinned by SHA-256 and size, installed the same way; needs webview2 (see below) |
| xact | Disabled pending matched-package/terms integration | First-party DirectX package acquired, but differs from historical pinned hash; outer terms are extractable. Need full applicable terms chain, bounded reviewed extraction and architecture-specific functional/audio proof |
| physx | Disabled pending modern recipe/testing | 9.23.1019 package downloaded/hashed; old Winetricks bug cutoff is stale. Need matched terms extraction, reviewed recipe change and actual CPU/runtime/application compatibility tests; GPU acceleration is not implied |

No component is permanently rejected merely because a standalone vendor DOCX
URL was not found. Current production acquisition supports the pinned VC DOCX;
that is an implementation limit to extend with tests, not a vendor prohibition.
No additional catalog entry was switched on just to satisfy a table.

## Core Fonts — available, original terms can be inspected

The immutable installed Winetricks snapshot is still 20250102, SHA256
9df4af038f04c753f3b0b02817cf60a51d8eac32357d070c45dfd139b93bd3f5.
Its load_corefonts dependency closure includes Arial Black's arialb32.exe as
well as the other ten packages. The inherited catalog omitted this package and
had incorrect hashes for all ten listed files. Both defects are repaired.
The validator now compares the exact dependency download manifest; merely
finding the expected filename/hash somewhere else in the script cannot pass.

All eleven packages were acquired from the SourceForge corefonts final mirror
and matched the corrected pinned-recipe identities. Offline inventory found
30 SFNT font files and a LICENSE resource, English language 1033, in EVERY
package. Full resource text is retained privately, never committed. The original
resource bytes are CP1252, not UTF-8; a renderer must preserve bullets, paragraph
structure and full clauses rather than use replacement decoding/truncation.

Reproduction after acquiring the packages with human permission:

```
python3 -B scripts/tests/inspect-component-packages.py --cache PRIVATE_CACHE
```

The committed evidence contains package/font/resource hashes, not their bytes
or legal text. Example andale32.exe SHA256:
0524fe42951adc3a7eb870e32f0920313c71f170c859b5f770d82b4ee111e970.
Its embedded LICENSE is 4920 bytes, SHA256
e025f4a7933977c1d98665616fdded6130b7a130f8eca18d72d18d8b71fd8d57.
The other packages have their own resource identities; do not assume byte equality.

The original [Core Fonts EULA](https://corefonts.sourceforge.net/eula.htm)
provides installation/use and conditional reproduction/distribution rights;
Microsoft retiring its download site is not itself revocation. Do not infer
unrestricted repackaging, modified fonts or commercial bundling rights.
[Debian's installer](https://packages.debian.org/source/sid/msttcorefonts) is
direct prior art for keeping proprietary font payloads out of the distribution.
Core Fonts licensing does not establish Tahoma rights.

Core fonts integration is completed and verified: all eleven packages stage
from the review cache into the winetricks cache under mode 0600, winetricks
executes offline under `/usr/bin/unshare --net`, pre-existing symlinks in
`C:\windows\Fonts` are removed beforehand to avoid overwriting host fonts in
`/usr/share/fonts`, and all 30 installed TrueType fonts are verified against the
evidence inventory before success receipts are emitted. The setup-machine-root
script preserves the genuine installed font files against symlink overwrites, and
font registration in the Windows NT Fonts registry is confirmed.

## Tahoma — specific licensing uncertainty, not absent files

The [Winetricks Tahoma recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks)
uses historical IELPKTH.CAB from SourceForge. The acquired CAB matched its
SHA256 c1be3fb8f0042570be76ec6daa03a99142c88367c1bc810240b85827c715961a.
Listing it found tahoma.ttf/tahomabd.ttf plus language-pack support files, not a
standalone license document. The inspected langinst.exe resource listing did not
identify LICENSE/EULA/RTF. This does not prove no governing agreement exists;
it means we have not established that agreement or the intended-use permission.
[Microsoft's Tahoma page](https://learn.microsoft.com/en-us/typography/font-list/tahoma)
does not offer it as an independent free font download. Do not use Windows-owned
font files or the Core Fonts EULA as substitutes for package-specific evidence.
Keep Wine's builtin/open-font fallback working in the meantime.

## WebView2 Runtime — Enabled, installer run directly

Programs that embed WebView2 (Copilot's "Continue with Microsoft" popup, Office sign-in) look for the runtime
registered under `HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`.
Windows ships it; Wine has none, so without it the popup fails in the WebView2 loader's runtime discovery.

- **Payload:** `MicrosoftEdgeWebView2RuntimeInstallerX64.exe` (215,116,512 bytes, SHA256
  `d3cbe8fedb104282d745fff106993efead51ae94f5595a4e75804446a974f9c3`, runtime 157.0.4341.60) from
  `msedge.sf.dl.delivery.mp.microsoft.com`, the per-file address Microsoft's `go.microsoft.com/fwlink/p/?LinkId=2124701`
  redirects to. The fwlink always serves the latest version, so it is not pinned. When Microsoft retires this file,
  update `payloads`, `release` and the hash together; a changed hash fails closed before anything runs.
- **Terms:** the download page's licence dialog loads `https://developer.microsoft.com/microsoft-edge/api/eula/webview2?locale=en-us`
  (JSON, `evergreenHtml`). `extract_webview2_terms` renders it as text (English and French as published) and refuses
  the Fixed Version agreement. The JSON hash and the text hash are pinned; nothing of the text is stored in the repository.
- **Install:** not a winetricks verb. Downloaded only after the terms digest is accepted, verified, then
  `wine <installer> /silent /install` under `unshare --net` in the machine prefix (about 50 s). The installer leaves
  its update service running, so `wineserver -k` follows a successful exit (otherwise `wineserver -w` never returns).
- **Updater:** the installer registers two Microsoft Edge Update scheduled-task files
  (`system32\tasks\MicrosoftEdgeUpdateTaskMachineCore/UA`) and COM classes, but no Windows service and no Run key.
  Wine does not run scheduled tasks, so nothing starts at boot; only the process the installer itself starts lingers.
  To keep the runtime on the verified version even if something launches the updater, install also sets
  `HKLM\SOFTWARE\Policies\Microsoft\EdgeUpdate` `UpdateDefault=0`, `Update{F3017226-…}=0` and
  `AutoUpdateCheckPeriodMinutes=0` (Wine shares this key between the 32- and 64-bit views), and verification checks them.
- **Verification:** `msedgewebview2.exe` and `msedge.dll` in `Program Files (x86)\Microsoft\EdgeWebView\Application\<version>`
  and `pv` in the registry key above; the receipt `webview2.json` records the terms digest.
- **Tested:** fresh VM overlay, `--prepare-terms`, `--install`, `is_installed()` true, 130 s including the download.

## Microsoft Edge and Microsoft Copilot — Enabled, enterprise MSIs, WebView2 first

Both are Microsoft Edge Update ("Omaha") apps that Microsoft ships as enterprise MSIs. Their MSI's `DoInstall` custom
action (deferred, no impersonation) starts the updater, which installs the app and then hands over to a machine
updater. On Windows that whole chain runs as `NT AUTHORITY\SYSTEM` (measured, `docs/KNOWN_BUGS.md` B104), so the
components install them with msiexec as session 0's account, which is SYSTEM to the machine prefix, the way
`webview2` runs its installer. A user-run MSI of either still fails (B104: the chain then runs as the elevated user).

- **Payloads:** `MicrosoftEdgeEnterpriseX64.msi` (Edge 157.0.4341.60, 215,552,000 bytes, SHA256
  `b86b238035c47673e1c70589a28f2e0961e8cabdd3ff25edbb1e52239d7ecd76`) and `CopilotStableEnterpriseX64.msi`
  (Copilot 158.0.4370.33, 425,648,128 bytes, SHA256 `565b5cb202c22dee664193a77300d09063c9a754640f2d27f4ae43fb71eff4db`),
  both per-file addresses on `msedge.sf.dl.delivery.mp.microsoft.com` (Edge's business download page and Copilot's
  `go.microsoft.com/fwlink/?linkid=2370805` lead there; neither link is pinned, they follow the newest release).
- **Prerequisite:** `requires: ['webview2']`. `install()` refuses Edge or Copilot while WebView2 is not installed;
  the Windows Components frontend adds WebView2 ahead of them when it is missing, and WebView2's terms are accepted
  on their own. The Copilot app embeds WebView2 for its pages and its Microsoft account sign-in. With WebView2 in
  place the updater already has an app when the MSI registers its own, which is also what makes a user-run Edge MSI
  work around B104.
- **Terms:** web pages, not documents. Edge: `https://explore.microsoft.com/en-us/edge/terms` (Microsoft Software
  License Terms, Microsoft Edge, as the MSI's own `edge://terms` shows). Copilot: the Microsoft Copilot Terms of Use,
  `https://www.microsoft.com/en-us/microsoft-copilot/for-individuals/termsofuse` (its supplemental terms; they refer to
  the Microsoft Services Agreement). The pages' markup changes with every site build, so `extract_html_terms` takes
  the text of the page's `<main>` element (no scripts or styles) from the agreement's heading (`terms_start`) on, and
  that text's hash is what is pinned (`terms_sha256` and `terms_text_sha256` are the same). The pages are fetched with
  the user agent `WineExplorer-components/1.0`: Microsoft's servers refuse Wget's. Nothing of the text is stored here.
- **Install:** after the terms digest is accepted, the MSI is downloaded (deadline 30 minutes), verified, staged for
  session 0's account and run as `msiexec /i Z:\…\<msi> /qn /norestart` (msiexec reads a Unix path as an option, so
  the staged path goes through the machine prefix's `Z:` drive, which must map `/`). As with WebView2, the update
  policy is set and the machine prefix's Wine stopped afterwards (the updater lingers).
- **Disk space:** checked before anything is downloaded (`disk_space`: Edge 3 GiB, Copilot 5 GiB free on the machine
  root). The MSI exists several times over during the install (cache, staged copy, msi's working copy, the updater's
  `Install` and `Download` copies) and the setup the updater runs unpacks through file mappings; Wine does not
  implement its preallocation (`failed to pre-allocate the file: Call not implemented`), so on a full disk the setup
  died with an unhandled page fault halfway (setup exit 0xC0000354 after the auto-debugger, msiexec 1627, seen as exit
  91). Measured for Copilot: 2.9 GiB more in use at the peak than before. Before the check an 18 GB test image failed so.
- **Updater policy:** `HKLM\SOFTWARE\Policies\Microsoft\EdgeUpdate` `UpdateDefault=0`, `AutoUpdateCheckPeriodMinutes=0`
  and `Update{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}=0` (Edge) or `Update{C50565E9-CCCF-44B4-BA15-5AC5C6569197}=0`
  (Copilot). Observed: during the Copilot install its newer updater (1.3.319.41, run by session 0's services, which
  have the network) still downloaded 158.0.4370.33 differential updates for the installed WebView2 and Edge, with the
  policies set (Edge Update honours update policies only on domain-joined or managed machines). Their setup failed
  (`Failed to set zero data for hollow archive: Request not supported`, `InstallerError 506`), so both stayed on the
  pinned 157.0.4341.60 and verified; this relies on a Wine gap (FSCTL_SET_ZERO_DATA, `docs/KNOWN_BUGS.md` B106), not
  on the policy.
- **Tested (2026-11-25):** clean overlay of the efr2 image (Wine 0ec8719014e, class 60) with a 27 GB root, as root
  through `prepare_terms`, `license_text` and `install()`: `webview2` 77 s, `edge` 75 s, `copilot` 148 s (including
  the downloads), each `is_installed()` true, all three receipts valid at the end. An earlier run refused Edge before
  WebView2 with the prerequisite message.
  Program start and sign-in after a components install were not re-checked in this run.
- **Verification:** Edge: `Edge\Application\msedge.exe` and `Edge\Application\<version>\msedge.dll`; Copilot:
  `Copilot\Application\copilotapp.exe` and `Copilot\Application\<version>\Installer\setup.exe` (under
  `Program Files (x86)\Microsoft`), `pv` under `EdgeUpdate\Clients\{app}` and the policy values.

## Compiler 47 — Enabled with Microsoft SDK cabinet distribution

DirectX Shader Compiler `d3dcompiler_47` is enabled and integrated using first-party
Microsoft Windows 8.1 Standalone SDK endpoints on `download.microsoft.com`:
- `sdksetup.exe` (1,281,424 bytes, SHA256: `5107822a5a99bcdef4c7b7c7eea218425692f0185750b0d4fafd441034a7486b`)
- `2630bae9681db6a9f6722366f47d055c.cab` (3,467,294 bytes, SHA256: `d0440eb81c532dc23639c0c63f2fcde9deddb23bb4cce01c19ac6b96cc3e269d`)
- `61d57a7a82309cd161a854a6f4619e52.cab` (4,174,380 bytes, SHA256: `f736e161547095bb8d98c636b85fdfeb4070fefeee3c3745db3ce88f6eb1d9de`)

Terms extraction statically unpacks the Burn engine payload manifest from `sdksetup.exe`
to extract the official license RTF resource `u1` (64,354 bytes). The terms parser strips RTF
control tags, yielding an 18,295-byte plain-text agreement (SHA256:
`5b575dc86b5e04198a8915dabe81b987daae47be007ad42897bc57a5d92a5f43`) encompassing the
complete Windows Software Development Kit agreement.

Execution replaces the old unverified Mozilla GitHub recipe with an approved, offline
recipe:
1. `sdksetup.exe` and both cabinets are verified in review cache and staged to Winetricks cache mode 0600.
2. Binary cabinet members are extracted offline without network access (`/usr/bin/unshare --net`):
   - 32-bit PE member `fila319f706acfa16d6707473ebf29bdc7f` -> staged as `d3dcompiler_47_32.dll` (3,466,856 bytes, SHA256: `efbdbbcd0d954f8fdc53467de5d89ad525e4e4a9cfff8a15d07c6fdb350c407f`)
   - 64-bit PE member `fil3585cb2ea5db13cc0838f8d06b5c9679` -> staged as `d3dcompiler_47.dll` (4,173,928 bytes, SHA256: `e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4`)
3. The recipe copies the 32-bit PE to `C:\windows\syswow64\d3dcompiler_47.dll` and the 64-bit PE to `C:\windows\system32\d3dcompiler_47.dll`.
4. Artifact hashes are strictly verified fail-closed after installation.
5. `protect_builtins` purges installer registry overrides (`HKCU\Software\Wine\DllOverrides`) so Wine's builtin DirectX compiler remains default globally while native compiler DLLs are placed in the system directories for applications requesting native overrides.
6. Verified live end-to-end in OOBE wizard flow alongside `corefonts` (receipt `d3dcompiler_47.json`).

## XACT — terms are extractable without executing the package

The [official DirectX page](https://www.microsoft.com/en-us/download/details.aspx?id=8109)
still describes the legacy runtime containing XACT. The documented first-party
8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14 download succeeded. Observed SHA256
053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b
does NOT match our pinned mirror's 8746ee1a... package. Never waive that difference.
Its outer LICENSE resource is 10032 bytes, SHA256
3794344074cfd10bfee246a9914ec4d801242fc82c172aa021551b8c0822f828.
It identifies DirectX SDK terms, refers to included programs' own terms and
contains distribution/platform restrictions. It is not automatically sufficient
to present it as the sole XACT runtime agreement. Further matched-package analysis
is needed; neither user consent nor Winetricks removes these applicability questions.

Winetricks treats xact as 32-bit and has a separate xact_x64 route with a warning.
Do not claim a combined-WoW64 runtime pass from running the x86 recipe alone.
No SDK/DXSETUP executable was run during this package/terms inspection.

## PhysX — stale Wine regression gate, plus unverified new integration

The [current recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks)
records bug 56606 as broken from Wine 9.5 and fixed starting 9.10. The pinned
2025 recipe lacks the fixed-version argument, so it wrongly presents a permanent
block on newer Wine. Do not simply pass --force and certify the old integration.

[NVIDIA's 9.23.1019 package](https://www.nvidia.com/en-us/drivers/physx/physx-9-23-1019-driver/)
was acquired directly and matched current upstream's SHA256:
9b42b84e881769d681e09f62a1b51532616b2e6a2d5d99d0ccae6eb5fbbc208c.
This is newer than the production metadata's 9.21.0713. NVIDIA describes security
updates; package/terms/recipe changes must be reviewed together. No install/game
pass is claimed. cabextract cannot parse this EXE; absence of CAB output is not
absence of a license. CPU runtime compatibility and GPU PhysX support are
different questions, especially on Intel/AMD/older low-end target machines.

## Consent and next implementation boundary

The human explicitly authorized needed downloads/installations in disposable labs
and confirmed prior Winetricks terms acceptance. This is not redistribution
permission, permission to put vendor art/fonts/DLLs/documents in Git/ISO, or a
shipping automatic-accept switch. Downloaded vendor files and complete terms stay
only under the private lab cache.

Where authoritative standalone terms can be obtained and matched to exact
packages, preserve review-before-payload-download. For embedded terms, the
alternative product flow is an explicit Download for review action showing size,
then extraction without PE execution, then full terms display and separate
acceptance before installation. The human explicitly approved that alternative
on 2026-10-19. Implement it under OOBE_EMBEDDED_TERMS_WORKORDER.md; production
code has not yet changed. Extend formats with bounded, hash-bound extraction
and stale-response/cache tests before enabling. Applicable package rights remain
a separate question; approval is not redistribution permission.
