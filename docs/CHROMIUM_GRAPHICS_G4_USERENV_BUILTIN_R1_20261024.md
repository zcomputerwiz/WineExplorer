<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# userenv normal builtin route — isolated test result

Executed ONLY `USERENV_BUILTIN_TEST_PLAN_20261024`, once per architecture.
**BUILTIN_API_UNIT_ROUTE_PASS_NOT_BROWSER_ACCEPTANCE**. No source/PE rebuild,
VM/browser run, image mutation or persistent module deployment. Source remains
Wine e096ae4; previous native-marked unit evidence is preserved separately.

One WSL `unshare --mount --net --propagation private` namespace made all mount
propagation private, bound the entire old `clean-source` tree read-only, and
bound the two reviewed BUILTIN DLLs read-only over normal build module paths.
`findmnt` retained actual source/target/ro flags. Inside targets matched:

| Architecture | New builtin SHA256 |
|---|---|
| x64 |89e93bd0774200acbe97ffda7745c192396bbe7c7710abcc0d2fdce5ea3981f2|
| i686 |3452a717e91f9fd191b3fd9b1ad8404be27c771ed03e10dc0224bc9f5e25e62b|

Copied ONLY existing test EXEs into fresh D: architecture directories. No
adjacent DLLs, WINEDLLOVERRIDES or WINEDLLPATH were used. Fresh ordinary prefix
setup/default loader selected `C:\windows\system32\userenv.dll`; loader traces
explicitly say **builtin** and the test's GetModuleFileName agrees. Actual x64
and actual i686/WoW64 each executed **260 harness assertions/tests,0 failures,
0 skips,1 pre-existing todo**. The new oracle-vector group executed, not skipped.
Both test and both prefix-initialization processes exited0. No route failure,
retry or repair occurred. Unchanged binary test hashes are recorded in results.

Private loopback was the only network interface/route; no outside network.
Display environment was cleared, XDG runtime private, and /tmp/.X11-unix bound
to a fresh empty read-only directory only in that namespace. All test prefixes
are fresh under `/var/tmp/wex-userenv-builtin-r1-20261024` on D:-backed Debian.
Each startup/test cap90s, exact-prefix cleanup kill10s/wait15s; all exits0.
Both prefixes independently scanned COMPLETE-empty afterward. Namespace exit0.

Physical old modules were independently observed OUTSIDE the namespace before,
while it was active, and after exit: byte hashes, identities and timestamps
unchanged. Old x64 SHA590d6f22… and i686 SHA162e1e2d… remained visible outside,
while inside saw the reviewed new bytes. Bindings disappeared with namespace
exit; no copying/installation into the old build occurred. This verifies the
builtin-marked artifacts through the normal loader, beyond native-unit relinks.
It does not prove guest deployment, Chromium startup or AppContainer enforcement.

Private evidence:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-sid-builtin-r1-20261024/`.
`run.py` and results retain exact mount/network/Wine/cleanup argv and bounds;
all loader/assertion stdout/stderr, inside/outside observations, findmnt, audit
and **57-file seal** retained. Original DLL sources remain in the prior private
build directory; no binary/prefix payload committed. Prefixes retained, not
deleted. Report-only change; `git diff --check` passed. No unrelated unit reruns.
M wine/mainDmaster/other workers untouched. Stop for parent review; no VM
deployment/browser control authorized by this result.
