<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: handoff repro corrections and project-build rework

Reviewed root c4f62c73 report plus both committed repro sources, actual retained
Wine network launch log, and current WSL build/source paths. No new VM/browser
or runtime test was run by parent. Worker Perfetto SQL sequence/stock-Wine
results remain reported, not independently reproduced.

## Findings affecting the conclusions

1. Actual retained chromium-navigation-phase helper.stderr.log lines852/858
   launch network.mojom.NetworkService with --service-sandbox-type=none. This
   does not by itself prove every effective Windows token property, but directly
   contradicts the report's assertion that this observed child is necessarily
   launched with AppContainer security capabilities. A missing tracing slice for
   PID1036 is not evidence of a process death, a pipe disconnect, or its cause.
   Worker-reported target event ordering is useful but do not call the precise
   lost IPC operation proven without target-correlated receive/send/status and
   actual process identity/terminal evidence.
2. Both synthetic binaries were tested against STOCK Debian Wine10, not the
   combined project runtime fffa7de/02d4ef2. Results cannot establish a defect
   in the actual candidate. The restricted-token repro fails on Windows too:
   SetTokenInformation(TokenAppContainerSid) returns87 and CreateProcessAsUser
   returns5, with no child on either platform. The source logs SID setup failure
   then continues, and does not implement Chromium's
   PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES + HANDLE_LIST launch. Therefore
   there is no established valid native AppContainer baseline or demonstrated
   project regression; lack of elevation has not been shown to be the cause.
3. The anonymous-pipe repro uses CreatePipe and STARTF_USESTDHANDLES, not the
   Chromium platform channel's overlapped duplex named-pipe + explicit handle
   list contract. It does not exercise IOCP. Parent does a blocking ReadFile
   without an actual deadline and returns0 after a failed ACK, so the claimed
   bounded test can hang or false-pass. It cannot rule out actual Mojo bootstrap,
   named-pipe, overlapped/IOCP, or explicit handle-list defects.
4. The asserted missing build is not established. Parent verified in Debian
   /var/tmp/wex-g4-token-parent-20261027/build/config.status and actual ELF
   build/loader/wine and build/server/wineserver exist. Wine source/server/main.c
   exists; source HEAD currently aa79f010 and config.status references that
   source. Existence does not prove the tree is clean or fully runnable, and
   build/source may carry prior trace work: inspect state, never overwrite a
   loaded/shared build. The last actual browser trial also used an existing
   guest matched candidate at /home/debian/wex-g4/wine-control-channel-20261027/
   build/wow64. Reuse if intact. server/program.c is not evidence of an absent
   source tree. No full clean build or new image is a precondition to checking.

Native Windows comparator 03213352 has target HTML/JS200 and target document
commit/net_error0; its overall fixture still reports rAF timeout. Wine remains
blank/HTTP[] at8GiB; no actual fix or post-fix browser test has occurred.

## Assigned rework: actual candidate, actual launch contract

Antigravity coordinates the same capable implementer in OWN D: root
codex/chromium-navigation-handoff continuing c4f62c73, and OWN Wine child of
02d4ef2/fffa7de only if actual defect is found. Keep combined token/graphics
histories and current normal sandbox/feature behavior.

1. Inspect retained target-correlated traces and actual process creation evidence
   with decoded IDs and times. Identify which actual child/utility handles or
   endpoint carry the target URLLoader request; do not equate a missing trace
   slice or remote652 log with death, nor assert AppContainer where launch says
   service-sandbox-type=none. Identify the concrete observed prerequisite and
   use native Windows comparator to select a discriminating semantic test.
2. Correct/rewrite the focused original PE repro to exercise pinned Chromium's
   actual duplex CreateNamedPipe FILE_FLAG_OVERLAPPED + client connection, the
   actual STARTUPINFOEX/explicit HANDLE_LIST and any independently proven
   security-capabilities/token form. Include real IOCP/async ordering and error
   status only if used by the implicated path. Use genuine supported Windows
   AppContainer creation via documented SECURITY_CAPABILITIES when relevant;
   do not manually SetTokenInformation(AppContainerSid) then ignore failure.
   Child must prove actual token and inherited handle, parent must verify exact
   bytes/status/exit and use bounded wait/owned cleanup; all failures nonzero.
   Run the SAME PE against native Windows and the ACTUAL project Wine runtime,
   not stock Wine as substitute. Capture evidence privately. If the actual path
   is not restricted, do not add unrelated security-capabilities just to force it.
3. First inspect existing configured source/build and last guest candidate; use
   them read-only as inputs. If runnable, execute repro in a fresh own prefix and
   isolated lab. If modification/rebuild is needed, create an OWN copy or clean
   own D-backed source/build and preserve prior work; don't mutate a loaded/shared
   build. Diagnose a genuine missing dependency if it fails, do not infer from
   the nonexistent server/program.c. A coherent full build is authorized if
   actually needed, no extra approval loop; user prioritizes actual Chromium.
4. Implement supported Wine fix(es) only for reproducible actual semantic
   differences, add focused meaningful regression(s), build a coherent matched
   private candidate, and run normal8GiB real-browser page verification. Up to two
   materially different justified iterations remain authorized. Check HTTP index/
   scene, target commit, nonce, visible front, native status and cleanup. If no
   defect is proven, report exact concrete failing operation/remaining evidence,
   rather than attributing it to stock Wine or changing security features.

Same preserved R4 disk/prefix, private loopback, actual VirGL, unique sockets,
capacity/space/idleness/rawUTC/lifetime/cleanup-reserve/exact owned QMP ACK +
guest SHUTDOWN/QEMU0/no fallback/child stop rules. New stage/output/profile/
prefix paths, no prior evidence/cache deletion, unrelated worker/VM changes,
network exposure, debugger attach/suspend, fake success, sandbox bypass, vendor
payload publication or master merge. Private data D:/D-backed Debian. No generic
harness redesign; focus on the page loading.

Commit AND push both root/Wine branches if changed and original
CHROMIUM_HANDOFF_REWORK_RESULT_20261028.md, with command/output/evidence paths,
actual runtime identity, meaningful tests, browser/HTTP/nonce/cleanup results and
remaining uncertainty. Reply Codex then stop. At95%five-hour finish bounded active
cleanup/checkpoint+push; weekly99% stop; never credits.
