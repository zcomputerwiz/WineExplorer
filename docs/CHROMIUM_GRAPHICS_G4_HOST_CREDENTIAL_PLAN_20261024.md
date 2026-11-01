<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host credential repair, offline only

Parent reviewed actual protected run `a474a4f9`: independently rehashed all 28
sealed files, decoded the actual four OpenSSH failures and QMP wire transcript,
and checked the exited Windows controller0. Every SSH attempt reported a missing
host identity file; no authenticated reply or idle probe occurred. This is not
evidence of a guest key mismatch. The precise disappearance cause is unknown.

The strict QMP observer worked on this run: matching capability/powerdown ACKs,
post-request SHUTDOWN guest=true/reason=guest-shutdown, owned QEMU0, no fallback,
children stopped and controller0. Reviewed post-run metadata/27 inventories/
unchanged frozen three identities/full former-top hash match. No filesystem-clean
or old-timeout-cause claim. New top changed legitimately and is retained at
27,590,656 bytes, SHA256
`393308d2dcf343df1dac1d6a2f47a4f4f27d70e26a88596d0d3f97eacf14999c`.
The one boot authority is consumed. Do not rerun any existing manifest.

## Luna task: durable host prerequisite and precise failure

No QEMU, guest boot, SSH connection, staging, calibration or browser execution.
No disk/seed/guest credential changes, key generation, WSL restart/shutdown or
changes to other workers' sessions. Current source checkout and D: rules apply.

1. Record read-only host metadata for the missing temporary path, boot UUID,
   mount namespace and `/tmp`/`/var/tmp` backing. Explain only observed facts;
   do not assert that WSL reboot or tmpfiles caused the loss without evidence.
2. Restore the EXISTING authorized `D:/VirtualBox_VMs/vm_key` into an owned,
   native Linux durable directory `/var/tmp/wex-g4-lab-credentials` (0700), key
   `vm_key` (0600), exclusive absent/non-symlink target. Compare the internally
   derived public key against accepted seed key type+base64 before adopting it.
   Record metadata/fingerprint/match only; never private bytes. Reuse an existing
   destination only if secure identity/modes/content/public match can be proved;
   otherwise stop without overwrite. Do not rotate keys or alter accepted seeds.
3. Verify its readability/identity/modes/public match from two separate ordinary
   WSL invocations, recording boot/mount identities. No forced session restart.
   This proves those observations, not persistence across every future reboot.
4. Narrowly improve `g4_guest_readiness.py` so explicit OpenSSH identity-file-
   unavailable and unsafe-private-key-permissions messages stop immediately with
   a host-credential error. Preserve original bytes; do not misreport these as
   guest authentication rejection or continue retrying banner timeouts when the
   same stderr already establishes a missing identity. Retain ordinary transient
   network retries, strict host-key/auth failures and all timing/cleanup gates.
5. Add discriminating negative tests: missing identity plus banner timeout stops
   after one attempt with no second request; unsafe-key-permission refusal is
   fatal; ordinary banner/refused connection can still progress to two healthy
   replies. No weaker existing assertions. Run focused readiness plus the prior
   integration/QMP/launcher/budget/capture/idle suites.
6. Update the reusable harness guide to explain operator-supplied credentials,
   durable native paths and per-execution checks. Do not add a generic framework
   or promise a stable public API. Preserve all old helpers, source hashes and
   manifests as historical evidence: new helper hash makes old preps unsuitable.

Seal private host-only evidence on D: and commit AND push code/tests/report.
STOP for parent review. Fresh r2 read-only preparation is a separate next task;
no new manifest or guest run in this handoff. Expected future source path changes
must be pinned in that fresh preparation. Current usage73% five-hour/27% weekly;
at95% finish bounded work/checkpoint, weekly99% stop; no reset credits.
