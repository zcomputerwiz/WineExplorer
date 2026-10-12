# Handoff to Codex — 2026-10-12

Read `CLAUDE.md` first (rules: XP parity, no Microsoft material in this repo, small stock-Wine changes listed in
`docs/STOCK_CHANGES.md`, D-Bus over spawning tools, commit after each working step, GPL headers, never pass C escapes
through shell heredocs/sed/`python -c` — write a script file or use an editor; the last rule bit twice today).

## Where things are

| Item | State |
|---|---|
| `master` (this checkout) | Pushed to origin at `2113eb3` + this doc. Contains W1–W3 (LightDM, greeter protocol, logonui Welcome screen), D1 compositor, D2 effects (merged `603a8ab`, tested), `scripts/vmtest.py`. |
| `installer-port` branch | Worktree `../WineExplorer-inst`. **Not pushed, not merged.** Gemini's installer (taken as files, not their history — their history contains Microsoft material and must never be pushed) plus all fixes below. Master was merged into it (`f4e53e2`, conflict in wexhost call tables resolved by keeping both sets: composition_shadow first, then the installer calls). **That merge has not been compiled yet.** |
| `d2-effects` branch | Merged into master; worktree `../WineExplorer-d1` can be removed. |
| Plan docs | `docs/WINLOGON_AND_DWM.md` (W1–W7, D1–D4, status line at top), `docs/INSTALLER.md` (Gemini's; needs updating, see below). |

## Machines

- **Proton VM** (VirtualBox, SSH `-p 2223 wineuser@127.0.0.1`, key `D:/VirtualBox_VMs/vm_key`): build box.
  `~/inst` = installer-port sources, `~/inst/build` = its Wine build (prefix `/usr/local`, **never `make install`
  there** — `package-wine.sh` stages into the ISO). `~/open` = master build into `~/open/inst` for Xvfb tests
  (`~/open/d2test.sh`). Build targets for one dll: `make dlls/<name>/all` (plain `make dlls/<name>` does nothing).
  ISO: `cd ~/inst && sudo -n env BUILD_DIR=$HOME/inst/build WORK_DIR=/home/wineuser/iso-build OUT_ISO=$HOME/inst/wineexplorer.iso bash installer/build-iso.sh --clean > ~/inst/iso.log 2>&1`
  (~20 min). Sync sources with `git archive --format=tar HEAD <paths> | ssh ... 'tar -xmf - -C ~/inst'`.
  A rebuild with every fix up to `0036319` minus the unattended-safety commit was running at handoff
  (`~/inst/iso.log`); rebuild once more after syncing HEAD (textsetup/main.c + screens.c changed since).
- **Dev VM** (WineExplorer-VM): on LightDM with the logonui greeter, session running as wineuser, Composition=0.
  Holds the genuine overlay — never build the ISO there.
- **XP VM**: reference, running headless.
- **VirtualBox `WEX-Install-Test`**: old install with a TCP serial port (2240); superseded by QEMU below. Can be
  powered off.
- **WSL2 Debian 13** (`wsl -d Debian -u root`): QEMU/KVM (`/dev/kvm` works), os-autoinst, tesseract, imagemagick,
  parted. Use this for install tests.

## Test tooling (new today)

`scripts/vmtest.py` (run inside WSL): `start NAME [--iso PATH]`, `stop`, `eject`, `shot`, `key` (QEMU key names:
`ret f8 tab backspace ctrl-alt-f2`), `type`, `click X Y`, `clicktext "Bob Smith"` (OCR), `wait TEXT`, `text`,
`sh NAME USER PWFILE CMD` (serial console; needs `serial-getty@ttyS0` enabled in the guest). VM state lives in
`/tmp/vmtest/NAME`; to edit an installed disk while the VM is off: `qemu-nbd -c /dev/nbd0 disk.qcow2; mount /dev/nbd0p1`.

`installer/tests/install-interactive.sh ISO PASSWORD_FILE [VM]` (installer-port branch) runs the whole interactive
install: blank disk → partition editor → copy → restart with the CD in → GUI setup → OOBE (Alice with password, Bob
Smith without) → Bob's desktop. **Not yet run end-to-end** — written from the manual run. Test password:
`build/install-test/password.txt` (gitignored; strip CR, it was written on Windows). Example:

```
wsl -d Debian -u root -- bash /mnt/d/DeepThought/Projects/WineExplorer-inst/installer/tests/install-interactive.sh \
    /mnt/d/VirtualBox_VMs/iso/wineexplorer.iso /mnt/d/DeepThought/Projects/WineExplorer/build/install-test/password.txt
```

Calling WSL from PowerShell: put multi-step commands in a script file (quoting through PowerShell breaks `$`); and
don't `pkill -f` a pattern contained in your own command line.

## Installer: fixed today (installer-port)

Verified in a VM unless marked: blank disks listed; partition editor; license wrap/reflow; GRUB "Continue Setup
from the hard disk" when a disk has Setup in progress (*not yet verified*); CD eject without the live-medium prompt
via `/etc/live/tools.conf` (*not yet verified*; `noprompt` does nothing); template prefix built by
`wex-gui-setup.sh` (setup.exe used `system()`, which runs cmd.exe inside Wine — all `system()` calls in setup/oobe
removed); first logon renames the template's `C:\users\wexsetup` paths to the user and runs `regsvr32 shell32.dll`
(`scripts/wine-explorer-session.sh`; desktop 6 s); full names kept; passwordless accounts get password mode none
(not locked); comctl32 wizard focus fix (traced: focus lands on Finish; *Enter→Finish not yet verified in an
install*); OOBE title NUL glyph; "Not right now" mnemonic; unattended: missing `TargetDisk` stops Setup instead of
erasing disk 0, no answer file → interactive (*not yet verified*).

## Next steps, in order

1. Sync installer-port HEAD to `~/inst`, full `make` (checks the merge), rebuild ISO, copy to
   `D:/VirtualBox_VMs/iso/wineexplorer.iso`, run `install-interactive.sh`. Fix what fails.
2. Unattended: build with `--unattended` (uses `installer/setupinfs/unattend.sif` or the example), boot the
   "Install (unattended)" entry in QEMU, verify it completes with no input; also verify a wrong `TargetDisk` stops.
3. Update `docs/INSTALLER.md` (build deps: libfdisk-dev libblkid-dev libncurses-dev; test procedure; what changed),
   then merge installer-port into master and push (the user has approved pushing master).
4. Known gaps, not started: installed system still uses GDM (switching it to LightDM + our greeter is W7);
   Automatic Updates choice is recorded but never applied; time zones shown as Linux names; GUI setup pages lack
   the XP wizard header/backdrop; format screen shows `/dev/vda1` instead of "Partition1 … Disk 0"; Recycle Bin
   icon blank in new profiles; GRUB on the installed system shows the Debian theme.
5. Later plan phases: W4–W7 and D3–D4 in `docs/WINLOGON_AND_DWM.md`. D2 untested items listed there.

## Other loose ends

- `.claude/worktrees/agent-ab8e75e225a87aeeb` leftover dir; orphan VirtualBox diff image `{21d17fdd...}` (in use,
  could not be closed); `.gitignore` has an uncommitted `*.tmp` line that is not ours.
- Dev VM has a test account `wextest` (password in gitignored `build/w2/test-account.txt`).
- An architecture question from the user was answered in chat: most of the code could ship as an add-on package
  for stock Wine; the core changes (wineserver, win32u, winex11, ntdll) need upstreaming or a wine-staging-style
  patch series. Offered, not started: generate that patch series from `STOCK_CHANGES.md` and mark upstreamable fixes.
