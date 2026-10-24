<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native calibration R4: explicitly pinned internal loader

Parent independently verified all54 sealed R3 files and11 archive files. Retained
471-byte rejected cmdline has exactly the approved expected argument tail; only
argv0 changed from the build frontend to its internal `loader/wine`. PID945/
start3606/prefix remained stable. Native execution beyond the loader and full
child status are UNKNOWN. Preserve failed R2/R3 layers, outputs and raw bytes.

Luna implements OFFLINE only, then commits AND pushes for parent review:

1. Add an optional explicit approved loader path to `linux_identity`. Default
   None preserves all generic browser rejection/acceptance forms. Only the native
   collector supplies the exact fixed build `/loader/wine` path. Accept precisely
   `[approved_loader] + expected_argv[1:]` with existing trailing-NUL normalization;
   retain PID/start/prefix, bounds, deadline, rejected-byte capture and cleanup.
   No inferred observed path, basename rule, arbitrary argv tail or new flat form.
2. Native guard separately hashes that exact canonical internal loader BEFORE
   Popen. Expected SHA256 is
   `8f3ff89a331ee1305e7db7f18b69425572b96aeff34b274fe96f90db84fa835d`
   (24,336-byte ELF64 x86_64). Host reference matches17 original build pins;
   guest loader hash has NOT yet been observed. Preserve19 environment pins and
   original2 PE pins; label this an additional loader pin. The frontend resolves
   to `tools/wine/wine`; do not call it a shell script. No runtime/API fix or
   security attestation claim follows from argv matching and prelaunch hash.
3. Meaningful tests: generic default still rejects internal loader; explicit
   exact loader plus whole exact tail passes; wrong build/path/flags/nonce/child/
   prefix/start/hash fails. Test pin failure stops before launch and original
   rejection observation survives. Keep original source-approval negatives.
4. Prepare immutable R4 inventory in fresh D: directory. Original2 compiled PEs
   and fixed GUEST R1 paths stay unchanged. Proposed source approval explicitly
   pins reviewed new reader/collector. Same16 TAR members, updated pins JSON,
   current helper/source/test hashes and exact Windows controller commands.
5. Plan new D:-backed `system-native-identity-r4-20261024.qcow2` sibling from
   frozen capacity parent; host run `native-exit-calibration-r4-20261024`. Preserve
   both failed R2 and R3 siblings, their full current hashes/identities, original
   frozen3, all previous preps/evidence. Generalize explicit storage inventories
   as needed; fail closed on drift. No fresh guest paths by guessing, no overwrite.
   Capacity parent fullSHA c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936;
   failed R2 da698bec9326daf991f4e30b3b7edc702b7f4dc9db76d7c923c9ba3da2f8f89d;
   failed R3 27348d89c77aa7e17f0b1b946ad265ff4191ccaf63f9c24f9544d9a30eaf7715.

NO image creation, launcher execution, guest boot, SSH, consumer retry, PE rebuild,
production changes or browser run in this task. Parent reviews source/tests/fresh
PREPARED_NOT_RUN first. Separate reviewed image-creation and single-run authority
follow only if accepted. Existing raw UTC674/650/435 guards,900/90/80 containment,
unique sockets, privateLO, actual inner/outerVirGL, >=1GiB guest free, complete-empty
root ownership before stage, fixed-path live absence, strict shutdown and all
private D: storage requirements remain. At95% five-hour used checkpoint safely;
weekly99% stops. Preserve C: worktree M wine/main D: master/other workers.

Acceptance later requires natural helper0 and the original nonce/PID binding,
full3221225599/0xc000007f child status, complete evidence and exact owned cleanup.
Observer acceptance alone is not native or Chromium rendering acceptance.
