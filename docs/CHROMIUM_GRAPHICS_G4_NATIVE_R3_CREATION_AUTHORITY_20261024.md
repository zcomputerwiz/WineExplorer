<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R3 preparation accepted; create ONE fresh sibling layer only

Parent accepts5dba08fc after final launcher/procedure/test/manifest review.
Independent164 focused tests passed26.781s/no skips. Parent independently verified
56 current/snapshot/receipt hashes and all16 TAR members, original PEs/19expected
pins, exact current Python approval and durable credential/public-seed metadata.
Repeated read-only normal-chain/owner/identity/full parent+failed-layer hashes
match. New target and HOST outputs absent. Private audit on D:lab:
`native-r3-parent-preparation-review-20261024.json`.

## ONE creation, no boot or guest access

Use exact create argv in immutable R3 preflight f9edf5af01e21807640f44b77160af33ab97253e6b08a10a7b85d6c75ab3fe4a:
`/usr/bin/qemu-img create -f qcow2 -F qcow2 -b <exact frozen capacity top>
<D:lab/system-native-identity-r3-20261024.qcow2>`.
Inherit80GiB; no size, force-share, resize, repair, mount, rebase, commit or delete.
Existing failed `system-readiness-r1-20261024.qcow2` stays a preserved sibling.

Immediately before creation require exact pinned tool/inventory, canonical ABSENT
target, no owners/open chain, >=80GiB host reserve, frozen3 identities/full parent
SHA c2acebb9... and failed sibling identity/full SHA da698bec... . Retain exact
bounded command/stdout/stderr/exit and any partial failure; execute once only.

Afterwards require normal-lock new four-chain (new80/frozen80/old64/Debian3GiB),
exact canonical backings/formats/no dirty or corrupt flags, old failed four-chain
unchanged, all existing identities and repeated full parent/failed hashes within
120s each, owners and immutable inventory. Record new top identity/hash. These
are point-in-time/container checks, not filesystem cleanliness or guest absence.

Commit AND push creation report, STOP parent review before any VM/QEMU/SSH/native
PE/browser execution. No guest evidence overwrite. No automatic run authority,
production changes or other worker actions. Currentusage41%five-hour/53%weekly;
95%/99% stops/no reset credits.
