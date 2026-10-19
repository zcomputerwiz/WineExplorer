<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent-package terms investigation — 2026-10-19

Branch: codex/oobe-final-correctness. Follow-up to the user's suggestion to show
the governing SDK/IE agreement, rather than demand standalone DLL/font terms.

## Conclusion

That is the correct provenance approach, subject to component-specific terms.
We have now located an exact SDK agreement chain for the two D3DCompiler cabinets,
and an exact DirectX END USER RUNTIME agreement inside the XACT source package.
Tahoma's recipe identifies IE 5.5 SP2 as its historical source, but the matching
agreement and applicable standalone-use rights are not yet established.

These are engineering findings, not a legal clearance opinion. Presenting a
license and obtaining consent cannot expand its grant. We must distinguish:
(a) a user's own download/install/use; (b) our hosting, bundling or redistribution;
(c) whether extracting selected components is covered. No production entry was
enabled, no installation was attempted, and no vendor bytes or full agreements
were committed. The old requirement for a standalone first-party terms URL
must not be used as a categorical rejection.

| Component | Agreement provenance now established | Remaining question |
| --- | --- | --- |
| d3dcompiler_47 | SDK bootstrap's own license.rtf, linked to both exact compiler CABs by its manifest | Windows/development scope, runtime use under Wine, historical redist-list match, reviewed extraction and functional proof |
| xact | Exact June 2010 package -> dsetup32.dll -> English end-user-runtime agreement | Complete outer/inner terms presentation and applicability, updated recipe identities, selected extraction and x86/x64 runtime proof |
| tahoma | Pinned and current Winetricks identify the historical IE 5.5 SP2 language-pack URL | Authentic matching IE 5.5 SP2 agreement/supplements and applicability to font-only Wine installation |

## D3DCompiler 47: real SDK terms, not a generic newer agreement

Acquired without executing the installer:
[Microsoft's Windows 8.1 SDK bootstrap](https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/sdksetup.exe).
Observed SHA256:
5107822a5a99bcdef4c7b7c7eea218425692f0185750b0d4fafd441034a7486b.

cabextract can statically unpack its embedded Burn container. Member 0 is the
Burn manifest. It explicitly names license.rtf as payload u1, with its size and
legacy SHA1. UserExperienceManifest.xml (u12) selects license.rtf and EulaVersion
6.3.0.0. The RTF is 64,354 bytes; SHA256:
7606715231cab510135bb271836677797189c64cfacc59b8b444e427fecace78.

The same manifest names BOTH previously acquired compiler cabinets, and their
sizes and SHA1 hashes match. Our independent SHA256 pins also match. This is a
concrete terms-to-package relationship, not a license copied from an unrelated
SDK release. Manifest hashes establish consistency with that downloaded bootstrap,
not independent publisher-signature verification; no Authenticode validation
claim is made.

Important: this RTF contains FOUR separate Windows Kits agreements. Its first
agreement is ADK, NOT the SDK agreement. The applicable SDK section is titled
Microsoft Windows Software Development Kit (SDK) for Windows 8.1. A production
renderer must preserve the introductory selection instructions and the complete
applicable agreement, without accidentally displaying only the ADK section.

Reading the SDK section identifies these material issues:
- Section 1(a) describes development/testing of programs running on Windows,
  and requires complete copies including notices for the stated installations.
- Section 2(a) permits specified Distributable Code under additional conditions.
- Section 2(a)(ii) includes conditions concerning components in setup programs.
- Section 2(a)(iii) restricts distribution targeting non-Windows platforms.
- Utilities/build-server rights are separate and are not a blanket runtime grant.

These do NOT establish that all private runtime use under Wine is prohibited.
Equally, our optional downloader must not be declared cleared simply because
we ship no DLLs. Whether the proposed workflow and font/DLL-only extraction
are covered must be resolved on their own facts. Do not call Wine Windows for
license purposes merely because its API/version reporting imitates Windows.

The agreement's redist link is
[LinkId=294840](https://go.microsoft.com/fwlink/?LinkId=294840).
On this check it redirects to a first-party file titled Windows 10 SDK WDK Redist
List.rtf, not the historical Windows 8.1 list. Do not silently bind that moving
target as proof of an 8.1 list. Another inspected reference, LinkID=317349,
redirects to Bing. Historical third-party copies are discovery leads, not our
verified first-party agreement artifact.

Also downloaded the manifest-identified 294,912-byte Windows Software Development
Kit Redistributables-x86_en-us.msi. Its SHA1 matches the bootstrap manifest;
SHA256 is 1cca04682be201aa244f9e57988f2a0eebfe9a7dfbaf6f89096ad9d8f005fb93.
Static msiinfo inspection of its complete ten-row File table found nested DirectX
remote MSI/CAB packages, not a license/redist-list document. This rules out that
specific table as a direct source; it does not prove absence from the whole SDK
or the nested payloads, which were not fetched in this investigation.

[The original DirectX SDK maintainer's deployment guidance](https://walbourn.github.io/hlsl-fxc-and-d3dcompile/)
documents Windows 8.1 SDK application-local compiler redistribution. This supports
the technical package choice, not blanket Linux/Wine licensing clearance.

Next evidence: locate the original included redist/license artifacts in the
SDK's redistributables MSI/CAB payloads, match compiler member identities, and
review the intended end-user use and selected-component extraction. If applicable
rights remain ambiguous, seek qualified licensing review or use Wine's builtin;
do not substitute an unrelated agreement.

## XACT: the actual end-user-runtime agreement was found

The [official DirectX June 2010 download page](https://www.microsoft.com/en-us/download/details.aspx?id=8109)
explicitly identifies XACT as one of the package's legacy runtime components.
Current page version: 9.29.1974.1, published 2024-07-15. The already acquired
package SHA256 is:
053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b.

Its outer LICENSE resource is the previously inspected SDK agreement. Going one
level deeper, cabextract extracts dsetup32.dll without executing a PE. Its SHA256:
fb0e534f9b0926e518f1c2980640dfd29f14217cdfa37cf3a0c13349127ed9a8.

That DLL contains DATA / SETUP_AGREEMENT_ENG / language 1033. wrestool can extract
this resource directly despite its misleading resource-list output labeling
multiple names as DATA. The resource contains the separate Microsoft DirectX
End User Runtime agreement, 4,849 bytes, SHA256:
55eaf19bbbbcc80e36e22e3bac1842413376874505b4927abd7eb0c203987771.

It decodes strictly as CP1252; no embedded NUL exists. Its installation/use grant
is not phrased as the SDK development-only grant. Its restrictions on publication,
transfer and other conduct still matter. This is a much better match for runtime
users than presenting ONLY the outer SDK agreement. It does not establish
permission for us to republish the payloads or vendor agreement text.

Recommended next implementation:
1. Bind this runtime agreement AND the applicable outer-package agreement into
   an ordered terms set; do not silently discard the outer one.
2. Download for explicit review, or acquire the terms carrier separately if an
   authoritative exact carrier is available; execute no PE to get terms.
3. Show complete agreements with correct decoding, then obtain separate consent
   to install. Bind consent to the exact package/member/terms identities.
4. Validate selected XACT cabinet/member identities and both architecture paths.
   Decide whether selected extraction versus DXSETUP execution changes the scope
   analysis. Static extraction is not a runtime installation pass.
5. Keep shared-prefix protections, decline/offline behavior, user override
   precedence and normal-user functional/audio tests as mandatory gates.

The full terms chain and user experience still need implementation/review.
This finding removes the claim that runtime terms cannot be acquired before
execution; it does not by itself enable the catalog verb.

## Tahoma: known parent release, still no verified agreement artifact

Both the immutable installed Winetricks 20250102 recipe and
[current upstream recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks)
identify the former Microsoft source as:
https://download.microsoft.com/download/ie55sp2/Install/5.5_SP2/WIN98Me/EN-US/IELPKTH.CAB

The SourceForge mirror matches our existing c1be3fb8... package pin.
This is evidence for targeting IE 5.5 SP2 terms, NOT IE 6, IE 11, Core Fonts
or a modern Windows font agreement. Package TH.INF identifies IE Thai language
support with Microsoft copyright 2000 and contains a font installation section;
langinst.exe reports version 1.0.0.1, which does not independently identify
the whole IE release. No embedded EULA was found in the inspected package.

The historical first-party CAB and sibling IE5SETUP.EXE endpoints both return
HTTP 404 today. Generic searches and older IE5 media listings do not establish
the matching SP2 agreement, and similarly named older CABs have different sizes.
No random historical setup executable was downloaded or run as a substitute.

[Microsoft's permissions page](https://www.microsoft.com/legal/intellectualproperty/copyright/permissions)
points IE component redistribution questions to IEAK. It is not a font-only
Wine use grant. Other Microsoft product agreements mentioning bundled IE are
also not interchangeable with this package's own agreement.

Next evidence must be an authentic matching IE 5.5 SP2 setup/media agreement,
plus any language-pack supplements. Inspect its scope, possible Windows-license
prerequisites and component separation restrictions. If a user supplies licensed
original media for inspection, keep its files and agreement text private.
Until the chain is established, retain Wine's open fallback. This is an unresolved
specific question, not proof of a universal legal ban on Winetricks use.

## Reproduction, failures and retained state

Run the committed static research probe against the private authorized cache:

    python3 -B scripts/tests/inspect-parent-package-terms.py --cache PRIVATE_CACHE

It verified SDK manifest/licensing/cabinet hashes and DirectX carrier/resource
hashes. Committed metadata is in evidence/oobe-parent-terms-20261019.json.
It prints no full agreement text and executes no vendor code. It is not a
hardened production parser/renderer or a legal decision engine.

Local private inspection only:
- Installed Debian unrtf for RTF reading; installation log retained in the owned
  lab evidence directory. Its text export visibly loses some bullet/typographic
  fidelity: do NOT reuse that export as a production consent rendering.
- A command stopped at sh: file: not found after SDK extraction. Continued with
  od, strings, XML metadata, and the newly installed RTF reader.
- sed's first search for the SDK heading matched nothing because the actual
  heading contains (SDK). Searching the actual heading found the correct section.
- The web tool returned Internal Error for LinkIDs 317349 and 294840. curl -IL
  resolved them: Bing and the newer Windows 10 redist list respectively. HTTP 200
  alone is not a successful agreement-version match.
- Both historical IE endpoints returned curl (22), HTTP 404; no fallback identity
  was guessed.
- cabextract reports possible extra trailing bytes on signed SDK/IE cabinets.
  No hash check was waived and the input files were not rewritten.
- wrestool's list output obscured named DATA resources in dsetup32.dll. objdump's
  resource tree exposed SETUP_AGREEMENT_ENG; explicit extraction by name succeeded.
- Initial precommit diff check rejected a new blank line at EOF in this report
  and the research probe (lines 199 and 112). Removed those trailing blank lines
  with apply_patch; no commit or push occurred on that failed attempt.
- No VM was started, no prefix changed, no production entries enabled. The owned
  clone and transfer server remain stopped. SDK bootstrap and extracted terms
  remain only in /var/tmp/wex-oobe-final-20261018/downloads (private parent).
  Other workers' branches, main checkout, Wine and compositor pins are untouched.
