<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R6 code accepted; actual host/guest preparation still pending

Reviewed Antigravity/Claude3569ba7e (base d4daff2c). Imported reviewed source via
09153ac4/c2da7ea7 into codex/g4-r1-completion; no Wine gitlink/mismatch staged,
no master merge and no token work imported.

Parent independently ran test_g4_chromium_userenv_r6_adapter,
test_g4_userenv_view, test_g4_chromium_native_exit and test_g4_native_exit:
32 tests,11.182s, all pass, no skips. Profile OUTPUT/profile now agrees with
original native helper contract. Readonly four-module scope and probe gate
remain narrow; ordinary browser eight args/fixture/security behavior unchanged.

Parent additionally executed the ACTUAL original compiled metadata sanity PE
once on native Windows with a fresh nonce and20s bound, no browser/Wine/VM.
Probe SHA8558081c1211a7d0298bf33b2fa0a9ef62d4b3b3079439fbb4efa398ea962c4d,
source snapshot matches build receipt. Native exit0/reaped,21 checks/0 failures,
normal system32 modules, genuine S_OK/FILE_NOT_FOUND/ALREADY_EXISTS sequence,
final owned unregisterS_OK and final lookupFILE_NOT_FOUND; Python v2 decoder
accepted actual stdout. Private evidence r6-parent-native-probe-20261026/ on D:.
No proprietary implementation copied or DLL installed.

Source inspection confirms initial absence gates registration, fresh S_OK
confers ownership, error cleanup acts only on held owned mapping, same prefixed
moniker feeds derive/register. Unit failure-sequence tests mostly fabricate JSON
for decoder checks rather than execute C branches; this is a documented coverage
limit, not production-flow proof. Do not call them exhaustive fault tests. Actual
success-path native PE plus source review and independently accepted Wine49/260
units are sufficient to advance the functional browser gate. General pointer /
fault-injection / full sandbox hardening remain TODO per human priorities.

Operational finding: the delivery's PREPARED_NOT_RUN claim is premature. Its
six-artifact table and direct Python guest-runner invocation are not the R5
Windows launcher/VM/SSH transfer/archive/QMP-stop preparation. Guest LAB
/home/debian/wex-g4 is NOT the host D: directory in WSL. No fresh host R6 adapter,
complete source-stage TAR/deployment inventory or exact launcher/run/stop exists.
Do not execute the report's command directly on the host. Code accepted;
SOURCE_ACCEPTED_HOST_ADAPTER_PENDING. Luna assigned bounded completion using
reviewed R5 adapter pattern and accepted artifacts. Parent reviews final prep,
then authorizes ONE real R6 trial, with no extra new image/download/install.
