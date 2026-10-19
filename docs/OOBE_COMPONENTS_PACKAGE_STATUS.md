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
| corefonts | Disabled pending integration | All 11 originals and 30 fonts now verified; embedded terms exist. Need reviewed terms acquisition/binding, verified shared font publication/registration, real font selection and preservation of user fallback settings |
| tahoma | Disabled pending rights/terms verification | Verified historical CAB contains the two fonts, but Core Fonts EULA does not automatically cover this separate IE language pack; exact applicable terms/use rights remain unresolved |
| d3dcompiler_47 | Disabled pending SDK integration | Both Microsoft SDK cabinets downloaded; replace old Mozilla-DLL recipe with reviewed cabinet/member identities, matched SDK terms and actual compile/load checks for both architectures |
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

Remaining proof: complete displayed terms bound to all eleven packages, exact
published font hashes/registration, normal administrator/limited-user visibility,
actual selected faces (not just file existence), correct interaction with the
project's open-font substitutions, and unchanged unrelated user font settings.

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

## Compiler 47 — Microsoft cabinet route exists

Current upstream Winetricks uses the Windows 8.1 SDK's Microsoft-hosted cabinets,
unlike our old pinned verb's Mozilla-hosted DLLs. Both cabinets were acquired from
download.microsoft.com under the documented B0C80BA3-8AD6-4958-810B-6882485230B5
standalonesdk/Installers path. Observed SHA256:

- 2630bae9681db6a9f6722366f47d055c.cab:
  d0440eb81c532dc23639c0c63f2fcde9deddb23bb4cce01c19ac6b96cc3e269d
- 61d57a7a82309cd161a854a6f4619e52.cab:
  f736e161547095bb8d98c636b85fdfeb4070fefeee3c3745db3ce88f6eb1d9de

This establishes an acquisition path, not an approved manifest/terms package.
Need exact member/PE identities, corresponding SDK license/redist conditions and
functional shader compilation tests. Do not substitute newer unrelated SDK terms.
[SDK downloads/archive entry point](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads).
Retain the builtin compiler as default until explicit optional installation is proven.

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
acceptance before installation. A product-policy decision for that alternative
is pending; our current shipping path was not silently relaxed. Extend formats
with bounded, hash-bound extraction and stale-response/cache tests before enabling.
