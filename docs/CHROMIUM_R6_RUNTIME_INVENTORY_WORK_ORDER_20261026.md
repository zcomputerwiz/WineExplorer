<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: identify the actual cached-runtime file-set difference

Parent independently reviewed R6 execution26150a40 and shader correctionca446842.
Actual R6 remote stderr proves runtime_state stopped at set(actual)!=set(expected)
before consumer_started. It does NOT identify missing/extra filenames or prove
binary corruption. Raw QMP transcript independently contains both matching ACKs
and post-request guest=true SHUTDOWN/guest-shutdown; QEMU0/no fallback recorded.
Windows wrapper controller0 is recorded; nested WSL session windowsExitCode is
NULL, so do not call that separate exit status proven. This does not require an
unrelated launcher-hardening project. All consumed R6 paths/evidence preserved.

## Priority task: bounded inventory, no browser retry

Use own D: codex/chromium-r6-execution branch (26150a40) or NEW
codex/chromium-runtime-inventory from current parent commit. Preserve all earlier
reports; do not edit Luna's source/build/prefix/private adapter/consumed prep.
Implement original small bounded read-only inventory helper locally and test
against synthetic temporary directories before guest execution. Compare the
EXACT selected guest runtime to the original258-file manifest:

/home/debian/wex-g4/chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win
manifest in consumed stage:
/home/debian/wex-g4/chromium-registration-r6-20261026-stage/runtime-manifest.json

Emit sorted missing/extra/expected-present files, types/symlink status/sizes,
bounded actual hashes for existing expected files and small extra regular files,
and all read errors. Record original manifest provenance. Preserve historical
hash/version differences as WARN per user, not blockers. Missing required payload,
wrong selected path, symlink/foreign ownership or unreadable files remain concrete
findings. Do not guess extras are logs, edit manifest to match blindly, remove
files, change permissions, reinstall/extract/download or modify runtime/prefix.
Do not read raw environment, credentials or arbitrary extra file contents. Use
bounded counts/path strings/read lengths/time and retain partial results/errors.

First inspect existing retained R5/R6 evidence locally; if sufficient actual
missing/extra proof already exists, reuse it and avoid a boot. Otherwise this
order authorizes ONE inventory-only boot of the SAME owned D:-backed R4 disk.
Fresh launcher run name chromium-runtime-inventory-r1-20261026; if consumed STOP,
do not delete/rename around collisions. Exact existing reviewed Windows launcher
with that new name, same explicit D: source mapping,900s/90reserve/80stop. Check
usage before boot;>=95%five-hour or>=99%weekly defer. Verify selected disk/no
other owner, authenticated readiness/space and budget. NO browser, Wine/native
probe, compositor or API launch. One bounded SSH inventory command (<=90s,
<=1MiB retained output) after readiness, private D: logs. It may read the original
runtime and consumed manifest; no guest staging/profile creation required.

Always stop exact owned launcher in finally and await controller. Retain actual
QMP ACKs+post-request guestSHUTDOWN/QEMU0/no fallback/children absence and actual
Windows controller result. Treat unavailable child status as unknown. No broad
pkill, guest repair, new image/overlay or second boot/retry.

## Deliverables and next decision

Original docs/CHROMIUM_RUNTIME_INVENTORY_20261026.md on own branch, helper/tests,
exact commands/results/private evidence and precise set differences. Use actual
findings to recommend the SMALLEST next step: e.g. diagnostic warning for regular
generated extras versus concrete absent payload. Do not implement a speculative
runtime repair or loosen general integrity guards yet. No R7/second Chromium run
or production/token patch authorized by this order. Commit AND push; reply codex
and stop for parent decision. This is the critical path to restoring the browser
comparison, not a license/provenance/full-security audit.

## Separate tiny shader correction

On existing codex/chromium-shader-consumer-validation atca446842, original alias,
tiny-region, renderer-length and NaN reproductions are corrected; parent46tests
and Node35negative/28mutation pass. Fix only the remaining malformed explicit
bounds fallback: when key bounds is PRESENT with [],null,'wrong' (or other wrong
type), return non-success rather than silently defaulting. Existing default
geometry is supported ONLY under an explicit documented absence/default contract;
if empty dict deliberately means default, document and test it unambiguously.
Parent repro: paint the valid LAB_RECTS front using test helpers; pass geometry
{client:[0,0],scale:1,bounds:[]} (also null/'wrong'): currently status pass.
Add focused regression for wrong types/empty/absent semantics and a translated
client success control. Preserve all actual GL workloads and R6 source. Source-only
task, no VM activity for shader. Commit/push updated tests/report separately and
reply codex. Other deep parser/fault hardening can remain TODO unless it can cause
an acceptance false pass in supported caller inputs. Do not expand scope.
