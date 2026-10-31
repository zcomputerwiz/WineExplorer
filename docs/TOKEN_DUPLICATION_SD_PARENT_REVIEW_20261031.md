# Token duplication descriptor review — 2026-10-31

## Scope and verdict

Read-only review requested by coordinator copilot-42440f04-d55e-4c37-835f-b1a8ca85e23e, msg_1793480728425_4f1e1d12. Inspected actual f2e67abe diff, existing ce87bac0 Wine tests and Microsoft API documentation. No native execution, VM or implementation was performed.

Do not accept the blanket token object descriptor copy introduced by f2e67abe in token_duplicate when sd is NULL. This copies OWNER/GROUP/DACL/SACL from the source object. Native DuplicateTokenEx with NULL security attributes creates a default object descriptor, not a copy of the source object's custom DACL. This finding does not reject the separate AllApplicationPackages recognition or default-DACL user-SID changes; those require independent focused validation.

## Verified references

- Microsoft DuplicateTokenEx documentation, lpTokenAttributes: NULL gives a default security descriptor and a non-inheritable handle. Desired-access checking is separately against the source token DACL. https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-duplicatetokenex
- ce87bac0 dlls/advapi32/tests/security.c:test_token_security_descriptor, lines7211 onward. Lines7240-7279 create token2 with one marker ACE for S-1-5-6 through explicit SECURITY_ATTRIBUTES and verify it. Lines7283-7313 duplicate token2 with NULL attributes, inspect token3 OBJECT DACL and assert that marker ACE was not inherited. Upstream test origin 4bd10fbd1a77c065f94956d1aab64eab47aa1801, June2017.
- Same file test_token_label, lines7151-7186: primary and impersonation duplicates preserve the source integrity level; an explicitly supplied lower label in SECURITY_ATTRIBUTES is ignored in this test. Thus source integrity propagation is not evidence that the entire source SD should be copied.
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
