<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Official Windows x64 Chromium candidate, metadata only

Parent read official upstream metadata2026-10-23 after reset. NO runtime/symbol
download, extraction, VM boot or browser execution performed in this research.
Capacity remains the prerequisite. VANILLA_CHROMIUM_PLAN_20261023 governs.

The project's [download instructions](https://www.chromium.org/getting-involved/download-chromium/)
link the upstream snapshot archive. ChromiumDash Stable Windows metadata listed
156.0.8078.49 with main branch position1706539; that exact snapshot prefix was
absent. A bounded prefix listing found1706509 (30 positions earlier), whose
[REVISIONS](https://commondatastorage.googleapis.com/chromium-browser-snapshots/Win_x64/1706509/REVISIONS)
identifies source7d9824099030f5c28c939ba08654562faaad7c09,
`refs/heads/main@{#1706509}`. That exact source's
[chrome/VERSION](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/chrome/VERSION)
was actually fetched as Gitiles base64 text and decodes to156.0.8078.0.

This is a same-major156 comparison with retained Edge156.0.4314.42, NOT the
same patch/source/ANGLE revision or an established mapping of Edge's source.
Do not present it as a controlled vendor-only difference or exact stable build.
It is preferable to silently selecting current main158.0.8121.0/latest1725744.

## Candidate payload metadata (not fetched payloads)

Official bucket object Win_x64/1706509/chrome-win.zip:

- Bytes358,598,977; generation1790650091108450; updated2026-09-29T02:48:11.204Z.
- Bucket MD5 base64 `cdyiojrRPrbsty2zyhfLXg==`; CRC32C `XRR6ig==`.
- Generation-pinned download URL supplied by the bucket:
  `https://storage.googleapis.com/download/storage/v1/b/chromium-browser-snapshots/o/Win_x64%2F1706509%2Fchrome-win.zip?generation=1790650091108450&alt=media`.
- SHA256 NOT YET MEASURED. After accepted capacity, actual bounded download,
  payload/hash/PE/support-file and archive safety verification remain required.

The same actual object listing includes chrome-win32-syms.zip,912,844,905bytes,
MD5 `ow8Id/TjqPzZ1A7gc8rqfw==`, CRC32C `YhKeAA==`. The filename alone does
not prove x64/matching PDB GUIDs or useful WineDbg symbol loading. Do NOT download
it initially; if diagnosis needs symbols, inspect exact object generation and
verify PDB identity against the actual browser module before using it. This
concrete listing qualifies the download page's general statement that snapshots
lack symbols; neither statement proves usable symbols for our actual runtime.

## Next implementation handoff

After independently accepted capacity maintenance, Luna may prepare this ONE
official runtime privately on D:, using VANILLA_CHROMIUM_PLAN. Retain actual
metadata responses/versions/checksums in a fresh private inventory, source refs
and measured SHA256; fail if object generation/size/hash/source mapping changes.
Archive safety tests and x64 browser/support-file identities must pass. Parent
reviews adapter/tests/immutable preparation before ONE isolated live baseline.
No symbol download, source build, alternate snapshot or installer substitution
is implied. Main source checkout/master and other workers remain untouched.
