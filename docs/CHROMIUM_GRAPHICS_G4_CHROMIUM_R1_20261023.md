<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium r1 — one failed normal baseline, preserved and stopped

Executed ONE reviewed CHROMIUM_PREP normal HTTP baseline, no retry/alternate/
additional Edge arm, debugger, workaround, assertion change or production patch.
Preparation history remains immutable; actual run status is in postshutdown-audit.
**passed=false; no readiness, API/readback/front-pixel or G4 acceptance.**

## Exact execution/provenance

Private root `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-headful-r1-20261023/`.
Archive `chromium-headful-evidence.tar.gz`, SHA256
`6add3fb82b90d7f936c10789596111151637a0e0de9d72ce8f289ae79679ba66`;
all37 regular members hash-match extracted `chromium-headful-evidence/` and
execution.json. Parent independently confirmed those37/maps/archive and image.
Full root/adjacent host evidence inventory in evidence-hashes.json; no private
payload/profile in Git. Source code unchanged from reviewed ba5eb615; HEAD at
dispatch18d398c1. No unchanged unit tests rerun for this live-only report.

Manifest a89d0103.../source tar60e3ff9c... unchanged; exact commands are in
CHROMIUM_PREP and copied run/preflight.json: persistent C: PowerShell controller
`-RunName chromium-headful-r1-20261023`, existing private procedure with
`WEX_G4_CONTROL=chromium-headful`, separate exact-identity SIGINT stopper.
Host inventory/cache ZIP+258 tree hashes/seeds matched, public credential matched
seed/mode600, future run absent, no launcher/QEMU/open-chain owners. Only accepted
80GiB top layer used; launcher d96f6854... and procedure66a1cdb9... unchanged.
Host free1,859,150,630,912 bytes; live usage3%five-hour/1%weekly, no credits.

Guest free16,947,441,664 bytes BEFORE transfer/create (>4,431,218,372).
All19 actual accepted environment binaries,15 deployed inputs (13 sources + ZIP/
inventory) and258 safely extracted runtime hashes matched BEFORE consumer.
Fresh stage `/home/debian/wex-g4/chromium-headful-r1-20261023-stage/`, portable
runtime `vendor/extracted/chrome-win/`, profile in fresh run `/profile`.
Retained installed Edge prefix shared, not pristine; Wineboot-u0, pinned installed
Edge hash/version156.0.4314.42 persisted. Chromium258 files verified before/after
Wineboot; after-collection check was NOT reached. Normal argv unchanged: pinned
Wine + chrome.exe, original nonce loopback HTTP app, fresh profile, no-first-run/
no-default-browser-check/logging/v1 only. No diagnostic/attachment/security flags.

RawUTC650/435 gates passed:862.062952/848.077758 seconds remaining.
Private filesystem X sockets; privateLO namespace4026532238 differed from parent
4026531840 and was identical before/after, only lo UP/loopback routes. Actual
inner EGL and outer GLES2 compositor both virgl(D3D12 Intel UHD750), pass; host
D3D12 verified. These backend checks do not establish browser rendering.

## Actual failure (not a white-window result)

Consumer SSH1; procedure0/archive0 mean orchestration/evidence completion, NOT
product success. Runner44.955075s, error
`RuntimeError('Browser launcher exited before collection completed')`.
No nonce readiness, no accepted native/client geometry, zero fixed front samples,
no page WebGL results. HTTP server on127.0.0.1:43593 recorded ZERO requests/errors,
then stopped/joined; original637HTML/3750JS retained/hash-verified. Nonce
450fd83c41cf86eb88030de461c68651. Seven native inventories saw no visible window.
Hidden Chromium windows/full Z: image appear in raw logs; not readiness proof.

Actual failure-front.png is1280x720 black output plus pointer (viewed), SHA256
`ac1026b75415f141637184f89ed2b790f8447a7d5059207b302e8fbe4425aa59`.
Captured once before cleanup, capture0; failure-only diagnostic, NOT pixel pass.
Linux Popen1025/start8229 sampled; GPU role1144/start8353 sampled, no renderer role
sampled. Sampling is not proof no short-lived renderer existed. Ready-time Linux
before/after owner acceptance was never reached; no Windows/Linux ID mapping.

The alive check observed `browser.poll()!=None` BEFORE failure capture/cleanup,
but did not record its numeric status there. `browser_exit_after_cleanup=127` is
the later wait result after prefix cleanup, not an independently sampled native
32-bit Windows code. Do NOT label127 as a Chromium enum/missing API/sandbox cause.
Full Wine/browser/chromium/process/native/compositor/capture logs retained.
Classified fatal/GPU/renderer/graphics log counts0, quota62,113/134,217,728 bytes;
that neither explains exit nor makes this a healthy collection.

## Cleanup and bounded read-only source followup

Prefix cleanup kill0/wait0; no remaining servers/prefix processes. Separate
identity-validated launcher435266/start5579071 SIGINT -> QMP graceful true,
QEMU435281 exit0 and Xvfb435268 stopped. Windows WSL controller38928 started
20:24:34.5499298Z, exited0 at20:26:20.9711133Z; persistent host session completed0.
Stopper0 and explicit QEMU0/Windows0/final identity checks pass; all three recorded
Linux PIDs and Windows PID absent, no chain owners. No Ctrl-C/timeout/second boot.
postshutdown-audit.json independently records these gates and all archive hashes.

At parent's request only, read-only HOST source path confirmed:
`/var/tmp/wex-g3-repair-20261018/clean-source/wine`, HEADaf80a466... (NOT4315).
Exact pinned commit4315be83d75644f13500381dcec94dcdc5fa904e is available in
`D:/DeepThought/Projects/WineExplorer/wine` Git objects, not the owned WSL Git.
Its dlls/ntdll/unix/thread.c SHA256
`1ee311dd7f150868c179e8a3b472ba6e7e9ccd31d2d70965b6f6630fa916b99f`
matches that owned host file. get_unix_exit_code:86 returns1 for nonzero status
with low byte0, otherwise returns status; abort_process:1508 and exit_process:1541
use it for Unix termination. This supports loss of full native-status information,
NOT a unique interpretation of127 or this exit path's cause. Exact excerpt/paths/
hashes in exit-source-evidence.json; no guest query or new probe implemented.

Stop for parent review/selection of any future observation. No cause, graphics,
effective sandbox or fullG4 claim. C: report only; D: master/other workers and
pre-existing unstaged `M wine` untouched. All profiles/runtime/evidence preserved.
