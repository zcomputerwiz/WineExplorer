<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# An open Software Protection service (sppsvc): research, step 1 (2026-11-15)

Goal (owner): an honest, open source replacement for Windows' `sppsvc`, so that Office 2021 and 2024 (which need
Windows 10 or later and therefore Windows' licensing service, not Office's own `osppsvc`) can be licensed on
WineExplorer. Rules: clean room. Public documentation and observation of the public API only; no disassembly or
decompilation of Microsoft binaries, no key extraction, no capture of session keys, nothing that makes a
non-genuine service pass a check. Microsoft data stays out of the repository (`docs/ASSETS.md`).

**First question:** will the project be blocked by private keys, or is everything it needs public?

## Answer so far

**Probably blocked at one point, not yet proven.** Almost everything a licensing service does is public or can be
done honestly. The exception is the authentication step by which Office decides that the service answering it is
genuine; by design it needs a secret that only Microsoft's service holds. An open service cannot hold that
secret, and the project will not extract it. That step cannot be settled from public sources alone (the format is
not documented), so the finding below is an inference, and the decision it leads to is the owner's.

## What is public

| Item | Source | Use for an open service |
|---|---|---|
| The client API (`slpublic.h`): functions, error codes, structures | Microsoft Learn, "slpublic.h header" and each function page | The interface our `sppc.dll` already implements; checked against Windows 11 with `scripts/lab/office/sppc-probe.c` |
| `SLSetAuthenticationData` / `SLGetAuthenticationResult` semantics: documented errors `SL_E_AUTHN_WRONG_VERSION` (0xC004F077, "the security version is wrong"), `SL_E_AUTHN_MISMATCHED_KEY` (0xC004F078, "the key used in SLSetAuthenticationData is incorrect"), `SL_E_AUTHN_CANT_VERIFY` (0xC004F07A), `SL_E_AUTHN_CHALLENGE_NOT_SET` (0xC004F079); available from Windows 8 | Microsoft Learn | Shows that the pair is a keyed challenge with a security version; says nothing on the format or the algorithm |
| Activation models: MAK (activates online with Microsoft), KMS (clients activate against a KMS host on the organization's network, threshold of 25 clients or 5 servers, RPC over TCP), Active Directory based activation (the OS checks digital signatures in an object stored in the domain) | Microsoft Learn, volume activation guides | The honest activation paths. KMS needs a KMS host that was itself activated with a Customer Specific Volume License Key from Microsoft; clients trust that host |
| KMS client setup keys (GVLKs) | Microsoft Learn | Public by design: they select KMS client mode and activate nothing without a KMS host |
| Store locations and the repair routine: `%SystemRoot%\System32\spp\store\(2.0\)\tokens.dat`, rebuild with `slmgr /rilc` | Microsoft KB 2736303 | Where the host keeps its state |
| License files (`.xrm-ms`) are XML (XrML) signed by Microsoft; clients verify them against pinned public roots | Our own reading of the files on the Office media | An open service stores and serves them and verifies them with public certificates; it never signs one |
| Product key format and checksum (pkey2009); key configuration files | Our own work (`src/dlls/pidgenx`) | Done |
| Host facts: service `sppsvc` (NetworkService, delayed automatic start), plugin pair `sppwinob`, `sppobjs`, store directories `plugin-manifests-signed`, `store`, `tokens`, registry keys `SoftwareProtectionPlatform\{Activation, GenuineApps, PayloadOverride, Plugins, ReferralData, Setup}` | Read-only inventory of the Windows 11 host, 2026-11-15 | Shape of what to build |

## What is not public

- The format and the cryptography of the authentication data and result.
- The RPC/ALPC interface between `sppc.dll` and `sppsvc.exe`, and the contract between `sppsvc` and its plugins.
- The format of `tokens.dat` and of the protected store (on Windows 7 and earlier a kernel driver held it).

## Observation on the host (public API, made-up data)

`scripts/lab/office/sppc-auth-probe.c` against the host's own `sppc.dll` (Windows 11, 10.0.26100):

- `SLSetAuthenticationData` with no data returns S_OK; with any non-empty buffer of made-up bytes (sizes 1 to 512,
  first byte 0 to 3) it returns `ERROR_INVALID_DATA` (0x8007000D). So the client library checks a structure
  locally before anything goes to the service.
- `SLGetAuthenticationResult` returns `SL_E_AUTHN_CHALLENGE_NOT_SET` (0xC004F079) in every case.

This says the data has a defined structure and is validated on the client; it says nothing about keys. Finding the
structure by trial would be reverse engineering and was not attempted.

## From our own Office runs (Office 2016, Wine, 2026-11-14)

Word 2016 in Windows 10 mode (our `sppc` store) calls `SLSetAuthenticationData` with **288 bytes** after
generating a session key in-process, then asks for the result and stops with "cannot verify the license"
(OFFICE.md blocker 3). 288 bytes is what an RSA-2048 encrypted block (256 bytes) plus a 32-byte header would be;
that is an inference from the size, not a finding. If it holds, the genuine service decrypts the block with a
private key and answers with a value derived from the session key; Office compares. The check then proves that the
answer came from a service that holds Microsoft's private key, which is the purpose of such a check.

## What follows

An open `sppsvc` can:
- keep licenses, product keys and activation state, serve every documented `SL*` call, report the true state
  (grace, licensed after a real activation, notification);
- activate honestly: as a KMS client against the organization's own KMS host, through Active Directory based
  activation, or with a MAK through Microsoft's servers (all need only public protocol knowledge plus the
  organization's own genuine host or keys);
- leave Windows components that do not enforce the authentication step working.

It cannot, if the inference holds, produce the answer to Office's authentication challenge. Three honest ways on:

1. **Office 2016 in Windows 7 mode** with Office's own `osppsvc`, Microsoft's code running under Wine. Under way
   here (OFFICE_BUG_NOTES.md OB-1, OB-5). Does not reach Office 2021 and 2024.
2. **Microsoft's own `sppsvc` under Wine**, taken from a Windows installation the user has, as a downloaded
   component that never enters the repository (as the owner decided for the Calculator frameworks). Wine would have
   to provide what it needs: the service account, its store, the registry keys `HKLM\SYSTEM\WPA` and
   `SoftwareProtectionPlatform`, the hardware and TPM identifiers it reads. Feasibility is unknown and is the
   next thing to measure.
3. **An open service plus a published statement of what Office will not accept:** the project documents that
   Office 2021 and 2024 check the service's identity and that no open service can pass the check; applications
   that do not check work.

## Path 2 measured, first run (2026-11-15, lab VM, local copy never committed)

Files used: the host's `sppsvc.exe`, `sppc.dll`, `sppcext.dll`, `sppwinob.dll`, `sppobjs.dll`, `slc.dll`,
`sppcomapi.dll` and the two DLLs they import that Wine lacks, `pkeyhelper.dll` and `CLIPC.dll` (imports read from
the PE import tables only). No store, token or license of the host was copied.

* Imports: everything else the files import exists in Wine (Windows API sets included). `pkeyhelper.dll` imports
  `bcrypt` only, `CLIPC.dll` imports `ntdll` and `rpcrt4` only.
* `sppsvc.exe` loads and runs under Wine with the two DLLs (started by hand it exits as any service does outside the
  service manager).
* Registered as a session 0 service (`scripts/lab/office/spp-service-test.bat`, NetworkService) it enters
  START_PENDING and then **stops on `RPCRT4.dll.RpcServerInterfaceGroupCreateW`, which Wine does not implement**
  (an unimplemented-function abort; also seen: `NtQuerySystemInformation` class 0xb9 is a stub).
* `RpcServerInterfaceGroupCreate`, `...Activate`, `...Deactivate`, `...Close`, `...InqBindings` are public, documented
  rpcrt4 functions. Implementing them is ordinary Wine work from Microsoft's documentation and the next step.
* Not yet known: what the service does after that (store creation, plugin loading, hardware id, its RPC endpoint and
  whether the native `sppc.dll` can reach it from another session; OB-1 applies), and whether Windows-only
  components (the `spsys` driver was merged into the service from Windows 8 on) block it.

Verdict so far for path 2: **no hard blocker found yet**; the first one is a missing public RPC function.

## Next steps

1. Look for public protocol documentation that states the authentication scheme (Microsoft Open Specifications,
   Windows Protocols, papers). If it is documented and uses published keys, the inference is wrong and path 3
   disappears. Searches so far found none.
2. Measure path 2 on the lab VM with a copy of the host's `sppsvc` and plugins used locally (never committed):
   does it start under Wine, what does it ask of the system.
3. Write the specification of the parts an open service does provide (the `SL*` behaviours already recorded, the
   store, grace, KMS client), as the base of an implementation whatever the answer on authentication is.

## Evidence

`scripts/lab/office/sppc-auth-probe.c` (this note), `sppc-probe.c` (earlier). Sources: Microsoft Learn pages for
`SLSetAuthenticationData`, `SLGetAuthenticationResult`, the volume activation guides and KMS planning, Microsoft
KB 2736303.
