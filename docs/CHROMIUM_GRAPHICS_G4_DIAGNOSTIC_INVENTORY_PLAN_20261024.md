<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Diagnostic inventory policy and next bounded work

The human explicitly directs recording identities/hashes for diagnostics rather
than blocking on historical matches. This supersedes older runbooks' historical
device/inode/timestamp/binary-hash equality gates. Keep those old evidence records
unchanged. No more investigation of device71->58 is needed before app testing.

Parent independently reviewed R4:54 sealed files/12 archive members; original
native journal parses to full3221225599/0xc000007f with nonce/image/held-handle
binding, natural helper0 and completed cleanup. QMP protocol/guest event,
QEMU0/no fallback/children stopped/Windows0 match. Private review:
`native-r4-parent-evidence-review-20261024.json`. Native tool calibration accepted;
the historical storage check failure is retained as a diagnostic warning under
the new policy. This proves neither browser rendering nor effective sandboxing.

## Luna implementation, offline only

1. Add a small reusable diagnostic inventory helper. Capture actual file path,
   device/inode/size/times and actual SHA256, separately from optional previous
   values. Differences produce structured warnings, never application failure.
   A changed executable hash must identify the actual bytes tested and invalidate
   any claim that an old version/pin was tested; it does not stop diagnostics.
   Do not invent actual metadata on read failure: record an observation error.
2. Integrate this policy in current native collector environment/internal-loader
   observations and prospective private orchestration. Exact loader pathname and
   command-tail ownership remain; remove historical binary hash refusal, retaining
   actual values/warnings. For current orchestration, historical storage identity
   and content differences should be reportable without aborting. Make common
   helpers reusable by the next Chromium control; don't rewrite consumed manifests
   or rerun R4. Preserve observed native failures and all previous evidence.
3. Keep concrete ownership and operation checks: selected D:-backed writable disk
   and intended backing paths, regular/canonical paths, no evidence overwrites,
   no active other owner, fresh run/profile/output; exact owned process PID/start
   before signalling, private networking/sockets/budgets/cleanup. Download/archive
   traversal and transfer-integrity checks remain: corruption is different from
   a legitimately different historical version. Functional readiness/pixels/API/
   full native status assertions stay unchanged. No security bypass.
4. Meaningful tests: historical dev/inode/time/hash changes logged without abort;
   current hash actually computed; missing/unreadable metadata labelled unknown;
   wrong disk/backing/output overwrite/PID reuse still refused. Update historical
   equality-failure tests to the new user contract, not functional assertions.
   Update APP_TEST_HARNESS and current policy to avoid stale blocking instructions.
5. Commit AND push implementation/tests/report. Document any old inactive code
   intentionally not migrated and ensure it is not selected by future controls.
   NO VM/SSH/native/browser/disk mutation in this task. Parent reviews before
   fresh Chromium native-exit diagnostic preparation on existing D: storage.

Next high-value experiment after this small change: use the now-calibrated native
held handle to obtain Chromium's actual Windows exit/failure status (original
baseline Linux127 alone is insufficient). Reuse private cached upstream runtime,
original HTTP fixture and normal security/graphics flags. Plan that separately;
no repeat native calibration, new disk chain or provenance gate bureaucracy.

Work only C: graphics worktree codex/g4-r1-completion, preserve M wine/mainDmaster/
other agents. Reuse Luna Dirac. Private inputs D:.95% five-hour/99% weekly policy
remains; no credits, master merge, production rebuild or vendor publication.
