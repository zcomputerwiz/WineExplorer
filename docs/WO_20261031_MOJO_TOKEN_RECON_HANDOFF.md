# HANDOFF — WO-20261031-CHROMIUM-MOJO-TOKEN-RECONCILIATION (in-guest A/B)

> Historical handoff: superseded for execution by
> [the corrected 0x88 continuation](CHROMIUM_MOJO_0X88_CONTINUATION_20261031.md).
> The old probe requested the wrong access mask; guest startup is currently
> blocked before any valid A/B verdict, and the blanket descriptor-copy
> candidate is unaccepted. Use verified runtime paths and exact live server
> identity, not the old override assumption. The later continuation authorizes
> candidate commits/pushes; the historical ownership restriction below no
> longer governs this work.

**Prepared by:** claude (agent-bridge identity), 2026-10-31 ~14:05 local
**Status:** PAUSED — recon complete, in-guest execution NOT yet done. Guest powered off cleanly.
**Owner of git this run:** antigravity (do **not** commit/push).

---

## 1. Objective & deliverable

Antigravity committed both fixes and left pre-compiled artifacts on the host. The task is to run the
pre-compiled mojo bootstrap probe against the R6 wine build (`ce87bac0`) in two configurations, in the
QEMU guest, and report whether the sandboxed modes flip **FAIL → PASS**:

- **Run A (baseline):** R6 wine + R6's own **original unpatched** wineserver.
- **Run B (reconciled):** the SAME R6 wine, but with the **reconciled** wineserver (built from `f2e67abe`
  = `ce87bac0` + only the `server/token.c` self-token fix). Override via the `WINESERVER=` env var — do
  **not** overwrite the R6 tree's own wineserver file.

Each run executes `chromium_mojo_bootstrap_probe.exe all`, which covers all three modes:
`plain`, `service_restricted`, `network_appcontainer`.

**Deliverable to antigravity (via agent-bridge, recipient `"antigravity"`):** exact verdicts + bounded
stdout/stderr for Run A and Run B across all 3 modes; whether `service_restricted` and
`network_appcontainer` flipped FAIL→PASS from A to B; the baseline R6 wineserver SHA and confirmation the
reconciled wineserver SHA in-guest = `853c9f0c…`; and confirmation of a clean guest powerdown.

---

## 2. Current state (as of handoff)

- **Guest: POWERED OFF** (graceful QMP shutdown). The prior attempt booted at 13:45, ran idle to the
  launcher's 900s deadline, and auto-shut-down at 14:00 — `stop_reason:"deadline"`,
  `graceful_powerdown:true`, `qemu_exit_status:0`. **No probe work was executed; no logs captured.**
- **WSL:** no qemu / Xvfb / wineserver processes running. Antigravity already ran
  `wsl.exe -u root pkill -9 -f 'C:\windows'` to clean orphaned Wine services.
- **Preserved trees: untouched** (`wine-verify-r6-20261030`, `wine-control-channel-20261027`).
- **Artifacts on host: present, SHAs verified** (see §3).
- **SSH access: CONFIRMED working** — private key located at `/var/tmp/wex-g4-lab-credentials/vm_key`
  (provided by antigravity; verified to exist, 411 B, root:600).
- **Stale run dir exists:** `/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/mojo-token-recon-20261031/`
  (idle boot record only). ⚠️ `launch.py` does `output.mkdir(exist_ok=False)`, so a **new run MUST use a
  different run name** (e.g. `mojo-token-recon-r2`) or you must delete this stale dir first.

---

## 3. Reference table

| Item | Value / path |
|---|---|
| **Probe PE (host)** | `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium_mojo_bootstrap_probe.exe` = WSL `/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium_mojo_bootstrap_probe.exe` |
| **Probe PE SHA-256** | `5f589ef1fd25b90daabbd9c22edf2fe300a019ec04d8cdf766bbe78b665fa1f3` (336,893 B) |
| **Reconciled wineserver (host)** | `D:\DeepThought\Builds\wine-reconciliation\build\server\wineserver` = WSL `/mnt/d/DeepThought/Builds/wine-reconciliation/build/server/wineserver` |
| **Reconciled wineserver SHA-256** | `853c9f0cc44522b22309db7efbf40cce143b4e1d55944c71dcc92c407a148810` (5,324,784 B) |
| **SSH private key** | `/var/tmp/wex-g4-lab-credentials/vm_key` (root:600) |
| **known_hosts** | `/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts` |
| **SSH target** | `-p 2235 debian@127.0.0.1` (hostfwd from launch.py) |
| **Launcher** | `/mnt/d/DeepThought/Worktrees/chromium-token-integration/WineExplorer/build/g4-virgl-20261022/launch.py` |
| **Guest disk (overlay)** | `/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-native-identity-r4-20261024.qcow2` (backing: `system-capacity-r1-20261023.qcow2`; writes isolated to overlay) |
| **R6 build tree (in guest)** | `/home/debian/wex-g4/wine-verify-r6-20261030/build` — wine launcher `<that>/wine`, original wineserver `<that>/server/wineserver`. **Verify this path exists in-guest first** (`ls /home/debian/wex-g4/`). |
| **Preserved trees (DO NOT TOUCH)** | `/home/debian/wex-g4/wine-verify-r6-20261030`, `/home/debian/wex-g4/wine-control-channel-20261027` |
| **Host log output dir** | create `/root/wex-mojo-token-recon/` (do NOT overwrite the old `/root/wex-mojo-probe/`) |

### SSH invocation (confirmed by antigravity)
```bash
ssh -i /var/tmp/wex-g4-lab-credentials/vm_key \
    -o BatchMode=yes -o ConnectTimeout=5 \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts \
    -p 2235 debian@127.0.0.1 '<command>'
```
For file transfer use the same `-i`/`-o` options with `scp … -P 2235`.

---

## 4. Execution procedure (copy-paste ready)

All host commands run from WSL Debian. **WSL gotcha:** long multi-statement `wsl … bash -lc "…"` strings
get mangled by wsl.exe — keep each Bash call to a short single command, or write `.sh` scripts to disk and
run those. Symlinks already exist to shorten paths: `/root/wexbuild` → build dir, `/root/wexvm` → VM root.

### 4.1 Pre-flight (host)
```bash
# Confirm no guest is running:
ps -eo pid,comm,args | grep -iE 'qemu|launch.py' | grep -v grep     # expect: nothing
# Re-verify artifact SHAs on host:
sha256sum /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium_mojo_bootstrap_probe.exe
sha256sum /mnt/d/DeepThought/Builds/wine-reconciliation/build/server/wineserver
```

### 4.2 Boot the guest (background) — use a FRESH run name
```bash
wsl -d Debian --exec unshare --mount --propagation private \
  env WEX_G4_RUN_NAME=mojo-token-recon-r2 \
  python3 -B /mnt/d/DeepThought/Worktrees/chromium-token-integration/WineExplorer/build/g4-virgl-20261022/launch.py &
```
Note the launcher PID (`$!`) — you'll signal it for early teardown in §4.8. It boots QEMU (KVM, q35, 8 GB)
and **waits up to 900s** then does an identity-verified graceful QMP powerdown in its `finally`.

### 4.3 Wait for SSH readiness
Poll until this returns `OK debian` (guest boot + sshd up can take ~1–2 min):
```bash
ssh -i /var/tmp/wex-g4-lab-credentials/vm_key -o BatchMode=yes -o ConnectTimeout=5 \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts \
    -p 2235 debian@127.0.0.1 'echo OK; whoami'
```

### 4.4 Stage artifacts into the guest + verify SHAs in-guest
```bash
# Copy probe PE and reconciled wineserver to /home/debian:
scp -i /var/tmp/wex-g4-lab-credentials/vm_key -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts -P 2235 \
    /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium_mojo_bootstrap_probe.exe debian@127.0.0.1:/home/debian/
scp -i /var/tmp/wex-g4-lab-credentials/vm_key -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts -P 2235 \
    /mnt/d/DeepThought/Builds/wine-reconciliation/build/server/wineserver debian@127.0.0.1:/home/debian/reconciled_wineserver
```
Then in-guest, verify both SHAs and record the **baseline R6 wineserver** SHA (Run A control):
```bash
ssh … -p 2235 debian@127.0.0.1 'sha256sum /home/debian/chromium_mojo_bootstrap_probe.exe /home/debian/reconciled_wineserver /home/debian/wex-g4/wine-verify-r6-20261030/build/server/wineserver'
```
Expected: probe = `5f589ef1…`, reconciled = `853c9f0c…`. The R6-tree wineserver SHA is your Run A baseline — record it.

### 4.5 Run A (baseline: R6 + original R6 wineserver)
Guest-side, fresh private prefix wiped each run (pattern from `_wex_ipc_run_mojo_probe.sh` in this CWD):
```bash
ssh … -p 2235 debian@127.0.0.1 'bash -s' <<'EOF'
set -u
WINE_BIN=/home/debian/wex-g4/wine-verify-r6-20261030/build/wine
PFX=/home/debian/mojo-pfx; LOG=/home/debian/mojo_A.log
pkill -u debian wineserver 2>/dev/null; sleep 1
rm -rf "$PFX"; mkdir -p "$PFX"
WINEPREFIX="$PFX" WINEDEBUG=-all timeout 150 script -qec "$WINE_BIN /home/debian/chromium_mojo_bootstrap_probe.exe all" "$LOG" >/dev/null 2>&1
echo "RUN_A_EXIT=$? (124=timeout)"; echo "=== LOG A ==="; cat "$LOG"
EOF
```

### 4.6 Run B (reconciled wineserver via WINESERVER= override)
Same as Run A but prepend `WINESERVER=/home/debian/reconciled_wineserver` to the env so **all** wine
processes use the reconciled server:
```bash
ssh … -p 2235 debian@127.0.0.1 'bash -s' <<'EOF'
set -u
WINE_BIN=/home/debian/wex-g4/wine-verify-r6-20261030/build/wine
PFX=/home/debian/mojo-pfx; LOG=/home/debian/mojo_B.log
pkill -u debian wineserver 2>/dev/null; sleep 1
rm -rf "$PFX"; mkdir -p "$PFX"
WINEPREFIX="$PFX" WINEDEBUG=-all WINESERVER=/home/debian/reconciled_wineserver \
  timeout 150 script -qec "$WINE_BIN /home/debian/chromium_mojo_bootstrap_probe.exe all" "$LOG" >/dev/null 2>&1
echo "RUN_B_EXIT=$? (124=timeout)"; echo "=== LOG B ==="; cat "$LOG"
EOF
```
**Verify the override actually took effect** (otherwise Run B is meaningless): during/after the run confirm
the running wineserver path was `/home/debian/reconciled_wineserver`, e.g. by checking the probe's own
diagnostics or `ps` for the wineserver binary path mid-run. If `WINESERVER=` does not take effect, fallback:
copy the R6 tree to a scratch location (do NOT modify the preserved tree), drop the reconciled wineserver in
that copy's `server/`, and run from the copy.

### 4.7 Collect logs to host
```bash
mkdir -p /root/wex-mojo-token-recon
scp -i …vm_key -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/known_hosts -P 2235 \
    debian@127.0.0.1:/home/debian/mojo_A.log /root/wex-mojo-token-recon/
scp … debian@127.0.0.1:/home/debian/mojo_B.log /root/wex-mojo-token-recon/
```

### 4.8 Teardown + verify
Once Run A and B logs are collected, end early by signalling the launcher PID (its `finally` performs the
identity-verified graceful QMP powerdown — do **not** wait for the 900s deadline):
```bash
kill -TERM <LAUNCHER_PID>          # from §4.2 $!
```
Then confirm: `<run_dir>/results.json` shows `"graceful_powerdown":true`, `"qemu_exit_status":0`,
`cleanup.qemu_stopped`/`xvfb_stopped` true; and no qemu/Xvfb/wineserver processes linger in WSL.

### 4.9 Guest scratch cleanup (optional but tidy)
```bash
ssh … -p 2235 debian@127.0.0.1 'rm -rf /home/debian/mojo-pfx /home/debian/chromium_mojo_bootstrap_probe.exe /home/debian/reconciled_wineserver /home/debian/mojo_A.log /home/debian/mojo_B.log'
```
(Do this **before** powering down, or skip it — the overlay discards nothing important. Never delete the two preserved trees.)

---

## 5. Hard constraints (do not violate)

- Do **NOT** delete or modify the preserved trees `wine-verify-r6-20261030` and `wine-control-channel-20261027`.
- For Run B, override via `WINESERVER=` — never overwrite the R6 tree's own wineserver file.
- Fresh private prefixes only (wipe per run). No shared prefixes.
- Do **NOT** start Edge. Do **NOT** run an unbounded `WINEDEBUG=+server` trace — bounded probe stdout/stderr only.
- Clean up guest scratch you create; keep the preserved trees.
- No git commit/push (antigravity owns git this run).

---

## 6. Interpretation / expected outcome

Root cause (established in the prior WO, confirmed by antigravity): a Chromium-style sandboxed child fails
the mojo bootstrap **not** at handle inheritance or the pipe read (the invitation
`"MOJO_INVITATION_CONNECT_NODE"` is delivered) but at its **own** `OpenProcessToken`/`OpenThreadToken` of its
own primary/thread token — wineserver `ACCESS_DENIED` even for bare `TOKEN_QUERY` → child `exit_code=13` at
LowerToken → parent sees `ERROR_BROKEN_PIPE`(109)/`STATUS_PIPE_BROKEN`. Location: `server/wineserver`,
`server/token.c` `open_token`/`alloc_handle` (token DACL checked against restricting SIDs; default token DACL
lacks the restricting SIDs). The reconciled fix (`f2e67abe`) changes wineserver self-token access semantics.

**Expected if the fix works:** Run A → `plain` PASS, `service_restricted` FAIL, `network_appcontainer` FAIL
(same as before). Run B → all three PASS (the two sandboxed modes flip FAIL→PASS). If Run B still fails on a
sandboxed mode, capture that mode's bounded output and report it — do not chase with unbounded traces.

---

## 7. Reporting to antigravity

Send via agent-bridge (`sender:"claude"`, `recipient:"antigravity"`), referencing
`WO-20261031-CHROMIUM-MOJO-TOKEN-RECONCILIATION`. Include: (1) how guest access was established (vm_key);
(2) baseline R6 wineserver SHA + confirmation reconciled SHA in-guest = `853c9f0c…`; (3) Run A verdicts per
mode with bounded stdout/stderr; (4) Run B verdicts per mode with bounded stdout/stderr; (5) explicit statement
of whether `service_restricted`/`network_appcontainer` flipped FAIL→PASS; (6) clean-powerdown confirmation
(graceful_powerdown + qemu_exit_status, no lingering procs); (7) host log location. After reporting, re-arm
the ~15-min session-only mailbox watcher cron on mailbox `"claude"`.

---

## 8. Gotchas & pitfalls

- **900s hard deadline** in launch.py: the whole in-guest op must fit in 15 min (it does — two bounded probe
  runs + prefix init ≈ a few minutes). If you run short, signal early (§4.8); if you need more time, boot a
  fresh run with a new name rather than editing launch.py.
- **Fresh run name required** each boot (`mkdir(exist_ok=False)`); the stale `mojo-token-recon-20261031` dir
  will collide — use `-r2` or delete it.
- **WSL argv mangling:** keep Bash calls short / single-command, or run scripts from disk. `$VAR` in a
  double-quoted `bash -lc "…"` gets expanded by the outer Git Bash — use single quotes or literal paths.
- **Raw `wine` ≠ your build:** always invoke the R6 tree's own `<build>/wine`, never host `/usr/bin/wine`.
- The prior background agent (a fork) was stopped at ~14:05 while still polling for SSH readiness; it left
  only an idle boot record in the stale run dir. Everything above is self-contained — no dependency on that
  agent's partial work.

---

## 9. Key files already on disk (this CWD = `D:\DeepThought\Projects\OpenClaude`)

- `_wex_ipc_run_mojo_probe.sh` — guest-side probe runner template (fresh prefix, pty via `script -qec`).
- `_wex_ipc_diag_mojo.sh`, `_wex_ipc_run_probe.sh`, `_wex_ipc_init_prefix.sh` — related guest-side helpers.
- `/root/wexbuild` → build dir; `/root/wexvm` → VM root (short-path symlinks, already created).
