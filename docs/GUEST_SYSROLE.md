<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Provisioning the system role in a QEMU guest (lab recipe)

Why: Wine services only get the SYSTEM token when the **system role** is active (setgid launcher `wex-sysrun`, machine root
`root:wexsys`, elevation Helper value, runtime in root-owned paths). A guest that is not provisioned like a real WineExplorer
install runs services with the user's filtered token, and every installer that depends on a SYSTEM service fails in ways
that look like Wine bugs (the Chrome updater's `AddAllowedAce` 0x80070005 and Edge's `is machine: 0` both were this, until
the guest was provisioned; see `CHROME_INSTALLER_ANALYSIS.md` sections 12 to 15). Use this recipe before any browser or
machine-wide installer test in a guest. Scripts: `scripts/lab/guest-sysrole/` (they assume the lab layout of
`D:/DeepThought/ChromiumLab`, VM B on ssh port 2236, and are run through `run4.sh <script>` or `run10.sh`; read them first).

## Steps (each one fixes a failure that was hit)

1. **Launcher binary.** The guest has no compiler. Build it where `cc` exists (the `wexbuild` distro):
   `cc -O2 -Wall -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now
   -DSYSRUN_CONF='"/etc/wineexplorer/sysrun.conf"' -o wex-sysrun tools/wex-sysrun/wex-sysrun.c`, pass it as `WEX_SYSRUN_BIN`.
2. **Root-owned runtime.** The launcher refuses any path that is not owned by root all the way up
   (`wex-sysrun: refused: not owned by root: /home/debian`). Copy the runtime to `/opt/wex-rt*/build/wow64`
   (`guest-rt2.sh`). A runtime in a user's home can never get the system role.
3. **Source-linked data.** An in-tree build symlinks `nls/*` and `fonts/*` into the source tree
   (`../../../wine/nls/...`). Copy `wine/nls` and `wine/fonts` next to the runtime (`/opt/wex-rt2/wine/...`) or the server
   dies with `wineserver: failed to load l_intl.nls`.
4. **Machine root.** `scripts/setup-machine-root.sh` with `WEX_WINE_BIN`, `WEX_WINESERVER_BIN`, `WEX_NTDLL`,
   `WEX_SYSRUN_BIN`, `XDG_RUNTIME_DIR=/run/user/0`, no `DISPLAY`, and `src/resources/fonts` present next to it
   (`guest-sysrole-provision.sh`). It does not rewrite `sysrun.conf` or the `/opt` symlinks of an existing machine root when
   the runtime path changes: edit `/etc/wineexplorer/sysrun.conf` and re-link the machine root symlinks
   (`relink.sh`), otherwise `services.exe` dies with exit code 5 (`sysrun_main_image` rejects an image that is not the
   machine root's own).
5. **Elevation Helper** `HKLM\Software\Wine\Elevation\Helper` in the machine `system.reg` (`guest-sysrole-user.sh`); without
   it the system role is off even when the launcher works.
6. **Users.** A normal user (`wextest`) and a filtered administrator (`wexadm`, in `sudo`), both outside `wexsys`. The first
   logon personalization is not done by `useradd`: replace the template profile name `wexsetup` in the user's `user.reg`
   (`guest-personalize.py`) and create `drive_c/users/<user>/AppData/Local/Temp`, otherwise `TEMP` points at
   `C:\users\wexsetup\...`. Make `/home/debian` traversable (`chmod o+x`) if the loader is run from there, else exit 126.
7. **Elevated processes run in the machine root**, whose `drive_c/users` has no profile of the user: create
   `machine/drive_c/users/<user>/AppData/Local/Temp` (`mkprof2.sh`, owner the user, group `wexsys`) or elevated `msiexec`
   ends silently with 1627 after `GetTempFileNameW path not found`. This is a product gap (the broker should create it).
8. **Unattended elevation (lab only).** Install the compile-time test broker: `wexelevate_test.exe`
   (`-DWEXELEVATE_TEST_APPROVAL`), `wexelevate_stub.exe`, point the machine root `wexelevate` service `ImagePath` at
   `wexelevate_test.exe` (`guest-broker-path.py`; the entry already exists, do not append a second one), `/tmp/wexelev/approve.all`,
   and start the prefix with the secure desktop holder (`sdprobe64.exe hold`, `guest-elev-run.sh`). `msiexec` asks the broker
   by itself for a per-machine package from a limited token; other programs go through `runas_wait.exe`.
   Never leave these in an image: `test_sysrun_server.sh` has the checks.
9. **Debug output of elevated processes.** The launcher drops `WINEDEBUG`. Set it in the machine-wide Environment
   (`guest-env-debug.py '<channels>' | off`); the output goes to the log of the client that started the server
   (`/tmp/gsr-out/sdhold.log`). A `cmd /c set WINEDEBUG=...` wrapper overrides your channels.
10. **Disk.** Each elevated attempt copies the MSI into `machine/drive_c/windows/Installer` (170 MB for Chrome); delete them
    between runs. Keep more than 8 GB free; remove old `*/prefix` directories.

11. **A display for the elevated prefix.** Installers (the Edge updater) create a hidden window and fail with 0x80004005 when the
    prefix has no graphics driver (`err:winediag:nodrv_CreateWindow`, then a misleading "Failed to ::ShellExecuteEx"). Start the
    run with `WEXDISPLAY=1` (`guest-elev-run.sh` starts a headless labwc as `wexadm`, enables winewayland) and put
    `WAYLAND_DISPLAY=wex-shell-0`, `XDG_RUNTIME_DIR=/run/user/1002`, `WEX_WAYLAND_NATIVE_WINDOWS=1` into the machine Environment
    (`guest-env-debug.py VALUE NAME`). The labwc binary and its config must be readable by `wexadm`
    (`chmod o+rx /home/debian/wex-g4 /home/debian/wex-g4/labwc`).
12. **`runas failed: error 1055`** (service database locked) means the machine root is still auto-starting services (the installed
    Chrome and Edge updater services make that take a minute). `guest-elev-run.sh` retries up to 8 times.
13. **Heavy debug channels break startup.** `+relay` writes about 850 MB per Edge run, `+ole` about 250 MB; `+service`/`+security`
    together with `+process` made the elevated start fail with 1055. Use `err+all,+exec,+process` first, then one channel at a
    time. Check `system.reg` after `guest-env-debug.py`: the `WINEDEBUG` line can exist twice.

## Known open items found by this recipe

* `setup-machine-root.sh` left `pe_section_cache/*.cache` as `root:root` (fixed in the script, 2026-11-05).
* The broker should create the user's profile directory in the machine root (step 7).
* The first-logon personalization should be a callable script, not something each lab re-implements (step 6).
* After the registry descriptor fix the Chrome updater now fails in COM marshalling
  (`dispatch_typelib_ps_CreateStub` E_NOTIMPL for `{ACAB122B-...}`); see `CHROME_INSTALLER_ANALYSIS.md` section 15.
