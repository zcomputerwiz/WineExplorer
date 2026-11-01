<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge installer prerequisite: offline review checkpoint

Status: **PREPARED_NOT_RUN**. No VM boot, SSH, installer or browser execution.
Implements only the installation prerequisite in
`CHROMIUM_GRAPHICS_G4_EDGE_INSTALL_PREFLIGHT_PLAN_20261022.md`; no rendering or
browser acceptance is implied.

Implementation commit `9348b4be` adds `scripts/check-g4-edge-install.py` and
`scripts/test_g4_edge_install.py`. Exact approved MSI/consent/signature-provenance,
fresh owned paths and >=5 GiB free are required before any process, including
cleanup. Reuses owned Wine/compositor helpers and offline isolation; only Wine
`msiexec /i PACKAGE /qn /norestart /l*v LOG` is dispatched. Private HOME/XDG/temp
directories stay under output. Wineboot <=90s, installer <=120s, complete budget
285s including hashing, with 40s reserved for cleanup inside the 300s SSH bound.
Timeout/reboot/cleanup failure cannot pass. Prefix files are retained.

Installed evidence requires bounds-checked MZ/PE COFF machine 0x8664, executable
SHA256 and matching Microsoft Edge registration version 156.0.4314.42.
`version_source=registration`; **no independent PE file-version verification**.
Process snapshots retain installer-created processes; only the owned prefix's
servers are stopped. Log copying is capped at 128 candidates, 16 MiB/file,
32 MiB total and the run deadline; skipped originals remain in the prefix.
Postprocessing failures preserve the primary error and write failed results.

## Actual validation

`wsl -d Debian --exec python3 -B -m unittest discover -s /mnt/d/DeepThought/Projects/WineExplorer/scripts -p test_g4_edge_install.py -v`

20 Linux tests PASS, no skips. Mocked executable flows cover exact MSI argv,
private environment, deadlines, rejected guards without process/finally calls,
boot/compositor/installer/reboot/timeout/cleanup failures, remaining servers,
missing payload/registration, wrong registration version/PE architecture,
malformed PE bounds, successful install evidence and log-copy/parsing failures.
No real vendor code ran. `git diff --check` passed. No unrelated tests rerun.

## Private preparation for parent review

Current D: preparation directory:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-install-r1-v2-20261022.preflight/`.
`preflight.json` records exact launch/run/identity-stopper commands, source commit,
7 staged files (5 Python dependencies + approved MSI + acquisition provenance),
16 known Wine/compositor binary hashes and 5 host-helper hashes. Archive SHA256:
`35150687282dd716801a7ab4c55ae9c70169c966d136d43a9d098696f2072f63`.
Offline verification passed for archive bytes, all 7 members, all 5 local source
files and all 5 host helpers. Destination run directory is absent.

Existing private procedure
`build/g4-virgl-20261022/presentation-timing-trial.py` gained only explicit
`edge-install` command/inventory/evidence parameters, >=5 GiB pre-transfer check
and private-directory archive exclusions. Raw UTC 650/435 guards, exact Linux
identity validation, persistent Windows controller, 900s launcher, 90s shutdown
reserve and separate SIGINT/QMP stopper are unchanged. No runtime/framework or
production diagnostic modification. MSI/acquisition and private procedure remain
outside Git. The earlier `edge-install-r1-20261022.preflight` is preserved history
but superseded (runner hash changed to include guard time in the deadline).

Next action is parent code/manifest/procedure review, not installation. Guest
space, deployed/binary hashes, acceleration, actual installer compatibility and
graceful shutdown remain untested for this preparation. No cleanup/downloads,
host msiexec, bypass, retry or browser invocation occurred.
