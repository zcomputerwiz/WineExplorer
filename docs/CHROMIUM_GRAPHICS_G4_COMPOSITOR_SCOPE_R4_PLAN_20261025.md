<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Correct compositor ownership scope, then prepare a fresh R4 control

Parent independently reviewed R3 ae8890a1:47 sealed files/18 archive members,
actual API45/45 JSON, strict shutdown. Remaining process937/start3373/labwc is
exactly the owned compositor. No browser/HTTP was launched. This is a harness
scope error, not failed SID derivation. Preserve all consumed R3 paths/evidence.

Luna implements this bounded OFFLINE change plus fresh preparation:

1. Add an explicit opt-in to OwnedWexcomp, default preserving old metrics behavior,
   that removes WINEPREFIX from ONLY its local native compositor Popen env.
   Keep the caller's Wine environment/prefix unchanged and retain the existing
   shared Wayland endpoint updates. Enable it ONLY in the new R4 control. Leave
   graphics_resources.sample and genuine Wine/server leak assertions intact;
   no arbitrary PID exclusions or lower idle assertion.
2. Meaningful regression: run actual small native Linux children with both child
   env modes and a genuine marked owner; show sampler sees the marked owner and
   legacy marked compositor, excludes only the unmarked child by its environment.
   Test caller environment/endpoint preservation, default compatibility, process
   cleanup and actual control opt-in. No VM/compositor/render probe needed here.
3. Fixed HOST/output/profile chromium-userenv-r4-20261025, stage -stage suffix.
   Reuse existing SID builtin DLLs, API probe and runtime; no SID rebuild,
   calibration or new disk/layer. Compile only original parent output macro for
   fresh R4. Extend fixed mode/view-wrapper dispatch narrowly; keep R3 defaults
   and old artifacts. Same private readonly view, API probe/intermediate cleanup,
   normal browser eight flags, backend/networking and native observation.
4. Fresh small adapter/PREPARED_NOT_RUN inventory with exact current transfer,
   modules/probe/helper/mode/commands. Use current diagnostic metadata/hashes,
   not historical refusal gates. Retain path/owner/no-overwrite/transfer/space/
   readiness/private namespaces and285/55/82/300SSH/45archive/90090/80 bounds.
   Focused changed tests and existing relevant contracts; no broad unrelated suite.
5. Commit AND push code/tests/report/preparation; STOP parent review before ONE
   live browser diagnostic. No VM/SSH/mount/browser run or persistent deployment
   in this step. Parent's next local work is reviewing existing evidence and the
   new dispatch contract; do not duplicate the implementation.

Only C: graphics source root, private D: data/D:-backed Debian. Existing dirty
source/build read-only, rootMwine/mainDmaster/other agents preserved. Wine SID
source e096ae4 and its unit/builtin/guest API proof remain accepted; no root
gitlink update yet. No security bypass/feature spoof/vendor publication/master
merge. Live usage at wake1% five-hour/0% weekly;95%/99% stopping policy remains.
