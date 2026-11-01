<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Bounded next fix: retain the remote preflight error, do not guess its cause

Native calibration attempt c6a2ef20/44fa88a4 failed before staging/consumer.
Parent independently verified all23 saved records, actual19 binary map, raw
QMP/QEMU0/Windows0 and Linux435960/435975/435962 + Windows38548 absent. Guest
free15629791232 bytes and650 guard passed;435/deployment/helper/native events
never reached. No calibration/browser/API result, no retry authority remains.

Definite source bug: private presentation-timing-trial.py remote() uses
capture_output=True/check=True, but run() saves only repr(CalledProcessError).
The captured stdout/stderr are lost when that procedure exits. We cannot recover
the underlying remote traceback from current artifacts. Do not claim an ESRCH,
permission, busy-prefix, sudo or browser cause. Seed grants NOPASSWD:ALL, which
does not prove that this particular command succeeded. Preserve all evidence.

## OFFLINE implementation only, smallest bounded step

Reuse Luna Dirac; same C: branch, no D: master/submodules/other-worker changes.
No VM/SSH/Wine/browser/native helper execution, new preparation or runtime copy.

1. In the existing private procedure, retain a bounded structured record of a
   failing remote operation: actual argv/return code, stdout/stderr presence,
   encoding, original byte lengths and retained bytes. Preserve arbitrary bytes
   losslessly (base64 is suitable), and mark truncation with deterministic small
   caps (for example4KiB stdout/8KiB stderr). None versus empty must be distinct.
   Handle text and bytes without replacing the primary exception. Keep existing
   error repr and failure ordering; do not treat remote exit1 as empty/idle.
2. Save this record in execution.json before rethrow/finally. On a preconsumer
   failure archive stays skipped and NO additional SSH occurs. A failure to save
   diagnostic details must not replace the primary remote/preflight failure;
   explicitly retain a secondary diagnostic-write error where possible.
3. Actual unit coverage through the real procedure/formatter path: nonzero remote
   child with distinct stdout/stderr, missing/empty/non-UTF8/text/oversized output,
   truncation/base64 roundtrip, original-error preservation and no subsequent
   stage/consumer/archive dispatch. Include one actual bounded local Python child
   failure to establish real CalledProcessError stderr retention, not only mocked
   canned success. It is an original host test, not a guest/Wine run; D: outputs.
4. Do NOT change the idle-prefix probe, catch broader /proc errors, relax guard
   policy or alter VM permissions yet. No demonstrated underlying errno exists.
   Record transient ENOENT/ESRCH versus permission failure handling as a question
   for the later read-only diagnostic, not a production or preflight cause claim.
5. Commit AND push tests/report and record exact ignored-procedure hash/diff or
   retained patch; no new general orchestration framework. STOP for parent review.

After acceptance, parent will separately plan ONE fresh owned read-only VM
preflight diagnostic that preserves full remote error output. It must NOT reuse
or overwrite the failed host run/preparation. No staging/helper/browser action
will follow automatically, even if that diagnostic returns an empty owner list.
The native helper's fixed R1 guest paths were not staged but their absence must
be verified before any later reuse; old host R1 evidence remains immutable.

95% five-hour/99% weekly stopping rules and enabled heartbeat remain. No credits,
master merge, bypass, loaded production rebuild, proprietary publication or
unreviewed OOBE/security imports. All private VM/runtime/evidence remains D:.
