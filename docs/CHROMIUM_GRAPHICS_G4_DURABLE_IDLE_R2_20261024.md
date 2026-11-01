<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ONE durable-key r2 run — ready, idle probe ESRCH failure preserved

Executed exact immutable7469840a... commands under
[DURABLE_IDLE_RUN_AUTHORITY](CHROMIUM_GRAPHICS_G4_DURABLE_IDLE_RUN_AUTHORITY_20261024.md).
Fresh31-file/credential/public-seed/current-top/hash/normal4chain/frozenIDs/idle/
reserve gates passed. No source changes, retry, extra SSH, staging, service or
credential repair, calibration/browser/image creation. M wine/D:master untouched.

Controller started02:21:59.423109UTC. Raw674 gate remaining883.961660s;
readiness cap180s,5 attempts over23.923120964s:3 transient SSH255 then2 valid
authenticated replies, same boot e17bbb8b-1d1b-4313-8769-9dbfb3814b5d, expected
nonce/UID1000/debian, cloud-init done/errors empty and systemd running. No missing
identity warning in this result. Logind loaded/active/running recorded, not an
independent ACPI test.650 gate remaining860.029401s; guest free15629598720bytes;
all19 actual binary hashes matched;435 gate remaining859.129688s.

EXACT original d36b5d1d... idle probe then exited1, stdout empty,615stderr bytes
retained without truncation. Final traceback line:

```text
ProcessLookupError: [Errno 3] No such process: '/proc/2/environ'
```

Failure occurred at `(p/"environ").read_bytes()`. Owner list UNKNOWN, not empty
or idle. No extra SSH followed. This proves this read failed with ESRCH; it does
NOT prove a process-exit race, kernel-thread diagnosis or the discarded historical
failure's cause. No policy fix/exception broadening now. Diagnostic exit1.

Exact stopper80s exited0; persistent controller held through WindowsWSL0 at
02:23:07.0408218UTC (PowerShell session0). Retained QMP matched ACKs and post-own-
request SHUTDOWN guest=true/reason=guest-shutdown; protocol4.924656398s, whole
shutdown5.058243754s. QEMU0/no fallback, children stopped, launcher graceful=true.
Linux381/396/383 and Windows15508 independently absent. No filesystem-clean claim.

Post-stop normal-lock4chain passed; frozen3 identities unchanged. Former-top
full SHA c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936
matched before/after in10.689649429/10.883120264s (<120s), no65GB rescan.
Only writable safety top changed:35323904bytes, final SHA256
`cb1aaaa91a4b27ac34be6619f36f63b628c52490a9fba7b39e2530f787ce89b7`.
Post inventory30 immutable files matches (new writable top recorded separately).

Evidence under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:
`native-idle-ready-r2-20261024/` holds execution/readiness/budgets/identities/
QMP-results/serial/host logs; adjacent `.wsl-session.json` and stdout/stderr.
`native-idle-ready-r2-20261024.host-audit/` holds pre/post storage/credential
audits, diagnostic/stopper logs and exact decoded idle stderr/stdout. Its
`final-evidence.json` seals31 files, SHA256
`dd254e4cd632f8669861ae65f649b33ab3062691bec9d40ac737abffa30cac6d`.

No tests rerun or implementation work during execution. At96%five-hour/31%weekly
this report is the safe checkpoint. STOP parent review; no further run, probe
repair, calibration or graphics/G4 claim. Parent handles next-window planning.
