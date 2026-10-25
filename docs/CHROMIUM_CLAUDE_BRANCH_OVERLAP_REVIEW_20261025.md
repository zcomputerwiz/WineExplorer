<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium / Claude branch ownership and overlap review

Compared fetched WineExplorer ccr-08223ee4-p830bf at
97d534f832ee3ff85ab9591e5cdbf01574b77330 with graphics e405e336; its Wine
pin is4fa6f4ac8e0fd55c60824e30b890cd803b60ac37. Graphics plus accepted SID base
is e096ae4373d778fde4a876f94798711f369e7013 (production guest4315be83 plus
readonly userenv view in R5). All observations are of committed trees, not
Claude's uncommitted work or another worker's pending changes.

## Conclusion

No duplicate current implementation. Keep Antigravity's registration assignment.
Claude's branch still has AppContainerFreeMemory, AppContainerLookupMoniker,
AppContainerRegisterSid and AppContainerUnregisterSid commented out in
kernelbase.spec lines40/42-44. It has no userenv SID Derive export either.
Registration and SID derivation are missing dependencies there, not duplicate
versions of our implementation.

Claude's recent branch work is UAC elevation: genuine filtered-admin tokens,
registry/SCM access enforcement, wexsys service identity, broker authorization,
secure-desktop consent and caller migration. The newest commit adds
WexElevateRun and harness coverage; ELEVATION_CALLERS_WORKORDER still records
shell32/MSI caller migration as pending. Do not assign Chromium workers that
broker/caller task.

The branch also carries earlier token-security implementation (some predates
Claude): NtCreateLowBoxToken, restricted/capability SID state, token queries and
PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES wiring. Parent checked actual
server/token.c, ntdll/unix/security.c and kernelbase/process.c. Reuse and review
that existing code if Chromium next reaches child token creation; do not ask
Antigravity to independently invent a second token implementation. This overlap
review does not accept those security semantics or the historical browser PASS
claims. Current Checkpoint C acceptance remains unproven.

## Merge/integration boundaries

At Wine common base ed5fe967ed471ca524666631ad70dabbed6af076, graphics/SID changes
39 paths; Claude pin changes27 paths. Their current modified-path intersection
is empty. Root common base1fa6323385e9b069fafae60afdb42ee68e8e72bf has overlapping
wine gitlink and four docs: ELEVATION_WORKORDER, STOCK_CHANGES,
TOKEN_SECURITY_REFERENCES and TOKEN_SECURITY_WORKORDER. Root documentation and
Wine gitlink need intentional integration; no code merge/build was performed.

Replacing our Wine gitlink with Claude's would lose graphics and SID work;
replacing Claude's with ours would lose token/elevation work. Merge reviewed
commit histories into a separate integration branch later, and test the combined
build. Empty path intersection is not ABI/runtime compatibility proof. In
particular test registration HKCU writes/locking as a filtered administrator and
standard user under Claude's registry enforcement, with separate prefixes and
unchanged broker behavior. Neither a graphics-only run nor an isolated token
probe proves that combined configuration.

## Ownership

- Claude: token authority/enforcement, wexsys, elevation broker/secure desktop,
  caller migration and its native/installed-system acceptance.
- Antigravity: four real kernelbase registration metadata APIs, persistence,
  allocation/free, cross-process concurrency and x64/i686 conformance evidence,
  on the current isolated assignment based on accepted SID implementation.
- Parent: review and eventual combined integration; actual Chromium/Edge
  startup/graphics/lifecycle tests on owned VirGL guest. R5 remains consumed;
  no new VM/browser test or master merge authorized by this audit.

Tracked files were not reset or switched; only fetched Git objects and this
original report changed. Existing parent M wine mismatch remains untouched.
