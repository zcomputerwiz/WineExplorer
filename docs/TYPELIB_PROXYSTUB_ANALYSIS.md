<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chrome updater typelib proxy/stub analysis

Static analysis, 2026-11-05. Local Wine revision `f3463b6d257277da9d837feac2d7b24e256bf974`. No guest binary, embedded type library, trace with descriptor details, or native-Windows test was examined. **The exact unsupported type, and therefore the exact minimal patch, cannot be determined from the two errors and registry entries alone.** The evidence establishes that stub creation failed, not that non-dual `IUnknown` interfaces are categorically unimplemented. A test with only the ordinary shape below is not established to fail in this tree.

## Failure path and possible sources

* `wine/dlls/combase/marshal.c:899-905` obtains the IID's factory, calls `IPSFactoryBuffer_CreateStub`, and prints the second error when it fails. In this tree the COM marshalling code is in combase, not ole32. The client-side `E_NOINTERFACE` and MSI 1627 are observed downstream symptoms; those logs alone do not prove the HRESULT transformation or exclude another activation problem.
* `wine/dlls/oleaut32/oleaut.c:894-921` loads the registered typelib and gets the IID's `ITypeInfo`. `dispatch_typelib_ps_CreateStub` at `oleaut.c:1114-1144` logs the first error **after** `CreateStubFromTypeInfo` or `dispinterface_create_stub`; it contains no explicit `E_NOTIMPL`. The error log occurs after typeinfo lookup and `GetTypeAttr` succeed. For `TKIND_INTERFACE` or `TYPEFLAG_FDUAL` it calls rpcrt4; other types go to the dispinterface stub. Therefore this log does not itself prove which callee returned the code.
* `wine/dlls/rpcrt4/ndr_typelib.c:1690-1704,1716-1722,1745-1754` propagates errors from interface metadata, format generation and stub initialization. In format generation, `get_param_pointer_info` returns `E_NOTIMPL` for an unhandled referenced `TYPEKIND` at `ndr_typelib.c:942-972`; `get_param_info` does so for an unhandled referenced kind at `1024-1052` or an unhandled direct `VARTYPE` at `1058-1065`. `write_iface_fs` visits every method parameter and return descriptor (`1322-1346`), and `build_format_strings` propagates failure (`1352-1364`). Other called COM/typeinfo and parent-stub implementations can also return errors; trace the actual return before concluding that one of these branches fired. An unhandled nested type can alternatively produce a FIXME and placeholder format rather than cleanly returning `E_NOTIMPL` (`800-848`).
* `wine/dlls/oleaut32/typelib.c:5923-5953,6074-6102` transforms an HRESULT return into `VT_VOID` only for the dispatch view of an interface-based method. `get_iface_info` retrieves the underlying vtable typeinfo for a dual dispatch view (`ndr_typelib.c:1409-1428`). The unrelated `userdefined_to_variantvt` used in Automation invocation has `E_NOTIMPL` cases for records/unions (`typelib.c:6949-7007`); its existence does not establish that this stub-generation path calls it.

## Supported shapes and updater requirements

The factory accepts `TKIND_INTERFACE` irrespective of the dual flag (`oleaut.c:1134-1137`); the rpcrt4 helper obtains parent IID and inherited method count (`ndr_typelib.c:1444-1466`), and `IUnknown`-parent stubs use the ordinary stub rather than a delegated parent stub (`1731-1743`). Dual interfaces have explicit handling, not a blanket rejection. This is implementation intent, not proof that all calls work.

HRESULT/`VT_HRESULT` maps to `FC_LONG` (`ndr_typelib.c:109-145`); direct enum and alias-to-enum handling appears at `133-140,1024-1047`, with pointer cases at `942-968`. `VT_PTR` to a user-defined interface is recognized (`219-241,927-936`) and the interface IID is emitted as an interface pointer (`800-830`); in/out flags are processed at `1072-1095`. BSTR, VARIANT, SAFEARRAY and scalar paths also exist (`96-105,1010-1023`). These are not blanket `E_NOTIMPL` cases. Aliases, pointer indirection, arrays, records, ABI and ownership still require real round-trip testing; presence of a case is not proof of correctness. Unhandled direct VARTYPEs such as `VT_VOID` hit the default in `get_param_info`; unhandled referenced kinds such as `TKIND_UNION` hit the referenced-kind default. Neither is established as present in the guest's failing descriptor.

The current upstream [Chromium internal IDL template](https://chromium.googlesource.com/chromium/src/+/main/chrome/updater/app/server/win/updater_internal_idl.template) declares `object, oleautomation` interfaces derived from `IUnknown`, not dual: `IUpdaterInternal` has `HRESULT Run` and `Hello` with `[in] IUpdaterInternalCallback *`, and the callback has `HRESULT Run([in] LONG result)` (template lines 27-36, 49-59). Its generated system names and UUIDs depend on build flavor/version. This is a useful model, **not** proof of the exact shipped Google TLB or an assertion that the given CLSID implements this IID. Other updater interfaces may use BSTR, integers/enums, interface pointers, `[out]` and `[out, retval]`; inspect that exact TLB before assigning those to this IID. A normal TLB representation likely includes `VT_HRESULT`, `VT_PTR`/`VT_USERDEFINED` and `VT_I4`, but actual `FUNCDESC` values must be measured.

## Comparison and next change

[Upstream Wine master rpcrt4](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/rpcrt4/ndr_typelib.c) has the same relevant `get_param_info` supported kinds and `E_NOTIMPL` defaults (retrieved 2026-11-05); [upstream oleaut32](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/oleaut32/oleaut.c) uses the same interface/dual dispatch at its `dispatch_typelib_ps_CreateStub`. This is a focused comparison, not a full diff or a claim that upstream is identical. `docs/STAGING_OVERLAP.md:5-9` describes staging checkout `605e214`; the available `build/review/wine-staging/` is a selected patch export, not a full checkout, and `/mnt` is inaccessible/nonexistent in this environment. Its index lists an `oleaut32_typelib_dispatch` patch (`build/review/wine-staging-index.txt:228-230`), but that patch is not in the selected export, so its content and applicability cannot be assessed. No relevant rpcrt4/typelib marshaller patch was found in the available exported files; this does not rule out one in full staging.

First dump the guest's exact `updater.exe\5` with `LoadTypeLib`, `GetTypeInfoOfGuid`, `GetTypeAttr`, `GetFuncDesc`, and recursively `GetRefTypeInfo`/`GetTypeAttr` for each parameter, return and alias, including parent IID, flags, typelib syskind and offsets. Capture `+ole` trace including the `Writing procedure`, `Writing parameter`, `vt` and `unhandled type/kind` messages (`ndr_typelib.c:1006,1063,1320-1335`) and, if necessary, instrument HRESULT propagation at `oleaut.c:1134-1140` and `ndr_typelib.c:1702-1718`. Compare the same descriptors with native Windows. Preserve the actual failing method, VARTYPE, referenced kind and nesting.

The minimal change is conditional on that finding: if format generation rejects a valid Automation-compatible descriptor, extend `wine/dlls/rpcrt4/ndr_typelib.c`'s `get_param_info` or `get_param_pointer_info` and, as needed, `write_type_tfs`/pointer format generation to emit the correct NDR representation, stack layout, ownership and in/out flags for that descriptor on both proxy and stub. If Wine's `ITypeInfo` supplies the wrong descriptor compared with Windows, fix its decoding or dispatch/vtable selection in `wine/dlls/oleaut32/typelib.c` or `get_iface_info` instead. If failure comes from parent stub creation, fix that concrete path. Do not special-case this IID, force `IDispatch`, or map arbitrary unsupported descriptors to success. No one-line fix is justified by the available evidence.

## Regression test design

Extend `wine/dlls/oleaut32/tests/tmarshal.c:2925-2966` (existing cross-apartment host/marshal pattern), using typelib creation examples at `wine/dlls/oleaut32/tests/typelib.c:2269,2355,3340` and per-user registration/cleanup at `6960-7093`. Use `CreateTypeLib2` for the process SYSKIND and unique GUIDs. Define non-dual `TKIND_INTERFACE` types with `TYPEFLAG_FOLEAUTOMATION`, explicit `IUnknown` bases, correctly indexed vtable slots and `FUNCDESC`s: callback `HRESULT Run([in] LONG)` and server `HRESULT Run([in] ICallback *)` and `Hello`. Add the **observed failing descriptor** as a separate minimal method, preserving flags, aliases and nesting. Save, reload and register per-user; ensure the interface's `ProxyStubClsid32` and TypeLib/version resolve to PSOAInterface, using disposable HKCU keys if registration does not create them. Verify metadata and `CoGetPSClsid`. Restore/remove only test-owned keys and file.

Marshal an implementation from a pumped STA to a second apartment with `CoMarshalInterThreadInterfaceInStream`/`CoGetInterfaceAndReleaseStream`; check marshal and unmarshal HRESULTs, call both methods, pass a callback and verify its `LONG` and HRESULT across the boundary, references and cleanup. For any observed `[out]`/`[retval]` shape, assert output values and ownership. Include dual and simple non-dual controls. Run on native Windows first to validate the generated typelib and expected results, then on current 32-bit Wine and other supported bitnesses. Failure on today's Wine and success on Windows are **test goals, not verified results**: the ordinary callback shape is already represented in Wine's code, and until the guest descriptor/failure branch is known there is no defensible promise that this test reproduces the error.

## LPWSTR test status (2026-11-05)

Added `test_marshal_string_args` to `wine/dlls/oleaut32/tests/tmarshal.c` and the method `string_args` (`[in] LPWSTR`, `[in] LPSTR`
between integer arguments, `[out, retval] int`) to `tmarshal.idl`. It runs through the real apartment proxy with a nonempty, an empty
and a non-ASCII wide string. Wine result: 775 tests, 0 failures on i386 and x86_64. Native Windows 11 verification is still pending: run
the same test there before relying on it. Note for authors: the IDL must spell the types `LPWSTR` and `LPSTR`. A plain
`[in, string] wchar_t *` becomes a pointer to a 16-bit integer in the type library and marshals one character only (the first Wine run
failed in exactly that way).

## SetBlanket test status (2026-11-05)

Added `test_proxy_blanket` to `wine/dlls/ole32/tests/marshal.c` (registered after `test_client_security` in `START_TEST(marshal)`).
It cross-apartment-marshals `Test_ClassFactory` (`IID_IClassFactory`) with the existing `start_host_object` pattern, then on the
standard-marshal proxy: (1) `CoQueryProxyBlanket` before any set — no hard assertion, result recorded via `trace` (returned
`E_NOTIMPL` 0x80004001, no crash); (2) `CoSetProxyBlanket(RPC_C_AUTHN_DEFAULT, RPC_C_AUTHZ_DEFAULT, COLE_DEFAULT_PRINCIPAL,
RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE)` asserted `S_OK`; (3) `CoQueryProxyBlanket` afterwards
asserted `S_OK` with `authn_level == RPC_C_AUTHN_LEVEL_PKT_PRIVACY` and `imp_level == RPC_C_IMP_LEVEL_IMPERSONATE` (both marked
"verify natively").

Wine result (harness `wexdev.sh test ole32 marshal`, 2026-11-05): x86_64 1249 tests executed, 2 failures; i386 1252 tests executed,
2 failures. All of the new test's own assertions passed on both architectures. The harness test step failed (exit 2) with exactly
these two lines on both architectures; both are in the pre-existing `test_client_security`, whose `todo_wine` markers are now stale
because the combase `SetBlanket`/`QueryBlanket` fix makes those calls succeed:

* `marshal.c:3910: Test succeeded inside todo block: "IClientSecurity_SetBlanket" failed with error 00000000`
* `marshal.c:3924: Test succeeded inside todo block: "IClientSecurity_QueryBlanket(IUnknown)" failed with error 00000000`

Removing those two `todo_wine` markers is the follow-up to make the suite green. 64-bit test exe for native Windows verification:
`/root/WE-prefetch/build/wow64/dlls/ole32/tests/x86_64-windows/ole32_test.exe` (run as `ole32_test.exe marshal`; Wine builds one test
exe per module, so there is no separate `marshal_test.exe`).

Update 2026-11-06: the two stale `todo_wine` markers are removed (plus the two for an unset `QueryBlanket`), the unset defaults follow the
native values (WINNT, authz none, PKT_PRIVACY, IDENTIFY; measured with `test_proxy_blanket`), and the whole `marshal` test passes on
Windows 11 natively (1174 tests, 0 failures, our own ole32_test.exe) and in Wine (1249 and 1252 tests, 0 failures).
