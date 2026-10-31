# Token duplication descriptor review — 2026-10-31

## Scope and verdict

Read-only review requested by coordinator copilot-42440f04-d55e-4c37-835f-b1a8ca85e23e, msg_1793480728425_4f1e1d12. Inspected actual f2e67abe diff, existing ce87bac0 Wine tests and Microsoft API documentation. No native execution, VM or implementation was performed.

Do not accept the blanket token object descriptor copy introduced by f2e67abe in token_duplicate when sd is NULL. This copies OWNER/GROUP/DACL/SACL from the source object. Native DuplicateTokenEx with NULL security attributes creates a default object descriptor, not a copy of the source object's custom DACL. This finding does not reject the separate AllApplicationPackages recognition or default-DACL user-SID changes; those require independent focused validation.

## Verified references

- Microsoft DuplicateTokenEx documentation, lpTokenAttributes: NULL gives a default security descriptor and a non-inheritable handle. Desired-access checking is separately against the source token DACL. https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-duplicatetokenex
- ce87bac0 dlls/advapi32/tests/security.c:test_token_security_descriptor, lines7211 onward. Lines7240-7279 create token2 with one marker ACE for S-1-5-6 through explicit SECURITY_ATTRIBUTES and verify it. Lines7283-7313 duplicate token2 with NULL attributes, inspect token3 OBJECT DACL and assert that marker ACE was not inherited. Upstream test origin 4bd10fbd1a77c065f94956d1aab64eab47aa1801, June2017.
- Same file test_token_label, lines7151-7186: primary and impersonation duplicates preserve the source integrity level; the test checks that an explicitly supplied lower object label does not lower TokenIntegrityLevel. It does not establish that the object mandatory label is ignored. Thus source integrity propagation is not evidence that the entire source SD should be copied.
- TokenDefaultDacl is token state used in object creation; GetKernelObjectSecurity retrieves the token object's descriptor. They must be recorded independently.

## Small native/Wine regression matrix

Use one original bounded PE and disposable duplicate tokens; never modify current process token, prefix-global registry or host objects. Record API return/error, descriptor components, effective process/thread tokens, TokenDefaultDacl, TokenIntegrityLevel, TokenType and handle inheritance. Compare same PE on native Windows and each internally matched Wine candidate.

1. Existing conformance reproduction: explicit SA creates token T with distinctive object DACL marker ACE (use existing test's service SID and flags), verify marker installed; duplicate T with NULL SA into an impersonation token, assert source marker absent from duplicate object DACL. Repeat for primary token. Keep necessary initial handles for inspection; do not confuse already granted handle access with a fresh open.
2. Repeat with a SECURITY_ATTRIBUTES structure whose lpSecurityDescriptor is NULL; measure native results for comparison with case1 and handle inheritance true/false separately. Do not assert unmeasured owner/group defaults.
3. Supply a second explicit DACL marker in SA; assert the duplicate honors that supplied marker instead of copying the source marker, using existing assertions. Query OWNER/GROUP separately; custom owner requests may require SeRestorePrivilege and need a controlled expected-denial case rather than elevation.
4. Put a different marker in T's TokenDefaultDacl, independent from its object DACL. Duplicate and record both. Establish native expectations before asserting exact default owner/group or ACL layout; source default-token state and new object security are distinct.
5. Reuse existing label tests: source integrity persists with NULL and explicit SA, including an explicit SD containing a conflicting low label. Inspect LABEL_SECURITY_INFORMATION without conflating it with arbitrary SACL auditing ACEs; avoid privilege-dependent full-SACL claims.

Run existing test_token_security_descriptor and test_token_label against each candidate and report actual assertion identities, not merely suite totals. If restricted/impersonating creator context matters for the Chromium case, add only that focused context after the basic cases and record the effective token that supplies object defaults.

## Fix boundary

Restore native default-object-descriptor behavior for public duplication and preserve source token integrity as covered by existing tests. If lowbox creation, process token assignment, or Chromium's explicit descriptor rewrite needs preservation at a different boundary, identify that exact API/call path and test it rather than modifying all duplication calls. Native self query/adjust success alone does not authorize blanket self-token access or relaxed restriction checks. This review provides source/contract evidence; it does not establish the real Chromium timeout cause.

## Follow-up review: native oracle and rollback

Reviewed actual published root 5cf9cdd706b35a6fcd5b6841072eaf38219d58eb and Wine e1a90b9430bc664a9ba21549cff6b13c1510d2ad following msg_1793482516051_3113abc5. Verified remote branch tips. Read the entire 308-line disposable-token probe, both committed native logs, retained guest inspection/wineboot logs, and the production diff. Direct function comparison confirms token_duplicate is identical to ce87bac0; the separate AllApplicationPackages and user-SID default-DACL changes remain.

Independently compiled the committed probe on WSL Debian with both MinGW compilers (-O2 -Wall -Wextra, -ladvapi32), without compiler diagnostics, then executed both PEs on the Windows host. Each exited 0 with 114 unique passing assertions, 11 observed cases and no errors. Parent outputs are preserved in D:/DeepThought/ChromiumLab/token-sd-parent-review-DO1AHW/native-x64.log and native-x86.log. Only disposable duplicated token objects were modified; no prefix, registry or current-process token changes.

Accepted as a scoped native oracle: NULL attributes and attributes with NULL descriptor omit the source custom object-DACL marker; explicit descriptors honor their alternate marker; TokenDefaultDacl marker propagates separately as token state. An explicit low OBJECT mandatory label is honored while TokenIntegrityLevel stays Medium; subsequent NULL-attributes duplication returns a Medium object label and Medium token integrity. This corrects the earlier review wording about an ignored lower label. Owner/group presence was tested, not exact creator-dependent defaults or restricted-creator behavior; object DACL markers do not prove effective access enforcement.

Accept e1a90b94 as removal of the rejected blanket-copy experiment and baseline restoration only. It is not full descriptor conformance or Chromium acceptance. No guest execution or Wine matrix was independently rerun here. In particular the restored source-object-label propagation can differ from the native low-object-label case, and remaining SID/default-DACL changes need focused actual Wine/native comparison. Do not block browser progress on unrelated complete security conformance.

Next critical path: repair the guest execution prerequisite, not another speculative token rewrite. Retained r4 evidence shows DISPLAY empty and no active graphical session alongside window/COM startup errors. That is a concrete environment difference, not proof of the complete timeout cause. Inspect the previously successful browser adapter and restore its existing guest user/session/display/runtime environment using the installed stack, with verified socket/access readiness, rather than assuming host Xvfb supplies a guest display or installing a new stack. Then use one fresh owned prefix, bounded successful initialization and minimal x64/x86 startup checks before corrected Mojo/descriptor comparison. Capture the precise remaining wait/dependency if it still blocks. Preserve matched runtime identity, free-space capacity, previous evidence and exact-owned cleanup. Once probes support the relevant fix, resume the authorized normal Chromium comparison; Edge follows Chromium validation.
