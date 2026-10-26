<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent registration validation: clean build passes, unit startup blocked

**Independent build PASS; required unit matrix NOT COMPLETED.** No independent
49/260 assertion pass, failure or skip count can be claimed: no WinTest summary
was produced. This report does not adopt the worker's passing logs as evidence.

Validation branch `codex/g4-appcontainer-independent-validation` starts at parent
`4aabab76cc73387be1229340479ace7d5e3ccb75`. Reviewed worker reference7d258606 and
published Wine `cb1d117d02fef13af18772104b942c4aded50d00`; no implementation edits.
Parent C/main D master/submodules/compositor/other worker checkouts/builds and
old `/var/tmp/wex-g3-repair-20261018` were not modified or used as runtime inputs.

Own fresh native Debian/D-backed tree:
`/var/tmp/wex-g4-ac-parent-20261026/{wine,project,build}`. Before creation,
Debian free670,970,527,744 bytes; D: free1,856,052,002,816 bytes. Standalone Wine
Git clone uses `--no-hardlinks --no-checkout` from the common Wine object store,
then detached checkout of the exact published commit. No borrowed DLLs or logs.

The first unmodified root assembler invocation failed with **EXDEV**, moving
stock desk.cpl from native Linux to the D: checkout's assembly-stock backup.
Its logs and pristine Wine status are retained. Deterministic own-lab remedy:
extract `git archive 4aabab76` beside Wine and run the SAME committed assembler
there. No source/configure/header patch. Assembly/autoconf0, configure0, full
build0. Normal assembly supplies nine committed src/include headers, project
modules and declared stub replacements. Missing public wex headers in a bare
Wine archive are project-overlay preparation, not unknown private prerequisites.
API/test/header-list functional diff against cb1d117d remains empty.

Exact preparation/build operations (private scripts retain full argv/timing):

```text
git clone --no-hardlinks --no-checkout /mnt/d/DeepThought/Projects/WineExplorer/.git/modules/wine /var/tmp/wex-g4-ac-parent-20261026/wine
git -C /var/tmp/wex-g4-ac-parent-20261026/wine checkout --detach cb1d117d02fef13af18772104b942c4aded50d00
bash /var/tmp/wex-g4-ac-parent-20261026/project/scripts/assemble.sh /var/tmp/wex-g4-ac-parent-20261026/wine
# cwd: /var/tmp/wex-g4-ac-parent-20261026/build
../wine/configure --enable-archs=i386,x86_64 --prefix=/var/tmp/wex-g4-ac-parent-20261026/install
make -j16 all dlls/kernelbase/tests/x86_64-windows/kernelbase_test.exe dlls/kernelbase/tests/i386-windows/kernelbase_test.exe dlls/userenv/tests/x86_64-windows/userenv_test.exe dlls/userenv/tests/i386-windows/userenv_test.exe
```

Configure45.235s; make1642.425s, bounded3600s, exit0. Compiler/parser warnings
retained, not a warning-clean claim. All four PE test executables built; AMD64
0x8664/i3860x014c verified and hashed. No `make install` or module replacement.
Toolchain/config.log/config.status/source and overlay provenance are retained.

One original matrix dispatch, no test rerun:

```text
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/run-isolated-graphics.py --socket-root /var/tmp/wex-g4-ac-parent-20261026/sockets --private-loopback -- python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/validate_g4_appcontainer_independent.py --run
```

| Cell | Actual result |
| --- | --- |
| x64 kernelbase security |240s deadline; no WinTest summary/assertion counts |
| x64 userenv |Next fresh-prefix startup began automatically; interrupted/reaped58.198s later when stopping at first blocker; no summary |
| i686 kernelbase security |Not run |
| i686 userenv |Not run |

**First actual runtime error:** `TimeoutError('Owned unit240s deadline')`.
Read-only owned-prefix inventory retained eight processes including
`wineboot.exe --init` and `control.exe appwiz.cpl install_mono`. Test stdout was
empty; loader log stopped during that initialization path. This identifies an
observed startup boundary, not a proven stack-level cause or registration API
failure. No dialog was dismissed, runtime downloaded/installed by an operator,
loader override added, API assertion weakened, or production repair attempted.
The private network had only loopback; no outbound download route.

The normal build frontend was used, explicit own WINESERVER, no WINEDLLOVERRIDES
or WINEDLLPATH. Builtin loader traces show kernelbase/userenv/ntdll during
startup; Linux mapped-file samples tie all three to this owned build. This is
startup module evidence, not proof that the unit bodies executed. Current DLL
hashes are diagnostic identities, not historical acceptance gates:

| DLL | x64 SHA256 | i386 SHA256 |
| --- | --- | --- |
| kernelbase |147fb181ff8ce1eb13aa2bb166e053f40c26191662d1b447247fdfd1e720aa24 |261a2fd0dfa4b04f6b3c16a122f3efbc3a703bb0d078cea8ab552d1476c5f0ed |
| userenv |71de0bea2e3b85a9afcddf7dcdcc40a31b204605530b4d83e0e2cf08e2bf04ba |02f1f5b914b8aa0ba846cf5c04fb616c8aaf6a2563cfeeb5a162b4c151db8313 |

After first timeout, the exact matrix runner PID338/start187552/full argv was
revalidated and sent SIGINT to prevent further cells; raw KeyboardInterrupt and
wrapper254 retained. Each started prefix's explicit own `wineserver -k` and
`-w` returned0, observer reaped, final ownership COMPLETE-empty. First observer
exit1 occurred after scoped teardown, not a unit result. Xvfb PID350 exited0;
runner/Xvfb absent, private namespace/loopback proof before/after equal. No broad
pkill, shared prefix or loaded-binary writes. Prefixes/evidence are preserved.

Private evidence:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-independent-r1-20261026/`:
45-file seal, root archive, preparation/build argv/results/logs, configuration,
provenance, test hashes, both cell logs/JSON, startup inventory, mapped modules,
matrix stop identity, final cleanup and executed helper snapshot. Native build,
source, stock backups and prefixes remain in the D-backed tree above.

No VM/SSH/browser/R6/token merge or new fault/conformance work. Remaining
defensive production fault gaps remain TODO under the parent's high-value
policy. Next bounded decision is how to prepare ordinary fresh-prefix startup
without that optional installer path, then obtain real x64/i686 results using
this retained clean build. No guessed fix applied. STOP for parent review.
