<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent build accepted; isolate optional fresh-prefix setup before unit matrix

Parent reviewed Luna8809827b on codex/g4-appcontainer-independent-validation.
All45 retained seal entries match; build/provenance/actual loader-tail/cleanup
records examined. Exact cb1d117 Wine plus committed root4aabab76 source, normal
project assembly/autoconf/configure and FULL make exit0 are accepted independent
reproducible-build evidence. No dirty worker DLLs/logs were substituted. Assembly
must keep stock backup and Wine on the same filesystem: the initial EXDEV attempt
is preserved, and the successful run used the exact root archive beside Wine.
Generated configure, replaced stub backups/symlinks and wex header overlays are
normal deterministic project assembly, not hidden production edits.

Owned source/build remain /var/tmp/wex-g4-ac-parent-20261026/{wine,project,build}
in D:-backed Debian. No rebuild necessary. Independent unit results are STILL
PENDING: first kernelbase prefix initialization timed out240s, no test summary;
subsequent userenv initialization was interrupted. Do not infer an API failure
or claim the worker49 assertion results as independent. Exact owned teardown
completed and all final process scans were COMPLETE-empty.

Observed startup includes wineboot --init and control appwiz.cpl install_mono.
Pinned source dlls/mscoree/mscoree_main.c DllRegisterServer calls
install_wine_mono; dlls/appwiz.cpl/addons.c uses a modal add-on installer after
cache/default-directory attempts. In a private offline fresh-prefix lab, optional
Mono/Gecko setup can prevent the native API tests from starting.

Authorized bounded r2 unit preparation: new prefixes/evidence, explicit
wineboot --init with ENV-ONLY WINEDLLOVERRIDES="mscoree,mshtml=" solely during
initialization; REMOVE that environment variable for actual kernelbase/userenv
unit execution. No DLL-bit edit, persistent registry override, installed optional
runtime, network access, test weakening or browser/sandbox change. Record exact
phase treatment and normal builtin module evidence, preserve r1 failures, stop
at first new init/test error, always checked owned cleanup. Same retained build
and four test executables. Luna assigned this continuation; no duplicate task.

Candidate module paths for offline R6 planning (not unit-accepted yet):
- build/dlls/kernelbase/{x86_64,i386}-windows/kernelbase.dll
- build/dlls/userenv/{x86_64,i386}-windows/userenv.dll
under the owned root above. Final preparation/live acceptance remains conditional
on actual unit results and parent review. No VM/SSH/browser/R6 run or master merge
occurred. Defensive fault-conformance gaps remain TODO under human priorities.
