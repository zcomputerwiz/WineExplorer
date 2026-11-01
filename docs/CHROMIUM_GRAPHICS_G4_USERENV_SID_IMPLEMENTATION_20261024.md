<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# userenv SID derivation — isolated implementation and unit proof

Implemented only the demonstrated export gap under `USERENV_SID_PLAN_20261024`.
Wine branch `codex/g4-userenv-appcontainer` pushed to the project fork:
[e096ae4373d778fde4a876f94798711f369e7013](https://github.com/zcomputerwiz/wine/commit/e096ae4373d778fde4a876f94798711f369e7013).
Sparse source worktree is the C: graphics checkout's ignored
`build/wine-userenv-appcontainer`, based on4315be83; only userenv and userenv.h
materialized. No root wine gitlink, main checkout or other worker was staged.

Changed Wine files: `dlls/userenv/{Makefile.in,userenv_main.c,userenv.spec,
tests/userenv.c}` and `include/userenv.h`. New public prototype/stdcall export;
only bcrypt/ntdll imports added. The function rejects NULL/empty/>64 UTF16-unit
names and NULL output without changing an existing output pointer. It uses
RtlDowncaseUnicodeChar per unit, BCrypt SHA256 over lowercase UTF16 without its
terminator, first7 little-endian DWORDs, authority15/baseRID2/count8, and actual
AllocateAndInitializeSid memory compatible with caller FreeSid. Output is
published only after success; crypto handles close on operational failure.
No fixed fake SID, profile creation, token/permission work, other missing API,
browser flag, sandbox bypass or production server change.

Contract references: [Microsoft API/FreeSid ownership](https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deriveappcontainersidfromappcontainername),
[SID constants](https://learn.microsoft.com/en-us/windows/win32/secauthz/app-container-sid-constants).
The derivation is supported by the parent's independent Windows26300 oracle,
not copied proprietary implementation. Its two original results/scripts remain
private; input/output vectors were not regenerated from this implementation.
Tests cover exact test/TEST, mixed/lowercase, WäneΣ and punctuation SID vectors,
repeat equality/different names, layout/validity, FreeSid,1/63/64/65 boundaries
and invalid-output sentinel preservation; existing regression assertions remain.

Isolated build/evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-sid-r1-20261024.build/`.
Manual replay of existing Makefile compile/link recipes with new output paths:
12 successful x64/i686 module/import-library/test operations; `-Wall -Werror`,
empty current compile/link stderr. Only changed source/header came from sparse
worktree; existing dirty `/var/tmp/wex-g3-repair-20261018/clean-source` supplied
read-only tools/includes/libraries. No make/rebuild or edit there. Unit execution
additionally bind-mounted that whole old source/build tree read-only in a private
mount namespace. Initial missing-braces warning was fixed and retained separately.

| Builtin module artifact | Machine | SHA256 |
|---|---|---|
| x86_64-windows/userenv.dll |0x8664|89e93bd0774200acbe97ffda7745c192396bbe7c7710abcc0d2fdce5ea3981f2|
| i386-windows/userenv.dll |0x014c|3452a717e91f9fd191b3fd9b1ad8404be27c771ed03e10dc0224bc9f5e25e62b|

Actual unit loading route is explicit: the initial native-only launch of a
builtin-marked DLL returned Linux53 before assertions; retained, not called an
API failure. Separate UNIT-ONLY native-marked links of the SAME module objects
(omitting --wine-builtin), beside the actual test EXEs, were then tested with
`WINEDLLOVERRIDES=userenv=n;winemenubuilder.exe=d` in fresh private prefixes.
No native override was installed in any shared/browser prefix. Loader traces and
GetModuleFileName tests show the exact `native-unit/{x86_64,i386}/userenv.dll`
paths. These unit DLL hashes are6a013f1f…/81680cc1…, distinct from builtin links.
Builtin links compiled/exported correctly but were not directly loaded in units.

Actual results: x64 and i686 each **260 harness tests/assertions,0 failures,
0 skips,1 existing todo** (GetUserProfileDirectory invalid-handle error). Both
unit processes exit0; both prefix init0 and exact-prefix server kill/wait0.
i686 PE actually executes through existing new-WoW64 in a separate win64 prefix,
not a native32 Linux loader. Native-prefix roots `/var/tmp/wex-userenv-sid-r1-*`
are on the existing D:-backed Debian filesystem; all three created prefixes are
retained, independently scanned COMPLETE-empty after cleanup. Ordinary Wineboot
prefix setup is distinct from implementing an AppContainer profile API.

Parent `scripts/test_g4_userenv_sid_cleanup.py` compiles the ACTUAL extracted C
function with host fault callbacks, not a Python duplicate: BCrypt failure
steps1..4, allocation failure, reverse handle cleanup and unchanged output;
**1 unittest PASS,0.245s**, strict host compiler, D: temp only. Its fake crypto/
allocator are error-path unit controls; real SHA/NLS/SID/FreeSid proof comes from
the Wine-native oracle-vector tests above. Exact argv/logs, sources, machines,
identities/hashes, oracle references and limitations are in operations/audit JSON;
87-file seal retains failed and successful records. `git diff --check` passes in
both repositories. No unrelated test suites repeated.

STOP before VM deployment/browser rerun. Neither builtin module is installed in
the guest or the old runtime; no VM/image/layer or browser was run. This proves
the specified identity derivation in isolated unit contexts, not AppContainer
token enforcement, sandbox hardening, Chromium startup or graphics acceptance.
Other static API candidates remain TODOs, not assigned fixes. M wine/D: master/
other agents and all earlier failure evidence remain preserved.
