<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: protected readiness preparation accepted

Reviewed Luna `48183f33` after helper reviews `dd5946fb` and `529ca633`.
The parent independently ran all 69 focused integration/readiness/QMP/launcher/
budget/remote-failure/idle-diagnostic tests under Debian: 5.174s, OK, no skips.
Reviewed actual private launcher, procedure changes, final tests and immutable
preparation. No browser, native calibration or guest readiness result is inferred.

Parent private audit is retained on D: at
`WEX-G4-VirGL-20261022/native-idle-ready-parent-review-r2-20261024/audit.json`.
It verified 27 current file hashes: seven helpers, nine sources, two seeds, ISO,
prior preflight, six retained change artifacts and qemu-img. The preflight hash
is `ce85b091eedd02e217f36898f4841ba0da54bae52e075ec94ef27a497ef219b3`.
All 19 expected guest pins match the preceding preparation; these are expected
values, not a new live guest hash check. The original idle probe remains
`d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0`.

Normal-lock metadata matches all three original layers. Their identities remain
unchanged; full former-top SHA256 remains
`c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936`,
independently rehashed in 20.406s within the 120s bound. Owner/open-file snapshots
were empty before and after. New disk and run paths are absent; D: reserve passes.
These are point-in-time checks, not permanent ownership or ext4-clean proof.

The first parent audit completed its checks but failed writing its final record
because Linux Git cannot interpret this Windows worktree's Windows-format gitdir.
Its empty output directory is preserved. The corrected r2 audit receives the full
HEAD from Windows Git and repeats the checks; no production/preparation change,
image operation or guest execution occurred in either audit.

## Next bounded authority: create only, no boot

Luna may perform ONE exact creation from the accepted manifest into
`system-readiness-r1-20261024.qcow2`, backing the frozen current 80GiB top.
Immediately recheck current inventory, no owners, canonical absent target,
at least 80GiB host reserve, all original identities and full former-top hash.
Use the exact `qemu-img create -f qcow2 -F qcow2 -b <former top> <new top>` argv;
no size, force-share, resize, repair, mount, rebase, commit or deletion.

Retain bounded stdout/stderr/status and exact identities. Afterwards require
normal-lock exact four-layer metadata/80+80+64+3GiB/backings/no dirty or corrupt
flags, unchanged original identities and repeated full former-top hash, owner
snapshots and inventory. Failure preserves output and stops without retry.
Commit AND push the creation report; STOP for parent review before any boot,
SSH, helper or browser execution. Existing user authority covers the disposable
test environment; this boundary is our internal staged review, not a new human
permission request.

The reusable harness guide is `docs/APP_TEST_HARNESS.md`, published `0e23e4e6`.
Current usage 51% five-hour / 23% weekly; existing 95%/99% stopping rules apply.
