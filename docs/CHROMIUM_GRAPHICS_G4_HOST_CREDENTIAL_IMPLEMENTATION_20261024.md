<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host credential repair — OFFLINE ONLY, no new preparation

Implements [HOST_CREDENTIAL_PLAN](CHROMIUM_GRAPHICS_G4_HOST_CREDENTIAL_PLAN_20261024.md).
No QEMU/guest/SSH connection, service change, disk modification, key generation,
WSL restart/shutdown, new manifest or retry. Existing private procedures/seeds/
failed artifacts remain unchanged. M wine/D:master/other workers untouched.

## Actual host observations

Existing authorized `D:/VirtualBox_VMs/vm_key` copied exclusively into
`/var/tmp/wex-g4-lab-credentials/vm_key`: canonical native directory0700 and
regular non-symlink key0600, ownerUID0. Internal source-content comparison and
derived-public key type+base64 match to accepted seed passed. Only metadata and
fingerprint recorded, never private bytes or a private content hash:
`SHA256:JBfsSgezXod5+mnaZgYAbCMZwC65X0PDgPfejqFDPHQ`.

Two separate ordinary `wsl -d Debian --exec python3 -B .../observe-credential.py`
invocations (arguments1 then2) passed. First adopted the absent destination;
second reused it without writing. Key device2096/inode453768/411bytes/mode0600
and timestamps unchanged. Both saw boot UUIDce05ad81-ffb5-4ad3-b740-eecda22ba6ea,
mount namespace `mnt:[4026532219]`; old `/tmp/.../vm_key` absent in both.
Host findmnt observed `/tmp` as tmpfs and `/var/tmp` backed by root ext4 `/dev/sdd`.
These are current facts, NOT proof why the earlier copy disappeared, or a
guarantee across future restarts/namespaces. No restart was requested or forced.

Private evidence directory:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/host-credential-r1-20261024/`.
Contains original host-only observer and two metadata/public-match observations.
`sealed-evidence.json` hashes those3 files, SHA256
`af2d461f309cca6fcc4d87a92af172e6a37a269aafc4a5324628662d8e0df366`.
Credential itself is NOT included in evidence publication or Git.

## Narrow source repair / actual tests

Readiness now immediately labels explicit OpenSSH identity-file-not-accessible
as `host_credential_unavailable`; explicit unprotected/too-open/bad-permissions
private-key messages as `host_credential_unsafe_permissions`. This precedes
co-occurring network/auth messages (even a fallback identity's exit0), preserving
raw stderr and preventing a second attempt. Plain transient network errors still
retry to two healthy same-boot replies; strict auth/host-key/identity/deadline/
cleanup assertions remain. Guide updated with operator-supplied credentials,
durable native path, per-execution verification and no automatic auth repairs.

WSL Debian, owned C: checkout `scripts`:

```text
python3 -B -m unittest test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 72 tests in 4.713s — OK, exit0, no skips
```

Prior69 controls plus3 discriminating methods: missing identity+banner/auth
failure stops once, unsafe-key permissions stops once, network-only banner/
connection-refused can progress to two healthy replies. Original local Python/
socketpair controls execute as before, with D: temporary artifacts; no SSH.

New readiness source SHA256
`5232f407f0f6a44b06ea306de185e9f13dc8b97ee243119426e8a7db4c9aac7f`.
All old manifests/helper pins are preserved and now stale/unsuitable; durable
credential path/helper hash must enter a separately reviewed fresh preparation.
STOP for parent review. No live guest readiness/idle/graphics or disappearance-
cause claim; no implicit authority to create that next manifest or run a guest.
