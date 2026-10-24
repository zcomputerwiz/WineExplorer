<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent offline storage/shutdown review; NO creation or boot authority

While Luna implements readiness in disjoint files, parent inspected the actual
last launcher, stopper, serial and QEMU result. Last failed guest was still in
systemd/cloud-init startup. TCP forwarding was available before SSH banner.
The45s QMP request wait expired, then SIGTERM fallback; absence/QEMU0 is not
graceful shutdown or ext4 cleanliness. Underlying guest shutdown cause remains
unproven; readiness gating is a repair to test, not an already demonstrated fix.

## Preserved current disk

Read-only normal-lock qemu-img info --backing-chain shows EXACT three layers:
80GiB system-capacity-r1-20261023.qcow2 ->64GiB system.qcow2 ->3GiB Debian image
under WEX-G4-Acceleration-20261022/image-source. All qcow2 dirty/corrupt flags
false; these are container flags, NOT guest filesystem cleanliness evidence.
No image, guest or launcher was modified in this review.

Parent bounded physical-file SHA256 of current80GiB top (actual3353083904bytes):
c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936.
Completed20.7708s within120s; exact before/after identity equal: device71,
inode562949954610109,mtime_ns=ctime_ns=1792790710953668500. Owner/open-chain
snapshots empty before/after. This is not an atomic lock over the whole chain.
Old65GB+Debian full pre/post hashes remain preserved; do not rescan unnecessarily.

Before any further writable boot, prepare/review a fresh D: top named
system-readiness-r1-20261024.qcow2, backing the CURRENT80GiB top. Exact future
command: qemu-img create -f qcow2 -F qcow2 -b <absolute current80GiB path>
<absolute new D: path>, NO SIZE (inherit80GiB), no -u/-U/resize/rebase/commit.
Do not execute yet. Require targets absent/canonical, idle owners and full
current-top hash before/after creation, exact four-layer normal-lock metadata,
adequate D: reserve, then separately reviewed launcher proposal pointing ONLY
at new top. Preserve original layers read-only; journal replay, if any, must
land in new top. No manual fsck or repair authorized. Rehash frozen former top
after eventual run as well. A fresh overlay is not a claim the guest is repaired.

## QMP defect and next bounded implementation

Actual launcher calls recv once for greeting/capabilities/powerdown, discards all
bytes and closes socket before waiting for QEMU exit. Stream fragmentation,
coalescing, error responses and asynchronous events are not interpreted. This
is a definite protocol-observability defect, NOT proven cause of last timeout.

After readiness helper review, Luna should implement ONLY a small tracked
g4_qmp_shutdown.py + tests, before launcher integration. Fixed commands
qmp_capabilities/system_powerdown with unique request IDs; bounded buffered
newline JSON framing, duplicate-key/schema/size limits, matched replies,
explicit errors and retained asynchronous events. Keep connection for SHUTDOWN.
POWERDOWN or command acknowledgement alone is NOT shutdown proof. Require
SHUTDOWN guest=true/reason=guest-shutdown plus actual QEMU exit0 and no fallback
for a new graceful verdict. Guest-reset/host-signal/quit, EOF without event,
timeout/malformed/oversize responses and fallback can never pass. No quit/reset
or hard-kill command hidden as graceful; existing safe fallback remains failure.
Test actual parser with fragmented/coalesced messages, error/wrong IDs, events
before replies, missing guest event, deadlines and real local socketpair. No
QEMU/VM run in that helper step. Keep whole shutdown inside existing90s reserve.

Only after both helper reviews: one combined OFFLINE integration/safety-overlay
preparation, parent review, then separate creation authorization, then ONE later
readiness + unchanged idle probe diagnostic. No automatic calibration/browser.

Primary protocol/overlay references researched by parent:
- https://www.qemu.org/docs/master/interop/qmp-spec.html
- https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html (system_powerdown,
  SHUTDOWN: acknowledgement not guest completion; reason/guest distinguish cause)
- https://www.qemu.org/docs/master/tools/qemu-img.html (create with backing file
  records differences; backing never modified unless committed)

Parent-plan/Luna-implement/parent-review; all tracked work C:graphics branch,
private VM/storage D:, M wine/main D:master/other workers untouched. No merge,
security bypass, runtime download, production rebuild or OOBE/token imports.

## QMP helper handoff after readiness9d485b42 acceptance

Luna write scope ONLY scripts/g4_qmp_shutdown.py, its test module and concise
report. Library, no CLI that starts/stops arbitrary VMs; no private launcher or
procedure edits yet. Fixed qmp_capabilities/system_powerdown, no quit/reset.
Total protocol observation <=50s INCLUDING connection/negotiation/ack/event,
not50s per message; caller separately owns bounded QEMU wait/cleanup within90s.
Retain bounded framed transcript/errors and distinguish protocol ACK from guest
SHUTDOWN. Return protocol/event evidence, NOT an independent graceful verdict:
only future caller can combine it with QEMU0 and no fallback. SHUTDOWN before
the owned powerdown request cannot be attributed to it; event after request but
before its matching ACK must be buffered, not lost. EOF/POWERDOWN/host-signal/
guest-reset/timeout/invalid/wrong-ID replies cannot masquerade as guest shutdown.
Injectable socket/clock for tests; include real local socketpair fragmentation/
coalescing/event-order controls. No QEMU/VM/SSH in this stage. D: temp/log outputs.
Commit AND push exact files, STOP parent review before integration/preparation.
