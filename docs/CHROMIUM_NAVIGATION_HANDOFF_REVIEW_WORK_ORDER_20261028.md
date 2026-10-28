<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wine navigation handoff: parent review and implementation work order

Reviewed root54d259c6/report and independently inspected the private Wine
navigation-phase run's raw NetLog/native results and front PNG. Read the native
Windows comparison at root03213352 and its actual target-correlated JSON trace.
Parent did not independently run worker's32 tests or fully decode the Wine
Perfetto protobuf; those claims remain worker-reported pending verification.

## Review: evidence versus inference

- Wine8GiB actual fixture requests=[], native held browser timed out after45s,
  front is blank, kernel OOM not recorded. NetLog has3219 events spanning
  171667..208156. Pinned net/log/net_log.cc TickCountToString uses milliseconds:
  this is36.489 **seconds**, not36ms followed by39.96s of network silence.
  Exactly one event mentions127.0.0.1:36631: a speculative host resolver request.
- Worker reports a target-correlated Perfetto sequence through StartNow and
  later NotifyRequestFailed/ERR_ABORTED. Its original report relies on event-name
  byte probing; a later message claims packet-sequence decoding. Independently
  verify actual navigation ID/URL, tracks, event times, sources and cleanup
  boundary before treating this as a single target's ordered sequence. Interned
  names present in protobuf do not alone prove events fired or didn't fire.
  Distinguish an abort during the active window from teardown-induced cancellation.
- network_service_instance_impl.cc652 logs a previously bound/disconnected
  Mojo remote *before* starting a service; the string by itself proves neither
  an OS crash nor which process existed earlier. A named process/PID plus actual
  creation and terminal/error evidence is needed before attributing a pipe drop
  or ERR_ABORTED to a service crash. Do not base a production fix on the log text.
- Native Windows exact snapshot with a private sandbox-readable runtime copy
  now GETs target index/scene.js HTTP200, and target DidFinishNavigation records
  has_committed=true/net_error=0. Trace shows StartNow → OnReceiveResponse →
  OnResponseStarted → WillProcessResponse → CommitNavigation → DidFinish.
  Independent native readbacks succeeded but fixture passed=false due to an rAF
  timeout; this is a navigation comparator, not full graphics/presentation proof.
  Native environment/runtime ACL differences do not establish a Wine ACL bug.

## Complex assignment: identify and fix the actual handoff defect

The user explicitly authorizes substantial, multi-step Antigravity/Claude work.
Use OWN D: root codex/chromium-navigation-handoff from54d259c6 with this parent
review applied, and OWN Wine child from current combined02d4ef2/fffa7de if a
real fix is required. Preserve existing token and graphics histories; don't create
a second token stack. Same existing8GiB R4 guest/prefix/runtime/full matched
Wine client/server/privateLO/actualVirGL, normal browser sandbox/features and
original HTTP fixture. No broad framework or unrelated feature-completeness work.

1. Decode the retained Wine Perfetto trace *properly*, including interned names,
   track IDs, event boundaries, process/clock identity, URL/navigation ID and
   actual times. Compare its target track against the private native r5 trace at
   D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028-r5/trace.json
   and the confirmed native sequence in
   docs/CHROMIUM_WINDOWS_NAVIGATION_PERMISSIONS_RESULT_20261028.md on branch
   codex/chromium-windows-navigation-baseline03213352. Inspect raw evidence,
   not just report text; say unknown if a trace is truncated or lacks identity.
   The signal is the first divergent *actual target* operation, not a string
   found anywhere in a protobuf or a post-cleanup cancellation.
2. Follow that boundary into pinned Chromium URLLoader factory/Mojo transport
   and corresponding fffa7de Wine implementation. Check whether the document
   request is constructed, passed over the real pipe, received/acknowledged by
   the network service and connected to loopback. If startup/handle passing,
   AppContainer access or IOCP/named-pipe/socket completion is implicated,
   build focused original Windows PE repros for the same concrete semantics and
   compare native Windows and Wine with normal restricted-token/sandbox behavior.
   A blocking TCP GET or merely an existing test name cannot certify Chromium's
   overlapped/nonblocking/handle-inheritance path. Collect actual status codes,
   bytes, completion ordering and process identity. Do not presuppose ACL,
   ProcessHandleCount, network crash, graphics or OOM as the remaining cause.
3. Where a demonstrated Wine defect affects this path, implement genuine correct
   behavior, focused regression(s) including relevant failure paths, and build
   a matched private candidate on your own D-backed configured source/build.
   Multiple related fixes and native-vs-Wine checks are authorized as needed;
   no artificial one-line/one-file restriction or additional paperwork approval.
   Keep code reviewable and avoid fake success, bypass, broader security changes
   or work on speculative API gaps. Reuse current actual components instead of
   full rebuilds where safe; rebuild a coherent complete client/server if needed.
4. Perform fresh normal 8GiB browser verification after a supported fix; record
   actual index/scene GET, nonce readiness and visible front, trace/log/exit/OOM
   and exact shutdown. If an actual *new* blocker is found, continue focused
   diagnosis/fix/verification, up to two materially different bounded iterations;
   no identical retries or larger blind matrix. A genuine successful page load is
   the target, not merely a fixture server write or alive browser. Distinguish
   page load from WebGL/rAF/presentation/security acceptance.

Targeted existing Chromium/NetLog or semantically inert narrowly scoped Wine
diagnostics may be used to distinguish concrete hypotheses when retained evidence
is insufficient; trace sensitive bytes only as needed and keep all private. The
worker may perform original focused guest/native tests and necessary private
incremental builds, not wait for a separate preparation approval. No external
browser debugging port, attached debugger/suspension, sandbox bypass/feature or
proxy switch, manufactured token/ACL/success, vendor publication, global process
scanner, unrelated host session access, shared loaded build writes, new disk/
runtime download/reinstall, other VM changes or master merge.

Fresh attempt paths and preserve ALL prior evidence/candidates; don't remove caches
to manufacture space. All private data D:/D-backed Debian. Apply existing host/
guest capacity and ownership checks, rawUTC and lifetime/cleanup budgets, unique
sockets, exactowned QMP ACK + guest SHUTDOWN + QEMU0/no fallback/child cleanup.
Stop at a concrete external blocker or after materially distinct authorized
iterations, report exact failure and next test; don't idle or loop on paperwork.

Commit AND push root/Wine branches and original
docs/CHROMIUM_NAVIGATION_HANDOFF_RESULT_20261028.md: target-correlated source/
trace comparison, actual repro results, code/focused tests, browser/HTTP/nonce/
visual and cleanup outcomes, limitations, and remaining blocker. No private
payload/trace/profile in Git. Reply Codex for parent review. Live95%five-hour:
finish bounded active work/cleanup/checkpoint commit+push then reschedule; weekly
99% stop; never use credits. User values actual Chromium, not a perfect harness.
