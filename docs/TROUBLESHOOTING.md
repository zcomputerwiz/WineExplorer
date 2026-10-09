# Troubleshooting (VM / session)

## 1. New Wine clients hang after `pkill wineserver`
**Symptom:** after killing the wineserver, any `wine ...` started from ssh prints nothing and never returns
(`strace` shows the process in `read()` on an internal wait pipe); `explorer` may be stuck too.
**Cause:** `pkill wineserver` kills the server but leaves its child processes alive (`services.exe`, `rpcss.exe`,
`winedevice.exe`, `explorer.exe`, `wineboot.exe`, ...). A new server and the orphans fight over the same endpoints
and named objects.
**Fix:** `scripts/vm.sh clean` (runs `scripts/vm-clean-wine.sh` on the VM): `wineserver -k`, then kill every process whose
command line contains `windows\system32` and any wineserver. `scripts/vm.sh restart-shell` does that and restarts GDM.
Never use `pkill -f <pattern>` from an ssh one-liner that itself contains the pattern: it kills its own shell
(exit 255); put the commands in a script file on the VM.

## 2. Cold start: the first session after a server restart dies
**Symptom:** GDM autologin shows a black screen (or "Wine configuration is being updated") and then drops back to the
login screen; `wine-session.log` ends at "Starting direct". The second attempt works.
**Cause:** the old session script ran `wine reg query ...` to read `Winlogon\Shell` *before* starting explorer. That
first Wine client starts wineserver, `wineboot --init` (prefix update) and a bare `explorer.exe /desktop` as side
effects, so our `explorer.exe /desktop=Default,WxH` found a desktop manager already running and exited, which ended
the session. Leftover processes from section 1 made it worse.
**Fix:** the session script (acting as Winlogon) now reads the `Shell` value straight out of `$WINEPREFIX/system.reg`
(no Wine process) and its first Wine process is `explorer.exe /desktop=Default,WxH /shell`. `/shell` is a new
explorer switch that turns the Windows-style shell on for that desktop. Verified: clean + restart gives a working
desktop on the first try.
If it ever recurs: `scripts/vm.sh clean`, then `scripts/vm.sh restart-shell`, and read `~/.wine-session.log`.

### 2b. First start after `make install` (prefix update) ends the session
After a fresh install Wine updates the prefix on the next start (`.update-timestamp` older than `ntdll.so`).
Run as a side effect of starting explorer, the update's helper (`wine: failed to start syswow64
undll32.exe: c0000135`,
this build has no 32-bit part) took the session down. The session script now runs `wineboot.exe -u` up front when the
prefix is stale, kills that wineserver, and then starts explorer cleanly. The log is `~/.wine-session.log`
(previous run: `.1`) and only has errors (`WINEDEBUG=-all,err+all`; set `WINEDEBUG` to override).

## 3. A stale VM clock stops rebuilds
The guest clock once ran months ahead (VirtualBox time sync is disabled on purpose). Files created with future
timestamps (`*.res`, objects) are "newer" than their sources, so `make` skips them. Fix: set the clock
(`sudo date -u -s ...`) and `find ~/build ~/src -newermt now -exec touch {} +`. `vm.sh sync` extracts with `tar -m`
so synced sources always get the current time.

## 4. Volume/PipeWire only works with a seat
With GDM stopped there is no logged-in seat and PipeWire falls back to the "Dummy Output" sink; volume calls then
succeed but change nothing visible. Test audio inside the graphical session.

## 5. polkit prompts need the right login session
polkit decides by the *caller's* login session. A Wine process started from an ssh command belongs to the ssh
session (no agent) and is refused; start test programs from the shell (tray, Start menu) so they run in the
graphical session where explorer's agent is registered.

## 6. Editing from Windows
The tool environment strips backslashes in some heredocs and Python writes CRLF. Use the editor tools for strings with
`\n` or `\\`, and run `scripts/fix-eol.sh` before syncing (`vm.sh sync` does it).

## 7. Cursor stays as an I-beam or resize arrow in VirtualBox

Investigation on 2026-10-09: the Windows host and Guest Additions both run VirtualBox
7.2.20; the guest uses VMSVGA with 3D enabled, Xorg's `modesetting` driver and
`vmwgfx` (kernel 6.12.111). Wine is running directly on X11, without Gamescope.
The existing guest config forces `modesetting`, but still uses hardware cursors.

Wine's `+cursor` trace showed `NtUserSetCursor`, `process_wine_setcursor` and
`XDefineCursor` changing the Notepad cursor on hover. An independent XFixes probe
confirmed distinct I-beam, arrow and resize images, including restoring the arrow
over the taskbar and showing its vertical sizing cursor. This points to the
VirtualBox hardware cursor display path; it does not establish which component
in that path is responsible. No Wine cursor code change is justified by these results.

Workaround: from Git Bash run `scripts/vm.sh cursor software`, then restart the
graphical session once all work is saved. This sets `Option "SWcursor" "true"` in
`/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf`. Xorg draws the cursor in
the guest framebuffer instead of using the hardware cursor path. Mouse integration
remains enabled; cursor movement may look less smooth. The helper saves the original
config as `20-wineexplorer-modesetting.conf.before-cursor-fix` on its first use.
To return to hardware rendering, run `scripts/vm.sh cursor hardware` and restart
the graphical session again. These settings apply to Xorg, not Wayland sessions.

Diagnostic: copy `scripts/cursor-probe.c` to the guest and build with
`gcc cursor-probe.c -o cursor-probe -lX11 -lXfixes`. Run it under the session's
`DISPLAY` and `XAUTHORITY` after each hover. It prints the cursor image hash,
serial and hotspot, independent of VirtualBox's host cursor display. Compare
hashes for edit area -> title bar -> border -> desktop/taskbar -> Explorer border;
the arrow hash should return without closing Notepad.

Applied and checked on the VM: Xorg logged `Option "SWcursor" "true"` after the
approved graphical-session restart. VirtualBox framebuffer screenshots then
showed the I-beam, arrow and resize cursor switching correctly without closing
Notepad. This is a VM graphics workaround; Wine's cursor handling was unchanged.


## VM graphics integration (2026-10-09)

WineExplorer renders through Wine, X11, the Linux graphics driver, and the
hypervisor's virtual display. Install integration tools in the Linux guest.
Windows guest display drivers cannot drive the Linux kernel through Wine.

- VirtualBox: Linux Guest Additions, VMSVGA, `vmwgfx` and Mesa. In 7.2,
  `VBoxDRMClient` and its desktop helper handle display resizing.
- VMware: the distribution's `open-vm-tools` and `open-vm-tools-desktop`, plus
  the kernel's `vmwgfx` and Mesa. Desktop tools provide resizing and clipboard
  integration; graphics drivers are supplied by Linux.
- Hyper-V: the distribution's built-in Linux Integration Services and its
  virtual display driver. An xrdp / Enhanced Session setup is a separate display
  transport to evaluate; it is not automatically a graphics acceleration upgrade.

Current VirtualBox audit: `vboxguest` and `vmwgfx` are loaded; VBoxService,
VBoxDRMClient, and clipboard/session clients are running. Xorg reports
`glamor X acceleration enabled on SVGA3D`, DRI3, and `SWcursor true` together.
The cursor workaround therefore leaves accelerated X drawing enabled. This
confirms an accelerated driver path, not a measured performance improvement.

The cursor fault is localized to the hardware cursor presentation path: guest
XFixes images changed correctly while the visible cursor remained stale.
A missed update or stale cursor cache is plausible, but the precise fault inside
the driver/hypervisor has not been identified. Keep the software cursor fallback
for this VirtualBox configuration. Re-test hardware cursors after graphics stack
updates, and test each hypervisor independently rather than enabling this
workaround on all guests.

Useful next measurements are resize/repaint latency, CPU use during window
movement, and application rendering performance. Profile invalidation and
painting in WineExplorer before assuming additional VM tools will speed up GDI.

References: [Oracle Guest Additions](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/guestadditions.html),
[VMware open-vm-tools](https://github.com/vmware/open-vm-tools), and
[Microsoft Linux Integration Services](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/supported-linux-and-freebsd-virtual-machines-for-hyper-v-on-windows).
