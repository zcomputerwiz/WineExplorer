<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Codex review of the Raven reuse proposal

Reviewed 2026-11-11 against Raven commit
`dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a` and the isolated Wine base
`c3f59e05a37134aee529a7d636af3ba67a9efc5c`. Verdict: endorse a bounded
comparison of the APPX reader; do not import or replace current implementations
on this evidence. The original note remains unchanged. This is a static review,
not a Raven build, runtime result, complete security audit or reuse approval.

## Findings and qualifications

1. **P2, NOTE_RAVEN_REUSE.md:33: distinguish lazy content checks from complete
   package verification.** `appx_package_reader` opens the ZIP, reads the block
   map and content types, and cross-checks declarations; it does not consume
   every payload stream. Hash comparison occurs in `load_block`, called by
   `file_read`. A caller that opens/enumerates and publishes an installed record
   can leave corrupt payload blocks unread. The comparison must drain every
   payload to its declared end, enforce byte limits and fail before publication
   on any read/hash error. Even successful block comparisons do not authenticate
   the unsigned block map. The note correctly identifies signature verification
   as absent; keep that distinct from integrity against a supplied map.
   Sources: [package.c:266](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/native/guest/raven-appx/package.c#L266),
   [support.c:659](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/native/guest/raven-appx/support.c#L659).

2. **P2, NOTE_RAVEN_REUSE.md:68: make the comparison gate include hostile inputs,
   not only the valid U1 fixture.** The existing disclaimer is accurate, but the
   suggested experiment needs explicit rejection criteria before a parser choice.
   `archive.c` admits a central directory up to roughly 2 GiB and performs
   first-match normalized part lookup; it does not provide our central-entry
   encryption/type/CRC/path-alias policy. `manifest.c:read_identity` selects
   Identity by local name, does not enforce a single foundation-namespace
   Identity, and silently truncates constructed names into 256-WCHAR arrays
   through `append`. These are concrete gaps against our bounded catalog-input
   contract, not demonstrated exploitation or a complete Raven audit. Include
   foreign/duplicate Identity, overlong names, path aliases/traversal, malformed
   local headers, CRC/encryption/modes, expansion limits and unread corrupt
   blocks. Do not weaken our current rejection rules to match Raven.
   Sources: [archive.c:81](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/native/guest/raven-appx/archive.c#L81),
   [manifest.c:630](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/native/guest/raven-appx/manifest.c#L630).

3. **P2, NOTE_RAVEN_REUSE.md:25: qualify the licensing authority wording.** The
   per-file list is the correct starting point for Raven's guest-adapter
   exception, but upstream `LICENSES.md:42` expressly makes file notices
   authoritative and says the manifest does not grant third-party permissions.
   Preserve and check those notices and dependency terms as well as the list.
   Both generated headers also have separate MIT entries in LICENSES.json,
   under interface-declaration components, pinned to windows-rs
   `58217a690d6256ee229044a174de67d72d28bc35`. That revision's Microsoft MIT
   text was checked. Retain its notice if declarations are copied; the GPLv3
   generator scripts are not part of that permission to copy code.
   Sources: [Raven notices](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/LICENSES.md#L42),
   [declaration inventory](https://github.com/zcomputerwiz/raven/blob/dc87fd0abfe468379c80b38f8ea5e7f856b5ec0a/LICENSES.json#L632),
   [windows-rs MIT text](https://github.com/microsoft/windows-rs/blob/58217a690d6256ee229044a174de67d72d28bc35/license-mit).

## Confirmed useful boundaries

The six named APPX files, winrt.c/winrt.h, twinapi.c and deployment.c are
explicitly listed as MIT Raven guest adapters, and their inspected file
headers agree. Raven's root LICENSE preserves that exception. No Raven host
call was found in the six APPX files; twinapi has the described optional
RavenTrace lookup. This confirms the note's scoped source/licensing observations,
not the safety or correctness of those implementations.

Reuse priority should be APPX ABI/block-map reference and differential test
cases first. Keep any parser experiment in the separately reviewed tools
workspace; a Wine packaging DLL is a separate boundary/linkage decision under
the authoritative README, not an implied change from this proposal.

Do not substitute Raven's generic WinRT object/async helper for Wine's existing
machinery in R4. Wine already has windows.storage/async.c; R4's typed wrapper
must retain its state machine. Raven's helper uses its own CreateThread worker,
callback/cancellation behavior and all-agile object convention, all requiring
separate validation if ever reused. Wine also already registers Language,
AnalyticsInfo, NetworkInformation, ApiInformation and PackageManager. Evaluate
missing methods rather than introducing duplicate class registrations. The
empty-package deployment stub does not advance U1's real package model.

The next comparison can include Raven and the MSIX SDK as optional candidates
against the same bounded corpus and our current original inspector. It is not
a dependency that should stall process/token identity or StorageFolder work.
No Raven source has been written into either project, compiled or executed by
this review; no current implementation assignment was redirected.
