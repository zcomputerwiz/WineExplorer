# E1-S18e R13 adversarial review

Static review of Wine commit b5da51aff3aae55ca4bb1208fd35e289bfe17d4b (checkout matches).
Read docs/TOOLING.md and CLAUDE.md first, then the prior design advice and latest decisions.
No code changes, builds, harnesses, services or VMs. The reported 306 passes and job negative
control are supplied historical evidence, not results independently reproduced here.

Verdict: the job/debugger removal is correctly placed, including bypassing the breakaway check
(wine/server/process.c:1261,1392,1445). An implicit rule is acceptable as a deliberate policy for
ALL verified-broker creations with explicit parent and token; a private flag is not intrinsically
safer. It does not implement the earlier, narrower server-authorized operation contract.
I would address findings 1-3 and obtain normal-server evidence before calling R13 closed.
No direct standard-user privilege escalation was established by this static review.

1. **Medium - the broker marker does not prove a unique, still-mapped main image.**
   Evidence: wine/server/mapping.c:423-439; wine/server/sysrun.c:322-326;
   wine/server/process.c:628; wine/server/mapping.c:1737-1743.
   sysrun_main_image has one call site: every SEC_IMAGE, non-DLL view added before STARTUP_DONE,
   not only the first executable. After initialization, additional images generate DLL events
   and do not call it. Before initialization, an already-SYSTEM service can map the broker as
   an additional executable image, acquire the marker, then map its own executable or unmap
   the broker. Production has no equivalent of e1s6_image_seen, and a successful marker is
   never cleared until the process object disappears. Thus "verified broker main image" is
   stronger than the implemented identity. This requires existing system-role authority;
   mapping the file alone does not give an ordinary process SYSTEM or the protected role.
   Fix: associate recognition with a single retained startup main-image view/file, record a
   negative first decision too, reject conflicting main-image registrations, and revoke the
   identity when that view is removed/replaced. Account explicitly for legitimate loader/WoW64
   mapping sequences. Test multiple executable views before init and unmap-after-recognition.

2. **Medium - the server does not enforce the no-impersonation part of the agreed gate.**
   Evidence: wine/server/process.c:1247-1256; wine/server/sysrun.c:365-384;
   wine/server/token.c:207-225,1235-1245.
   A broker thread with current->token set still qualifies: only its process role and primary
   token's SYSTEM user SID are checked. Assignment also checks the process primary, so it
   does not incidentally reject impersonation. Any valid parent/token pair qualifies, including
   a non-elevation helper or a SYSTEM target; there is no suspended-operation/request binding.
   The present shipping launch checks for no thread token immediately before creation
   (src/programs/wexelevate/main.c:395-401,1117), and validates the linked token/session
   (:1038-1063). Consequently this is a server-contract gap, not a demonstrated shipping
   impersonation exploit. Other broker creations are covered automatically by the broad rule;
   ordinary non-broker CreateProcess remains unchanged.
   Fix: reject matching broker requests while current->token is non-NULL, before allocation.
   Explicitly adopt/document the broad broker policy, or restore an operation discriminator
   bound to the retained caller, explicit target token and authorized suspended launch.
   A flag without those checks would not fix the authorization gap.

3. **Medium - production recognition and a 32-bit broker are not covered by the positives.**
   Evidence: wine/server/sysrun.c:295-300,324,368; scripts/test_sysrun_server.sh:677-682,
   702-711,749-751,958-970.
   Both caller ABIs run against one x86_64 wexelevate_test.exe. A lab server accepts that via
   e1s6_broker_image, independently of the new production marker. A normal server rejects
   that identity, yet the new positive job/debug cases are unconditional: the normal-server
   suite's job-survival case will retain the old inheritance and fail for this fixture layout.
   Separately, a distinct syswow64/wexelevate.exe inode is not recognized. If started by SCM
   with a SYSTEM token, it can create the elevated child without isolation; if root-started
   through the launcher, it also misses the existing system_images allowlist. A 32-bit file
   placed at the canonical system32 path would match: this is a path/inode issue, not an ABI
   truncation. The default native service path is wine/loader/wine.inf.in:1200; this review
   does not establish that a syswow64 broker is a supported deployed configuration.
   Fix: test a normal server using a disposable fixture at the production canonical image
   path, keeping lab-name exclusion as a separate negative. Test an actual i686 broker;
   either recognize its separately trusted installed inode, or enforce/document native-only
   broker startup and deny unsupported broker configurations rather than silently inheriting.

4. **Low - "placement only" overstates what remains inherited.**
   Evidence: wine/server/process.c:720-729,1418-1433,1451-1454;
   wine/server/winstation.c:517-552.
   Affinity is copied from the caller (a creation-time scheduling constraint, not continuing
   job/debug authority). Window station and, absent a named desktop, caller desktop handles
   are duplicated. Process group is copied only if supplied group_id equals the caller's;
   otherwise the child's fresh group is used. General handle inheritance is refused, but
   positive console and arbitrary standard-handle slots can still be duplicated with the
   caller handle's access; isolate does not restrict their object types or provenance.
   The shipping call uses CREATE_NEW_CONSOLE, no STARTF_USESTDHANDLES and zeroed startup info
   (src/programs/wexelevate/main.c:1018,1106,1120-1123). Kernelbase consequently requests a
   fresh console and supplies no standard handles (wine/dlls/kernelbase/process.c:197-218).
   Its group input comes from the broker (:192-193), not automatically from the caller.
   Thus I found no current shipping caller-console/std-handle control import here; caller
   window routing is intentional. Fix: reset affinity to an approved broker/system default
   if "placement only" is literal, and constrain isolated startup to fresh console/empty std
   slots, or an explicit validated console-resource allowlist. Document permitted desktop
   routing and group policy; do not describe this predicate as complete parent isolation.

5. **Medium - tests do not yet establish the debugger property or denial boundaries fully.**
   Evidence: scripts/wexelevate_probe.c:1188-1224,1264-1303;
   scripts/test_sysrun_server.sh:958-970; wine/server/process.c:1249-1255.
   The job survival check plus reported disabled-isolation control is useful. The debugger
   test continues events and checks the returned child PID, but lacks a negative control;
   WaitForDebugEvent(INFINITE) also relies on an outer timeout. It does not verify debugger
   kill-on-exit isolation or the three new incompatible-input refusal paths.
   Fix: disable only debugger isolation in a disposable test build and require this case to
   detect the child event/fail; add bounded event waits, an ordinary debug-child positive
   control, and debugger-exit-with-kill-on-exit plus elevated-child survival. Exercise nested
   jobs, actual last-job-handle close, breakaway flags, explicit debug object/job list/handle
   inheritance denials, impersonation and primary-token changes, with no child/handle leaks.
   Add production-vs-lab identity negatives, second-image/unmap tests, and console/group/
   affinity/desktop assertions for both caller ABIs. Give each denial a distinct lab audit
   site so a generic setup failure cannot satisfy a targeted negative.

Identity limits: inode equality intentionally accepts a hard link or renamed alias of the same
file while the canonical path still resolves to that inode; a same-name copy does not match.
Replacing the canonical file after recognition does not clear the bit; replacement before a
fresh recognition makes an old mapping fail equality. In-place writes preserve inode identity:
this is not a content-integrity check. These depend on trusted machine-root writers (see
scripts/setup-machine-root.sh:342-360), not merely a trusted basename. A legitimate atomic
upgrade need not revoke a running immutable old broker solely because its pathname changed.
Primary replacement does not clear the bit (wine/server/process.c:1840-1841), but a non-SYSTEM
token makes sysrun_process_is_broker false; restoring a SYSTEM-user token re-enables it without
reverification. Pin/invalidate the original primary/security version if that stronger identity
is intended. The lab alias is compiled out normally and must remain so. All proposed probes
above, including normal-build/real-broker/32-bit-broker validation, remain unrun here.
