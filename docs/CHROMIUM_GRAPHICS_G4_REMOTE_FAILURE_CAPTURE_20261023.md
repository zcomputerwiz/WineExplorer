<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Remote failure capture — offline review checkpoint

Implemented only the [remote-failure plan](CHROMIUM_GRAPHICS_G4_REMOTE_FAILURE_PLAN_20261023.md).
No VM, SSH, Wine, native helper or browser execution; no new preparation or retry.
Original failed R1 artifacts remain unchanged. The underlying remote errno/cause
is still unavailable; this change cannot reconstruct its discarded stderr.

The ignored C: worktree `build/g4-virgl-20261022/presentation-timing-trial.py`
now saves `remote_failures` before rethrow/finally, retaining actual argv and
return code plus separate stdout/stderr presence, input type, encoding/origin,
original byte length, retained length, cap, truncation and base64. Retained caps
are 4096/8192 bytes. Bytes are opaque/lossless; text is reversibly reencoded as
UTF-8/surrogatepass, explicitly NOT claimed to be original undecoded transport
bytes. Missing and empty remain distinct. Existing exception repr is retained.
Diagnostic serialization/write failure cannot replace the primary exception;
secondary errors remain in final metadata/stdout where possible. A final save
failure without an earlier primary failure still raises.

The idle probe, sudo command, remote invocation/text policy and budget gates
are unchanged. Preconsumer failure still skips archive and performs no later
stage/consumer/archive SSH. AST comparison of the before/after probe literal
matched SHA256 `d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0`.

## Retained private change

Directory: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/remote-failure-capture-r1-20261023.change/`.

| File | SHA256 |
| --- | --- |
| procedure.before.py | ee564d104615235d5d9a6c14e44d1a2fe58aba5096f8b8d986554a66d5e4272e |
| procedure.after.py (actual current procedure) | 4cea0c0bfd9cb5aa49f47d864949ace0a1f007a11207009dd50f7a4b52f449a3 |
| procedure.patch | 8a154d1eda9035fd5ae9540fda0e4810adecf1bc76faa50d799b8050c867385e |

This does not update any old immutable manifest/helper pin or authorize using
it with the changed procedure. Parent review and a separately scoped fresh
read-only diagnostic preparation are required before any future guest action.

## Actual validation

In WSL Debian, from this owned C: worktree's `scripts` directory:

```text
python3 -B -m unittest test_g4_remote_failure test_g4_virgl_budget -v
Ran 26 tests in 0.609s — OK, no skips, exit 0
```

Nine new tests execute the actual ignored formatter/procedure, not a duplicate
model: None/empty, non-UTF8/text, byte caps, early save, no later dispatch,
OSError/all-save failure, actual JSON serialization failure and unsupported
payload failure. One actual local Python child emits distinct stdout/stderr
and exits 7, producing a real CalledProcessError through that procedure path;
its subprocess timeout is capped at 5 seconds. All other remote dispatches are
fakes. New test temporary directories use D: storage. Existing 17 budget/private
procedure tests also passed. The private procedure/D: tests explicitly skip if
those private inputs are unavailable; none skipped here.

No guest/native API/calibration result follows from these tests. M wine and
other checkouts remain untouched. STOP for parent review; no idle-probe policy
fix, automatic calibration or live authority is implied.
