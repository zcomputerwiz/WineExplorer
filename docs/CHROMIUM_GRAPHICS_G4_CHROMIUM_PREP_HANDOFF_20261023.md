<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Bounded Chromium preparation handoff (after capacity acceptance)

VANILLA_CHROMIUM_PLAN and CHROMIUM_CANDIDATE_20261023 govern. This handoff
does NOT authorize a guest boot, browser execution or working around failed
capacity gates. Parent must accept actual maintenance evidence first. Reuse
Luna Dirac; no new agent or changes to other workers/master/submodules.

## Deliverables

1. Bounded host D: acquisition of ONE official Win_x64/1706509 ZIP, generation
   1790650091108450,358598977bytes, published MD5 cdyiojrRPrbsty2zyhfLXg==.
   Capture actual official metadata/REVISIONS/exact source VERSION responses.
   Require matching catalog/version156.0.8078.0/source7d982409..., exact downloaded
   length, checksum and measured SHA256; no redirects to unapproved hosts or
   alternate build/retry after partial failure. Keep raw failed payload/logs.
   No installer/symbol/source-build acquisition. All private files D:, no host
   browser execution. Metadata fetches/host cache are not a guest live test.
2. Inspect all ZIP paths/entry types before extraction. One chrome-win/ root,
   relative canonical paths, no .., absolute/drive/UNC/backslash/ADS/NUL paths,
   symlinks or Windows case-fold collisions/reserved-device aliases. Bound
   member count, uncompressed total/individual size, duration and host free
   space BEFORE extraction. Fresh output only; preserve partial failure, never
   overwrite/delete. Hash all regular files, retain license/notices. Validate
   actual x64 PE chrome.exe/chrome.dll/support files. Read Windows version
   resources as data (e.g. bounded FileVersionInfo), never launch the browser
   on the host. Source VERSION alone is not binary-version evidence. Any version
   or archive-layout mismatch returns for parent review, not a silent fallback.
3. Small explicit Chromium adapter to existing isolated headful controls,
   preserving Edge defaults/negative gates. No general browser framework.
   Reuse the accepted installed Wine prefix/environment, but stage portable
   Chromium separately and use a fresh profile. State that the prefix contains
   Edge; this is a shared-environment comparison, not a pristine-prefix proof.
   Do not copy/reinstall Edge or force different Wine/security configuration.
4. Immutable PREPARED_NOT_RUN inventory: host cache/runtime tree/source/helper
   hashes, fixture bytes,19 accepted environment pins AND new runtime inventory,
   exact guest cache path/full Windows image aliases, transfer/extraction/free
   space gates, normal app/HTTP argv, source-stage archive and exact launch/run/
   stop commands. No guest staging/boot during offline preparation. Future live
   operation must reserve room for ZIP+expanded tree+Wine/profile before copy,
   independently hash/check paths in guest, then use offline per-run isolation.
   Record the accepted NEW capacity overlay and launcher hash, not old manifests.
5. Commit AND push code/tests/report. Parent independently reviews actual cache,
   version/hash/source mapping, source/tests and immutable preparation before
   separately authorizing ONE live baseline. No automatic retries or Edge run.

## Concrete adapter hazards found in existing source

- check-g4-edge-headful.py binds PREFIX/EXE, installed payload/persistence checks,
  output names and C:/Z: aliases to Edge. In window_record(), EXE.relative_to(
  PREFIX/'drive_c') cannot describe portable Chromium outside C:. Compute only
  aliases actually backed by verified drive mappings; do not accept basenames.
- page_record() accepts only empty or ' - Microsoft Edge' suffix. Chromium needs
  its own exact vendor suffix policy, not a permissive combined suffix list for
  Edge. Keep original fixture WEXEDGE protocol prefix and637-byte page/scene.js
  UNCHANGED; their legacy name is not reason to alter comparison semantics.
- Native observer is already generic/query-only; avoid changing/rebuilding it
  unnecessarily. Preserve unique native HWND/WinPID/image/creation/geometry and
  independent Linux PopenPID/start/argv/prefix/profile identity checks.
- No owned-thread/selected-stack attachment flags in this baseline. Same local
  HTTP page/app mode, real VirGL and security requirements. No no-sandbox,
  GPU workaround, feature disabling, remote-debug endpoint or version spoof.
- Preserve strict WebGL1/2 readbacks/renderer checks, frozen front pixels and
  capture/lifecycle failure handling. GPU-child existence or startup flags alone
  are not effective sandbox or presentation evidence. Keep limitations explicit.

## Required tests / review evidence

Synthetic bounded download failures (wrong generation/catalog, short/oversize,
bad checksum, deadline), hostile ZIP names/types/collisions/size bombs and
existing output/space failure. Do not download real payloads in unit tests.
Explicit Edge-vs-Chromium commands/title suffixes/image aliases/PE/version/path/
nonce/profile/owner mismatches must fail closed; preserve Edge tests unchanged
unless adding explicit per-vendor parameters. Test that forbidden flags cannot
enter normal argv, wrong/absent runtime files fail before consumer launch, and
capture/cleanup/readiness failures cannot pass. Execute original Node fixture
tests and affected focused Python regressions; report actual counts/skips.
No unchanged full graphics/native socket reruns when their code is untouched.

## Parent notice-format correction from actual package/source

Actual pinned ZIP downloaded once with SHA256
6bdfecb33dbfa7159e5666798bdc28c76347bef913573e899cb43930b78d26cc,
correct length/published MD5 and258 extracted files; three main PE version
resources report156.0.8078.0. Initial cache record FAILED_PRESERVED solely on
an invented standalone license/credits/notice FILENAME requirement. Preserve
that failed acquisition.json and all bytes, no redownload or re-extraction.

Parent actually fetched these exact-revision primary sources as Gitiles TEXT:

- [LICENSE](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/LICENSE).
- [about_ui.cc](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/chrome/browser/ui/webui/about/about_ui.cc)
  dispatches the credits page to about_ui::GetCredits.
- [credit_utils.cc](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/components/webui/about/credit_utils.cc)
  loads IDR_ABOUT_UI_CREDITS_HTML from ResourceBundle.
- [about_ui_resources.grdp](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/components/resources/about_ui_resources.grdp)
  declares that generated HTML as Brotli-compressed binary data.
- [win-archive-rel.json](https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/infra/archive_config/win-archive-rel.json)
  is the archive config, not obsolete FILES.cfg; includes resources.pak.

Amend the retention check for this PRIVATE diagnostic: retain ALL original ZIP/
runtime/resource bytes unchanged and fetch/hash the exact source LICENSE and
these small provenance documents into a fresh private subdirectory. Do not
require a standalone filename that upstream does not provide. Do not claim
compiled credits text was extracted/inspected, that the root license covers
every component, or that redistribution/ISO bundling has been cleared. Browser
credits may be inspected later once functional; no host/guest launch just for
this check and no new decoder/build dependency or symbols download is needed.

Luna may add a fresh, additive cache-review record referencing the original
failed record's SHA256 and actual unchanged ZIP/tree/version evidence. Revalidate
actual paths/versions/checksums after the targeted file_versions fix, without
overwriting initial evidence. Only this specifically reviewed notice-format
failure is eligible; other missing/mismatched payload/version/archive evidence
must still fail. Parent reviews the resulting evidence before live authority.
