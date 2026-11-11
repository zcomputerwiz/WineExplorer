<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Exclusively owned U1 guest

Codex owns setup and lifecycle. This is separate from A, B, wexbuild and the
elevation fixtures. The new disk is a self-contained verified clone under
`/var/tmp/wex-uwp-u1-images-20261111/`; no shared writable backing file is used.
The first partial disk on D: is retained for diagnosis and is never booted.

Run inside Debian as root, or pass the arguments directly through Windows
`wsl.exe --distribution Debian --user root --exec` (avoid nested shell quoting):

```
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py start
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py ready
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py status
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py ssh 'uname -a'
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py stop
python3 /mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py restart
```

`start` requires completed disk check/comparison evidence and refuses an
existing run record. The headless KVM guest uses 8 GiB, four vCPUs, the owned
disk, loopback SSH 2237 and its own QMP/serial/run files. The run directory is
`/var/tmp/wex-uwp-u1-20261111`, mode 0700, with an owner marker and control lock.
Control rechecks PID/start ticks/command line/disk before acting. `ready` pins
the SSH host key only after checking the owned QEMU; subsequent SSH requires
that pin. No credential contents are written to logs. Explicit `restart` is
accepted only after the recorded process has stopped; previous identity is
retained, and the root disk has a fixed firmware boot index. The separately
owned 160 GiB work disk is attached with its serial at restart. No shared disk
is resized or reformatted.

`g-u1.sh` wraps only this owned endpoint. `wait-ssh-u1.sh` uses bounded readiness
attempts. Neither helper supports arbitrary ports/users or disables host-key
verification. Do not use old A/B wrappers for U1.

`stop` requests powerdown via the owned QMP endpoint and waits at most 50s for
process exit. A nonresponsive guest is retained with an error for inspection;
the tool never kills globally or claims graceful shutdown from a request alone.
Setup/check/build acceptance and state left behind are recorded in
`docs/uwp/LAB_STATUS.md`. Headless boot/SSH is not graphics/presentation proof.
