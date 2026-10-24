<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Implement the demonstrated userenv SID derivation gap

Parent independently reviewed R2 b759008a:46 sealed files/16 archive members,
actual native journal/cleanup/shutdown. Browser WinPID228/TID564 at44.106 logs
missing USERENV!DeriveAppContainerSidFromAppContainerName, immediately followed
by c06d007f in that SAME thread/time, then matching held-handle child exit.
Private `chromium-module-r2-parent-review-20261024.json` records review. This
justifies this API fix; other two static missing-export candidates remain TODOs.

## Contract and independent evidence

Microsoft documents HRESULT S_OK/E_INVALIDARG, profile-name input, PSID* output
and caller FreeSid ownership:
https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deriveappcontainersidfromappcontainername
The package authority/RID constants are public:
https://learn.microsoft.com/en-us/windows/win32/secauthz/app-container-sid-constants
This function computes an identity; implementing it is NOT token enforcement,
profile creation, sandbox hardening or full Chromium acceptance.

Parent's original, read-only Windows26300 public-API oracle created NO profile,
token or persistent security state. Private D:lab `appcontainer-sid-oracle-20261024`
and `appcontainer-sid-boundary-oracle-20261024` retain results; original PS/C#
P/Invoke scripts are sibling files. Observed:

- NULL name/output, empty name and65-or-more UTF16 units return E_INVALIDARG;
  existing output pointer remains unchanged.1,63,64-unit names succeed.
- Name `test` and `TEST` both return
  S-1-15-2-1980125950-1037672881-421768107-1949737198-2922275827-507320043-1582245000.
- `WineExplorer.MixedCase_01` and lowercase equivalent return
  S-1-15-2-3520518378-1338609999-3243366982-3993260403-3393427587-956535139-1620658767.
- `WäneΣ` returns
  S-1-15-2-1925959860-2464893839-2425465101-3504107519-2374159773-1197180585-2285453026.
- Punctuation `a/b:*? ` succeeds; do not impose filesystem/profile-name restrictions.

All six successful oracle cases match SHA256 of per-character lowercase UTF16LE
WITHOUT terminator, first7 little-endian DWORDs, authority15/baseRID2 and8 total
subauthorities. This is an experimentally verified derivation for those inputs,
not copied proprietary implementation. Contextual Unicode whole-string lower
does NOT match the sigma vector; use Wine's invariant simple Unicode downcase
(e.g. RtlDowncaseUnicodeChar), not Python-style contextual sigma conversion.

## Luna implementation and isolated unit proof

1. Create ONE owned sparse Wine source worktree from4315be83 in the C: graphics
   checkout's ignored build area, branch codex/g4-userenv-appcontainer. Only
   dlls/userenv and include/userenv.h need materialization; avoid a large C:
   checkout. This is a Wine-submodule worktree, so use that repository's Git
   rather than the app's parent-repository worktree tool. Preserve existing
   main/unused Wine checkouts and root M wine; NEVER stage root wine gitlink.
2. Implement real DeriveAppContainerSidFromAppContainerName, export/spec/header
   and Wine-native conformance tests under existing LGPL. Validate NULL/empty/
   >64 inputs without writing output on failure. Allocate a valid caller-FreeSid
   SID, deterministic case handling/different-name separation and correct layout.
   Prefer existing bcrypt SHA256 APIs plus simple invariant NLS downcase; no
   new crypto implementation, registry profile, global mappings or fixed fake SID.
   Close/free handles/buffers on every operational failure and publish output
   only after success. Add only needed imports. No CreateAppContainerProfile,
   token/server/permissions work or unrelated user32/power API changes.
3. Meaningful tests: actual export/prototype/ABI, exact oracle vectors including
   sigma, repeat/case equality and different names, punctuation,64/65 boundaries,
   NULL arguments/empty preserving sentinel, IsValidSid/count/authority/baseRID,
   FreeSid-compatible memory and failure cleanup. Use the public host oracle as
   an independent expected-result reference; do not merely hash with the same
   implementation twice. Keep existing userenv regression assertions.
4. Build ONLY isolated userenv modules/tests for x64 and i686, private outputs/
   prefixes/evidence on D: (or existing D:-backed Debian native disk). Existing
   owned build tools/libraries/headers may be READ references; source/build at
   /var/tmp/wex-g3-repair-20261018/clean-source is dirty and MUST NOT be edited or
   rebuilt. Replay the existing Makefile's module compile/link commands into a
   NEW output directory using sparse changed source/header and current read-only
   dependencies. No full Wine rebuild, loaded binary replacement or other workers.
   Test under isolated owned prefixes; document exact DLL-loading route and actual
   x64/i686 execution, skips/failures. A native-DLL override scoped to a private
   API unit prefix is an implementation test, not sandbox or capability spoofing.
   Never change browser security flags or shared VM prefix for these unit tests.
5. Commit AND push Wine branch to project fork and parent report/test sources
   to graphics branch; no root gitlink update. Report build/unit results, actual
   source+binary observations and limitations. STOP parent review before any
   VM deployment/browser rerun. New production DLL is not installed anywhere yet.

After review, use a separately planned normal-module deployment and one fresh
browser control on existing D: R4; preserve old DLL/evidence. No repeated native
calibration/new disk layers. Historical hashes remain diagnostic, concrete
ownership/overwrite/budget protections remain.95% five-hour/99% weekly checkpoints,
no reset credits/master merge/vendor publication/other-agent changes.
