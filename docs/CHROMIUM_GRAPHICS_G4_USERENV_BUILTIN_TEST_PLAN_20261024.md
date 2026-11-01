<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Verify the built-in userenv loading route before VM deployment

Parent accepts e096ae4 source/contract and isolated native-unit proof after full
diff review,87 sealed files, both actual260-assertion/zero-failure/zero-skip logs,
new builtin artifact hashes and independent actual-function fault test0.267s.
One existing GetUserProfileDirectory todo is unrelated. The unit DLLs were
native-marked links; the builtin-marked production links have not loaded yet.
This remaining functional distinction needs a small actual loading check.

Luna may run ONLY this isolated WSL API validation; no VM/browser/deployment or
code/PE rebuild. Use fresh private prefixes/output logs on D: or D:-backed Debian.

1. In ONE private mount namespace, make all mount propagation private. Bind the
   whole old owned source/build tree read-only, then bind only the two reviewed
   builtin userenv.dll files over its corresponding x86_64-windows/i386-windows
   paths in THAT namespace. Remount those file binds read-only. Physical old
   files and outside namespace stay unchanged; no copying into the old build.
   Record namespace source/target paths and actual inside/outside hashes.
2. Copy ONLY existing actual test EXEs into fresh private arch directories WITHOUT
   any adjacent userenv.dll. Do not accidentally repeat application-directory
   native-unit loading. Omit userenv native/builtin overrides entirely; ordinary
   default Wine loader and prefix setup must select the new builtin-marked module.
3. Run the existing full userenv suite for x64 and actual i686 under combined-WoW64
   using fresh owned prefixes, bounded startup/test/reap/cleanup. Capture loader
   trace, module filename and actual API assertions. Confirm builtin selection
   and correct tested file; no skips of the new group. No network outside private
   loopback, no shared display sockets, no attaching/changing other processes.
4. Stop only owned prefix servers, retain all logs/failures, record empty prefix
   owners and confirm physical old source/build modules unchanged outside the
   namespace. Do not repair/retry if normal route fails; preserve exact outcome.
   Historical unrelated metadata differences remain warnings under user policy.
5. Commit AND push report then STOP parent review. If this passes, next step is
   normal-module deployment into the powered-off/idle owned D: VM build with
   preserved old DLLs and one fresh browser control; that is NOT authorized here.
   No new layer/calibration/rootgitlink/master merge or other-worker changes.
