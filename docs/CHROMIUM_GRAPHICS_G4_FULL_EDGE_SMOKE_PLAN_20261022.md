<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Next consumer: full Windows Edge executable, scoped graphics smoke

This is a conditional next plan, NOT execution authority while the budget-guard
repair and remaining regression cells are active. Parent plans/reviews; Luna
implements. First finish/review the current cells or diagnose their first failure.

## Why and authority

Electron/WebView2 passes do not establish full Edge browser startup/rendering.
The next high-value consumer is the actual Windows x64 Edge browser, using a
fresh private prefix/profile and an original offline page. Start with a small
headful graphics smoke; do not imply normal tabbed-browser/lifecycle acceptance.

An asynchronous user question requests acceptance of Microsoft's current
[Edge terms](https://www.microsoft.com/en-us/edge/server/download) and private
download/install authority. Do NOT download the browser payload, accept terms or
install until the human confirms. Existing WineTricks-component consent is not
proof of acceptance of this separate browser agreement. No vendor binary/art is
to be committed, uploaded or bundled. Payloads/evidence/storage remain on D:.

## Bounded implementation milestone (separate later dispatch)

1. After consent, use Microsoft's official Windows x64 offline package channel.
   Resolve/record the actual version and official artifact URL at execution time;
   do not infer current releases from cached search results. Preserve metadata,
   SHA-256 and valid Microsoft Authenticode verification. Keep all files private.
   Use only the owned disposable guest/prefix; no host Edge install, user profile,
   existing VM or system defaults changed. Retain installation logs. An installer
   or token/security failure is a separate reported blocker, not license to bypass
   the sandbox or import another worker's unreviewed code.
2. Build an ORIGINAL offline HTML fixture with CSS marker/background and two
   real visible WebGL1/2 canvases. Check exact default and FBO pixels, FBO complete,
   GL errors, loss state and actual renderer string. Publish nonce-bound structured
   readiness/observed viewport/canvas geometry through ordinary browser facilities
   (for example page title plus Chromium console log). No extensions, renderer Node,
   unsupported embedded-WebView2-as-browser mode, or proprietary app modification.
3. Launch the actual Edge browser headfully. An app window (--app=file URL) may
   be explored as a SCOPED smoke to avoid needing CDP or external networking;
   verify its actual behavior rather than assuming inherited Chromium switches.
   It does not certify tabbed UI. Keep per-run mount/network isolation with no
   outbound network, fresh user-data dir and no account/sign-in/default-browser
   operations. No --no-sandbox/--disable-gpu/security weakening or capability spoof.
4. Reuse/extend original native HWND observation only as needed to identify the
   nonce-bearing owned browser window and actual client/render viewport bounds.
   Derive screenshot pixels from observed geometry/scale, not searching the screen
   for matching colors. Missing/ambiguous window/viewport/ownership fails closed.
   Use independently expected canvas colors, finite/in-viewport geometry and inset
   pixel regions alongside original CSS/frame checks. Readiness is NOT presentation
   proof. Freeze DOM and collect fixed, bounded captures; retain early stale images,
   require final settled images, never retry until a preferred image appears.
5. Require explicit accelerated inner EGL and outer GLES2 VirGL checks, genuine
   browser/GPU/renderer process provenance and clean full-run error/exit evidence.
   A page's WebGL renderer string alone does not prove the whole browser pipeline
   is GPU-composited. State exactly which evidence is observed and which is missing.
   No GPU fault/recovery in this first smoke. Preserve failure evidence and stop.

Implement executable positive/negative fixture/protocol/decoded-pixel tests before
any live browser smoke. Parent independently reviews code/tests and lawful payload
provenance before dispatch. Stage/hash complete dependencies before boot. Reuse
the existing D:-backed guest, unique sockets, corrected budget guards, persistent
controller, 900-second bound/90-second shutdown reserve and exact identity stopper.

One reviewed live attempt first; do not expand into browsing external websites,
accounts, networking, media decoding, full UI automation or broad token fixes.
Record startup failures with a narrow demonstrated owner for the next plan.
Full tabbed Edge, lifecycle/recovery, Chromium/CEF variants, installed Classic/Luna
shell and efficiency remain distinct later cells, not passes from this smoke.
No master merge. Check usage and preserve the standing checkpoint/weekly limits.

## Current authority and immediate acquisition milestone

The human replied to the explicit terms/download/install question: "Yes, accept
and test privately" on 2026-10-22. Edge payload acquisition/private VM testing is
now authorized; no additional acceptance is inferred for unrelated agreements.
The prerequisite regression cells are complete (9cb47a12, 9d8e6935). Parent
independently inspected x64/i686/WebView2 images and all 44 archive hashes;
i686 EXE machine is actually 0x014c. Corrected guards and both final shutdowns
passed. Preserve x64's original procedure failure and the guard repair history.

Next dispatch is acquisition/provenance ONLY, no VM boot/install/browser launch:
resolve current Stable Windows x64 MSI from Microsoft's first-party
https://edgeupdates.microsoft.com/api/products?view=enterprise metadata, preserving
the exact selected product/release/artifact record and timestamp. Validate the
official HTTPS artifact URL/final host, expected hash algorithm and payload digest;
verify valid Microsoft Authenticode on the downloaded MSI using the host's
read-only signature verification. Preserve signer/chain/status and exact version.
Missing/invalid provenance, hash or signature stops this milestone without execution.
Use a new private D:-resident directory, no vendor file or artwork in Git.
Record the user's authorization and official terms URL privately/as concise notes.
Commit/push a concise original provenance summary, return for parent verification
before fixture implementation/guest installation. Never run host msiexec/setup.

Future coverage note: current WebGL controls clear/read pixels but do not compile
and draw a textured shader workload. Record that gap; add real shader/texture draw
coverage in a later bounded graphics step rather than calling clear-only checks
complete WebGL application compatibility. Full tabbed-browser operation is likewise
distinct from the proposed app-window smoke.
