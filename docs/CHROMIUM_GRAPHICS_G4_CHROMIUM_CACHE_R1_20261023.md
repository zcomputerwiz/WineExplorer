<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium cache r1 — FAILED_PRESERVED, notice review pending

Bounded OFFLINE acquisition checkpoint only, per CHROMIUM_PREP_HANDOFF,
VANILLA_CHROMIUM_PLAN and CHROMIUM_CANDIDATE. No guest access/staging/boot,
installer/browser/symbol execution/download, source build or alternate snapshot.
No adapter implemented or runnable PREPARED_NOT_RUN manifest claimed. Parent is
reviewing upstream notice packaging before that work continues.

## Actual acquisition and discrepancy

Private cache:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-1706509-r1-20261023.cache/`.
One invocation of `python3 -B scripts/g4_chromium_cache.py CACHE` under host WSL;
exit1 at2026-10-23T16:13:42.4456158Z. Raw official metadata/REVISIONS/Gitiles
VERSION, ZIP, complete extracted tree and acquisition.json preserved. No retry,
redownload, re-extraction or overwritten failure record. Acquisition record SHA256
`fd7eebd5dcb30e9a66074b09135faef6d2d7f961c246f1035dd7a5c52145b995`.

Actual metadata matches Win_x64/1706509, generation1790650091108450,
source7d9824099030f5c28c939ba08654562faaad7c09, version156.0.8078.0. The exact
[generation-pinned official object](https://storage.googleapis.com/download/storage/v1/b/chromium-browser-snapshots/o/Win_x64%2F1706509%2Fchrome-win.zip?generation=1790650091108450&alt=media)
returned358,598,977 bytes, published MD5 `cdyiojrRPrbsty2zyhfLXg==`, measured SHA256
`6bdfecb33dbfa7159e5666798bdc28c76347bef913573e899cb43930b78d26cc`.
No redirect accepted. These are checksum/source-mapping records, not a signature
or reproducible-build attestation.

All258 archive entries are regular files beneath chrome-win/, total851,393,923
expanded bytes; all names/types/collisions/size bounds checked before extraction.
All extracted files have measured hashes in acquisition.json. Six actual core/
support PEs checked AMD64, including chrome.exe/chrome.dll/chrome_elf.dll,
D3DCompiler_47.dll/libEGL.dll/libGLESv2.dll. chrome.exe SHA256
`c84cd1e7ce7865ccc13bf00f33a3cb934d293c38a1a106585de3d3077c888c99`.

Read-only host FileVersionInfo returns file AND product156.0.8078.0 on each
of chrome.exe/chrome.dll/chrome_elf.dll. After parent review, tightened validation
requires exactly three typed/unique/exact expected full-path rows, with one20s
budget covering all wslpath conversions and PowerShell resource inspection.
Only this data inspection was replayed against retained files, successfully;
adjacent `.version-replay.json` retains actual results. No browser was launched.

Final gate failed `ValueError('No retained license/notices')`: no member filename
contains license, credits or notice (case insensitive). This is specifically
absence of standalone named files, NOT proof of absent embedded notices or a
license violation. No opaque PAK parsing, browser chrome://credits execution or
new notice acquisition attempted. Failed cache remains unaccepted pending parent
packaging review; never silently relabel it verified.

## Code/tests and continuation

Added narrow `g4_chromium_cache.py` and focused `test_g4_chromium_cache.py`.
Actual host-Linux command from explicit C: worktree scripts directory:
`python3 -B -m unittest test_g4_chromium_cache -q`:12 tests PASS/no skips.
Synthetic positives/negatives cover exact metadata, bounded downloads/partial
failure/checksums/deadline/redirect refusal, hostile paths/types/collisions/bombs,
fresh output/space, extraction hashes, PE bounds/architecture, strict resource
version/path/schema/deadline, missing standalone notices and preserved acquisition
failure without retry. Tests never download real payloads. Existing fixture,
Edge/graphics/native observer code unchanged; unrelated tests/Node not rerun.

Adjacent `.cache-audit.json` records retained payload/tree hashes and current
source/test hashes separately from the immutable failed acquisition record.
Current helper includes the later version-validation fix; replay is not claimed
as a second acquisition or retroactive executed-source attestation.

Remaining: parent notice-packaging decision, then explicit adapter/affected
regressions and immutable runtime/source/helper/19-pin preparation using accepted
NEW capacity overlay/launcher. No live authority follows this checkpoint.
C: graphics branch only; pre-existing `M wine`, D: master and other workers intact.
