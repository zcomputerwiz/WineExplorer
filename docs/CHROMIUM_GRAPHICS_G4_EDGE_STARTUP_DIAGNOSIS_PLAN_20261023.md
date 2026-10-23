<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge startup blocker: bounded diagnostic, not a speculative security fix

Parent reviewed 6fccc595, independently verified all 280 archived file hashes,
read actual native-window/process/browser logs and verified successful owned
cleanup/QMP/QEMU/controller shutdown. Preserve r1 FAILED: readiness timeout.
Wineboot preserved the observed installed bytes/registration. Browser window and
GPU child appeared, but no renderer launch/readiness was observed. The log reached
FileURLLoader::Start for the correct original file URL. These facts do not locate
the blocker in file IO, navigation, app-window behavior, sandbox or rendering.
No production patch or token/security-worker import is justified yet.

Current harness provides no actual front on pre-readiness failures. Fix that
diagnostic gap first; then ONE separately reviewed instrumented startup attempt.
Parent plans/reviews, Luna implements. No live execution in this offline milestone.

## Minimal offline implementation

Extend the existing Edge runner, not another browser framework. When a browser
was actually created and readiness/observation fails while the owned compositor
is still alive, attempt ONE bounded whole-front PNG before process cleanup.
Label it failure-only diagnostic, not a fixture pixel verdict or retry/readiness
poll. Preserve the original exception if compositor/capture/logging also fails;
record that secondary error. No capture after rejected pre-process guards or
nonexistent compositor/browser. Never delay mandatory cleanup or force repaint,
interaction, navigation, readback or GPU reset to obtain an image.

Add an explicit startup-diagnostic option. Keep ordinary smoke defaults unchanged;
the diagnostic mode can never produce a rendering/G4 acceptance pass. Retain the
same original page, actual installed Edge/prefix and a NEW profile. It changes
only bounded logging/observation: Wine file/exception/process traces and narrowly
targeted Chromium VLOG modules (file_url_loader_factory, navigation_request,
render_process_host_impl, child_process_launcher_helper[_win]) are permitted.
Do not change security, OS/backend, file-access policies, URL scheme, payload,
installer/pending operations or add browser features to make startup succeed.
No --no-sandbox/--disable-gpu/renderer-in-process or arbitrary command input.

Keep a conservative combined browser diagnostic log-size limit (e.g.128 MiB),
checked during the bounded wait. Stop and retain a log-limit result if exceeded;
do not truncate logs or continue until preferred evidence appears. A quota limit
is not a product failure diagnosis. Preserve sampled native windows/roles and
monotonic timing. At most one terminal failure image, not a screenshot loop.

Before browser creation, a tiny original read-only Win32 file-probe mode/helper
may verify the staged original fixture through its actual Windows Z: path:
GetFileAttributesEx, CreateFile with read/share-read/write/delete, bounded ReadFile
and reported size/bytes/hash or known original prefix. No Microsoft file reads
or broad filesystem traversal. Reuse the existing native helper if small; do not
build a general IO-conformance suite. Its success establishes only that simple
Win32 IO works, not that Chromium's async loader completed. Missing/failed probe
is a preserved diagnostic result, not permission to rewrite paths or bypass ACLs.
Avoid a probe that could hang past the parent run deadline.

Executable positive/negative tests must cover original-error preservation with
successful/failed/timeout terminal capture, capture absent before process guards,
diagnostic-vs-normal argv/environment, log quota/deadline behavior and cleanup.
If adding native probe behavior, compile/link it and test protocol parsing and
failure gating; do not claim a mocked syscall tests real Wine behavior.
Rerun the existing focused controls/actual-fixture Node tests. Commit AND push
implementation, tests and a complete PREPARED_NOT_RUN manifest, return for review.

## Later single diagnostic execution and interpretation

Only after parent acceptance of source/tests/manifest: one fresh-output/profile
diagnostic on the owned D:-backed VirGL guest, same approved installed prefix/build.
No reinstall, new payload, other VM or loaded-binary rebuild. Enforce >=1 GiB free
before transfer/create, all stack/installed/deployed hashes, offline private
mount/network/filesystem X sockets, explicit inner/outer VirGL and 650/435-second
raw-UTC guards. <=285s diagnostic within300s SSH, bounded45s readiness, sufficient
cleanup reserve, 900s launcher/90s shutdown reserve and exact-identity QMP shutdown.
Retain trace/front/prefix/profile on D: and archive hashes, not proprietary payload.

Inspect the actual failed front and trace ordering: did original file open/read
complete, did loader deliver headers/sniff MIME, did renderer creation get requested,
did its token/job/process setup fail, or is app/navigation waiting elsewhere?
Use current public Chromium loader code as a hypothesis map, not an assertion
about Microsoft-specific source/version. Reference:
https://raw.githubusercontent.com/chromium/chromium/main/content/browser/loader/file_url_loader_factory.cc
That code has several file/policy/read/response stages after its Start log.

If evidence still cannot distinguish an owner, report that exact missing evidence
and plan one narrow further diagnostic (e.g. owned thread stacks), not an invented
cause or broad patch. A genuine diagnosed owner can justify a minimal public-
contract implementation and original regression test in a later reviewed step.
No security bypass, cosmetic expansion, token/OOBE imports, master merge or G4 claim.
Keep usage95%/weekly99% checkpoint policy and never consume reset credits.
