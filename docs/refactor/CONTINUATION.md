# Refactor continuation — 2026-10-12

P0 acceptance passed; source is still in its original layout. P1 uses a separate
build checkout/prefix on the Proton build VM. Existing installed runtimes,
installer images and desktop sessions remain intact.
User's `.gitignore` edit is preserved. A pre-existing `.claude/` worktree is
untouched; `/.claude/` was added only to local `.git/info/exclude` after the app
started thousands of untracked-file diff subprocesses for it. That exclusion
is not a project policy change and is not committed.

Read `REFACTOR_MAP.md` and `PATHS.md` for current audit state. The audited source
is pinned to `52842ca4`; later documentation/tool commits do not change its Wine
tree. Keep that pin until actual Wine source changes are intentionally audited.
The bare evidence repository is `build/refactor/wine.git`. The real release tag
came unchanged from wine-mirror because the user's fork has no tags.

Current map: 750 paths/1,876 hunks, zero unclassified paths. Ninety-seven stock
paths have explicit hash-pinned, per-hunk decisions in `REVIEWS.json`; eleven include U hunks
containing twelve independent fix candidates. P0's strict acceptance command passes.
P1 has five validated/pushed branches: imagelist-alpha (985db3ef),
empty-recycle-bin-icon (00d5a25f), wizard-focus (94a5845c), and
drawtext-hideprefix (72feeed7), and folder-iconfile (d942c4f4).
Rename notification validation is running.
See UPSTREAM.md for statuses, descriptions and exact validation scope.

The import's upstream snapshot is `eba89375` (72 commits after wine-11.19).
The 68 import differences contain only modes, missing ntoskrnl files, open art
and CRLF normalization. The search helper is local
`build/refactor/find-import.py`; its bounded candidate search completed.
`build/refactor/check-coverage.py` performs six negative checks, all passing.
`build/refactor/record-reviews.py` created the first reviewed stock decisions;
do not rerun it after adding more reviews, because it writes only those eleven.
`build/refactor/review-graphics.py` added the graphics, theming, shell-hook,
work-area and endpoint volume decisions. It reads the pinned evidence and
preserves existing review entries. Build VM connectivity was checked: Xvfb
exists and the 60 GiB root filesystem has 19 GiB available. No guest was written.
`build/refactor/review-shell.py` added shell32, Explorer boundaries, build inputs
and backport decisions. Local generators are scratch tooling; the committed
REVIEWS.json is authoritative. The fork master still resolves to 081042ede6.

P1 runtime: `~/wex-refactor/upstream` is a shallow fork clone, and
`~/wex-refactor/build` is a full clean 64-bit build (completed). No installation
was performed. `base-build.log` and `base-build.status` record completion.
The active runner is `test-topic.sh rename-source-notification shell32 shlfolder`,
PID 1315670 at launch, with `rename-topic.log` and
`results/rename-source-notification/` evidence.
Do not run another topic while this runner holds test-runner.lock.
Topic results/prefixes are isolated under ~/wex-refactor. Old failed imagelist
runner 1281780 was stopped and its patches reversed exactly before rebuilding
master and rerunning; the successful imagelist run then confirmed six intended
failures before the fix and zero after. All current topic source is open Wine
code, with existing notices preserved.

Host sparse worktrees are `build/refactor/upstream` (validated imagelist),
`build/refactor/recyclebin` (validated resource fix), and
`build/refactor/wizard` and `build/refactor/drawtext` (validated), and
`build/refactor/folder-icon` (validated), and `build/refactor/rename` (draft).
Fetch VM commits through SSH into
refs/remotes/build/upstream/<topic>, verify the host worktree matches, then
update only that worktree's index/HEAD and push its approved upstream branch.
No WineHQ merge request was created. Git identity follows the user's configured
handle; real-name authorship and sign-off remain the user's manual step.

### P1 tooling failures and corrections

* `git --git-dir=build/refactor/wine.git diff master refs/audit/wineexplorer -- dlls/shell32/changenotify.c`
  produced a misleading deletion because the project ref stores that file under
  wine/. Discarded that output; compared the fork blob with the actual project
  file and used the reviewed hunk instead. No source was edited from that diff.

* Wizard baseline produced the eight expected failures, but the runner wrote
  EXPECTED_REGRESSION_NOT_FOUND: the Windows-written failure-pattern had CRLF,
  and command substitution retained CR. Exporter now writes LF bytes; the
  runner strips CR/LF before matching. Reversed the exact test-only patch,
  rebuilt master, and reran: eight failures before, zero after.
* `ssh -vv -i D:/VirtualBox_VMs/vm_key -p 2223 -o BatchMode=yes -o ConnectTimeout=5 wineuser@127.0.0.1 true`
  stalled after offering the public key. Adding `-o IPQoS=none` restored access;
  exact cause is undetermined. No guest reboot was performed. Use that option
  with SSH/scp. The old stalled connections are closed.
* `VBoxManage debugvm ProtonExplorer-VM info os-dmesg` failed VERR_FILE_NOT_FOUND;
  `VBoxManage debugvm ProtonExplorer-VM detectos` was an invalid subcommand;
  `osdetect` worked. `osdmesg --lines=24` failed VERR_NOT_FOUND. Temporary metrics
  were disabled afterward. The ignored console screenshot is not project art.
* `rg -l -i 'guestcontrol|passwordfile|credential' docs scripts/vm.sh fe.sh`
  failed because fe.sh does not exist. No guest credentials were changed.
* `bash -n script && nohup ... & echo $!` backgrounds the entire AND list,
  retaining the SSH channel until completion. Run syntax checking and launch
  separately. The wizard connection closed after successful completion.
* The first read command this continuation used Bash brace expansion
  `build/refactor/drawtext/dlls/user32/{text.c,tests/text.c}` in PowerShell:
  ParserError, Missing argument in parameter list. Repeated with explicit paths.
* DrawText launch printed literal `$!` because the PowerShell command included
  a Bash backslash before the escaped dollar. The runner launch is independent
  of PID printing; inspect its status/lock rather than launch a duplicate.

* `rg -n 'static void test.*alpha|static void test.*draw|create_test_bitmap|check.*pixel|START_TEST|test_ImageList' build/refactor/upstream/dlls/comctl32/tests/imagelist.c`
  failed with "The system cannot find the path specified" immediately after
  worktree creation with --no-checkout. Sparse patterns did not populate the
  empty index. `git -C build/refactor/upstream read-tree -mu HEAD` populated it.
* `git --git-dir=build/refactor/wine.git show master:dlls/shell32/tests/icon.c`
  failed: `fatal: path 'dlls/shell32/tests/icon.c' does not exist in 'master'`.
  Listed the test tree and searched it; stock-icon tests are in shelllink.c.
* The first attempt to write test-topic.sh used a JavaScript String.raw template
  containing `root="${WEX_REFACTOR_ROOT:-$HOME/wex-refactor}"`. It failed before
  any tool executed with `SyntaxError: Missing } in template expression`.
  Used an ordinary quoted JavaScript string for the patch instead.
* Initial imagelist baseline command failed with
  `wine: chdir to /home/wineuser/wex-refactor/prefixes/imagelist-alpha : No such file or directory`.
  Added the prefix-parent mkdir and explicit expected-assertion matching.
  The guard command
  `test "$(cat ~/wex-refactor/results/imagelist-alpha/status)" = WAITING_FOR_BASE_BUILD && kill 1281780`
  returned 1 because the runner had already advanced. A second launch rejected
  the dirty checkout. Stopped the original runner, killed only its Wine prefix's
  server, reversed the exact input patches, rebuilt master, and reran successfully.
  Added a checkout/build flock to prevent overlapping runners/status writes.
* `python build/refactor/export-topics.py` failed with
  `ValueError: Empty patch: dlls/comctl32/imagelist.c` after that topic had been
  committed. The following
  `scp -i D:/VirtualBox_VMs/vm_key -P 2223 -r build/refactor/topics/wizard-focus wineuser@127.0.0.1:wex-refactor/topics/`
  failed because the wizard directory had not been created. Exporter now diffs
  against fork master (including committed topic changes) and accepts a topic
  argument. `python build/refactor/export-topics.py wizard-focus` and scp passed.

## Commands that failed or were interrupted

1. In PowerShell:

   ```text
   git --git-dir=build/refactor/wine.git rev-parse wine-11.19^{}
   ```

   The unquoted `{}` was parsed by PowerShell, producing the spurious
   `-encodedCommand` argument and `fatal: ambiguous argument '': unknown revision
   or path not in the working tree.` The following diff in that shell still ran;
   its exit code concealed the earlier failure. The audit now passes
   `wine-11.19^{commit}` as a Python subprocess argument, obtaining the correct
   release commit `455e3509b98a6919fd4ad1def4803e08c41c03b2`.

2. Interrupted this long history search with Ctrl+C (exit 1):

   ```text
   git --git-dir=build/refactor/wine.git log -5 --format='%h %cs %s' --find-object=3629c0f846 -- dlls/bcrypt/bcrypt_main.c
   ```

   It found the October 5 bcrypt commit, then continued searching the entire
   history for four more matches. Use bounded post-release raw history instead.

3. Interrupted the first provenance-enabled audit invocation with Ctrl+C
   (exit 1):

   ```text
   python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline
   ```

   Its internal `git rev-list --objects master ^wine-11.19` caused repeated
   lazy blob fetches in the partial clone. Replaced it with `git log --raw
   --no-abbrev --no-renames master ^wine-11.19`, which reads change metadata.
   The corrected audit and exact-equality verification pass.

The first tag fetch omitted `--no-tags` and auto-followed historical Wine tags.
It succeeded; only the local ignored bare clone was affected. Reproduction
commands now use `--no-tags` to fetch only the requested release tag.
