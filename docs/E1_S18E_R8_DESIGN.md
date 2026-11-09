# E1-S18e R8: dialog registration and the private password channel (design, not implemented)

Finding R8 in docs/E1_S18_REVIEW.md; agreed direction in docs/E1_DECISIONS.md (2026-11-08) and
docs/E1_S18E_DESIGN_REVIEW.md item 5. The typed password must go straight from the consent dialog
to the root PAM worker over a private socket. The broker and the Python service never handle it;
it is plaintext only on that local channel, as Windows passes it to LSA in a serialized logon
buffer. Nothing here is built yet.

## Today

* `src/dlls/wexhost/e1s6.c` `start_child`: on CHILD_START, wexhost forks a native stub that
  only echoes one S1 answer record. The server endorses that stub's pid; the verifier registers it
  as the dialog (S1 kind 7, role 3). The stub is not consent.exe.
* `src/programs/consent/consent.c` (credentials mode) reads the account and password, wipes the
  password and sends only an empty kind-2 Confirm to the broker over a one-way anonymous pipe.
  It never receives the request's nonce or digests, so its Confirm cannot match.
* `tools/e1s7/pam-worker.c` already requires every answer packet to carry SCM_CREDENTIALS of the
  registered child (around line 157) and rejects null passwords.

## Proposal

1. **Duplex broker channel.** The broker starts consent.exe suspended on the secure desktop with
   an explicit handle list (WO-7) containing only its end of a duplex channel (two anonymous pipes,
   or one private pipe instance with a SYSTEM-only DACL). Before resuming, it writes an
   initialization record: request id, attempt, nonce, binding and request digests, channel id,
   absolute deadline, and the broker-derived display data (requester, image, publisher). consent
   acknowledges with the same binding before showing anything; reads are bounded by the deadline
   and drained concurrently with child death, caller death, stop and lease events (no
   wait-for-exit-then-read, no FlushFileBuffers handshake).
2. **Server endorses the real dialog.** CHILD_START no longer forks a stub. The broker passes the
   suspended consent process handle; the server resolves the process object, checks that it is the
   broker's own child, that its main image is the machine-root `consent.exe` (device and inode, as
   for the broker in R13), SYSTEM, same session and the validated desktop, and endorses that
   process's Unix pid with pidfd pins to the verifier.
3. **Verifier delegates the secret socket.** On that endorsement the verifier creates a
   SOCK_SEQPACKET pair, gives one end only to the pinned PAM worker, and waits for the dialog to
   claim the other: consent's in-process wexhost Unix side calls the verifier over the system bus
   (`DialogAttach(request_id, channel_id)`); the verifier requires the bus peer's pid to be the
   endorsed pid, the pidfd pin still live, and the exact request and channel, then returns the fd
   (SCM_RIGHTS via D-Bus). A second attach, a wrong pid or a late attach cancels the transaction.
   The broker never holds either end.
4. **Answers.** consent sends PAM answers (S1 kind 1, UTF-8, at most 4096 bytes) on the secret
   socket; the worker keeps its per-message SCM_CREDENTIALS check against the endorsed pid. PAM
   prompts come back the same way (the worker's existing framing, extended to carry the channel id
   and digests so the translation to S1 is exact). After the PAM conversation consent sends the
   bound kind-2 Confirm (or kind-3 Cancel) to the broker on the duplex channel. The account the
   verifier finally authenticated (canonical, from S4) is shown back for confirmation if it differs
   from what was typed.
5. **Hygiene in consent.** Mark consent's Unix process non-dumpable (wexhost call), block
   clipboard copy/cut and history on the password field, wipe every owned buffer (edit control,
   UTF-16, UTF-8, transport) on every exit. Kernel and PAM-module copies are out of our control and
   are not promised.
6. **Cleanup.** consent death, broker death, deadline, bus-name loss or verifier restart ends the
   transaction; the worker is reaped by its supervisor.

## Questions for review

* Is delivering the fd through a D-Bus call from consent's wexhost (pid checked against the
  server's endorsement and pidfd pin) sound, or should the server hand the verifier's fd to the
  consent process object directly?
* Is image identity of machine-root `consent.exe` plus "broker's own suspended child" enough for
  the endorsement, given same-UID native code is out of scope?
* Should the broker's duplex channel be two anonymous pipes or one private named-pipe instance?

Files that would change: `src/dlls/wexhost/e1s6.c` (+ a wexhost export for DialogAttach),
`wine/server/sysrun.c` and `named_pipe.c` (dialog endorsement), `src/programs/consent/consent.c`,
`src/programs/wexelevate/e1s18_broker.c`, `tools/e1s6/e1s6-lab.py`, `tools/e1s7/pam-worker.c` and
`protocol.h`, plus their tests.
