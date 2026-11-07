# E1 S12/S13 review findings to fix

Source: an independent read-only review, full text at
D:/DeepThought/ChromiumLab/guest-sysrole/s12-s13-review-antigravity.md (read it first). Verify each finding against
the current code before changing anything; some may be wrong or intended. Do not weaken any security check.

1. kernelbase/registry.c e1s_managed_effective treats any error status except STATUS_NOT_SUPPORTED as managed, so a
   transient failure (token open denied, no memory) breaks HKCU/HKCR for ordinary processes. Decide whether
   fail-closed is right for a process that really is managed (the server answers success with managed=1) and
   fail-open for errors that mean "not managed" (no token, server denied query), without opening a bypass.
2. Client hooks in kernelbase registry.c and ntdll reg.c call NtLoadWexProfile(WEX_HKU_QUERY) on every
   HKCU/HKCR access in every Wine process: one server round trip each. Remove the cost for unmanaged use
   (for example compile-time gate with the S13 define as for the server side, or a cached per-process
   answer that is invalidated safely when the effective token changes). Keep the S13 test build working.
3. kernelbase e1s_special_root asks for MAXIMUM_ALLOWED but the server denies MAXIMUM_ALLOWED unless the caller token
   is high integrity, so medium or low integrity managed callers lose HKCU. Check and fix by granting a bounded
   access mask that is safe for the caller.
4. server open_fd calls e1s_profile_check_parent, which climbs to / on every file open once any profile is loaded.
   Bound it (cache the verified directory, or check only inside the profile roots).
5. kernelbase e1s_root_cache keeps raw handles forever; S14 adds invalidation, so only record the limit in the
   comment unless a simple fix exists.

Deliver: working tree edits only (no commit), docs/STOCK_CHANGES.md and docs/E1_S13_HKU_LOAD.md updated, and a
final report stating which findings were real, what changed, what is unverified. You cannot build or run;
review statically with great care. No C escapes through shell heredocs.
