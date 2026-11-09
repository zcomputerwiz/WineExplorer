# E1-S18e R8 step 3: server endorsement of the dialog (implementation plan)

Contract: docs/E1_S18E_R8_SPEC.md v5.1 section 3. Careful step (Claude), reviewed by Codex before it is
called done. Everything is inside the existing system-role gate (`system_role_enabled()`); without a
machine root the server behaves as stock Wine. The standard-user chain stays disabled.

## Dependency found while planning

Section 3's endorsement checks "D's desktop is the object behind `desktop_route`" and "the caller's
retained S3 objects and versions match", and the tuple carries the six generations. The server does not
issue those authorities yet: docs/E1_S1_CONTRACTS.md record 1 states that H2/S3 returns the retained
opener/writer objects but not the server-issued opaque IDs, `desktop_route`, `prefix_id`, endorsed host
process or generations. The broker's R8 context (WO-20) is zero for exactly this reason and denies.
So the endorsement request (3d) needs a caller-binding authority first (3e below), which is design
work to agree with Codex. Steps 3a-3c do not depend on it.

## Order

* **3a Creator and protection from creation.** `new_process` records the creating process (retained
  reference, released on exit). For a process created by the verified broker with an explicit
  handle list: check the list is exactly one `FILE_GENERIC_READ` pipe read end and one
  `FILE_GENERIC_WRITE` pipe write end of two distinct anonymous pipes, and that no process other than
  the broker holds a handle to any of the four pipe objects (one scan of all handle tables); else deny
  the creation. While protected, refuse to every process other than B and D: opening D or its
  threads, duplicating handles from or to D, duplicating or opening any of the four pipe objects into
  another process (SYSTEM included), and inheritance of them by a further child of B or D. Files:
  process.c/.h, handle.c, named_pipe.c, thread.c. Tests in scripts/test_sysrun_server.sh with probe
  modes (a second SYSTEM process tries each access).
* **3b Kernel pin from D's loader.** For a protected process `new_process` sets and checks
  `SO_PASSCRED` and `SO_PASSPIDFD` on the server end of the process socket before replying (failure
  denies). `receive_fd` for such a process requires exactly one SCM_RIGHTS fd, one SCM_CREDENTIALS and
  one SCM_PIDFD, no other ancillary data, `MSG_CTRUNC` clear; the first valid message's pid, uid and
  pidfd become the pin; later messages must match the same live process, else the process is killed.
  Files: process.c, request.c. Tests: a pin exists after start; the pinned pid equals the real child;
  a fake extra fd or missing credentials kills the process.
* **3c Dialog image recognition.** `sysrun_main_image` decides once, on the first executable image,
  whether a protected process maps machine-root `system32/consent.exe` or `syswow64/consent.exe`
  (device and inode), like the broker recognition. Files: sysrun.c/.h, process.h.
* **3e Caller-binding authority (design with Codex first).** The server issues and retains the S1
  record 1 identities for a request: opaque IDs for the retained opener/writer and host process,
  `desktop_route` for the caller's desktop object, `prefix_id`, and the generations it owns
  (`server_gen`, `root_gen`, `broker_gen` registration), with object comparisons, never IDs alone.
* **3d Endorsement request and rechecks.** A new op of a private server request (B -> S, D's process
  handle with PROCESS_QUERY_LIMITED_INFORMATION in B's table, the 204-byte pre-endorsement tuple):
  the section 3 checks, `dialog_id` assignment, retained D process and token objects, ENDORSED with
  D's pidfd to V on the S6 control socket, and the recheck op used at attach, READY, RESULT, Confirm
  and redemption. Uses 3a-3c and 3e.

Each sub-step: build in the wexbuild lab tree, run the S6/S13/sysrun suites, a negative control, commit
(Wine submodule first), and list the stock changes in docs/STOCK_CHANGES.md.
