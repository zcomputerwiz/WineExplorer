<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# What an open Software Protection service has to do (specification, first draft, 2026-11-15)

Clean room: built from Microsoft's public documentation, from calling the public API on Windows 11 (read-only
probes, `scripts/lab/office/sppc-probe.c`) and from what Office's setup and applications ask of the client
(Wine traces of our own runs). Nothing here comes from disassembly or from third-party reverse-engineering
material. See [SPPSVC_RESEARCH.md](SPPSVC_RESEARCH.md) for the limits (the authentication step).

Work on this specification is stopped (SPPSVC_RESEARCH.md, status).

Status of each item: **done** (in `src/dlls/sppc`, the in-process store), **to do**, **out of scope**.

## 1. Interface (client API `slpublic.h`)

| Group | Calls | Status |
|---|---|---|
| Context | `SLOpen`, `SLClose` | done |
| Licenses | `SLInstallLicense`, `SLUninstallLicense`, `SLGetLicense`, `SLGetLicenseFileId`, `SLGetLicenseInformation`, `SLGetSLIDList` (the id-type pairs that Windows 11 answers), `SLGetProductSkuInformation`, `SLGetApplicationInformation` | done; error codes as Windows 11 |
| Keys | `SLInstallProofOfPurchase`, `SLUninstallProofOfPurchase`, `SLGetPKeyId`, `SLGetPKeyInformation`, `SLGetInstalledProductKeyIds`, `SLSetCurrentProductKey` | done (key identified with `pidgenx`) |
| State | `SLGetLicensingStatusInformation`, `SLConsumeRight`, `SLGetPolicyInformation(DWORD)`, `SLLoadApplicationPolicies`, `SLGetApplicationPolicy`, `SLUnloadApplicationPolicies`, `SLPersistApplicationPolicies` | done; state is the true grace period of an installed key (30 days, constant), then notification |
| Service info | `SLGetServiceInformation` (`Version`) | done for `Version`; `ActivePlugins` to do |
| Events | `SLRegisterEvent`, `SLUnregisterEvent`, `SLFireEvent` | accepted, never signalled: to do |
| Authentication | `SLSetAuthenticationData`, `SLGetAuthenticationResult` | **out of scope**: needs the genuine service's secret (research note); returns "not implemented" |
| Activation | `SLActivateProduct` family, offline ids (`SLGenerateOfflineInstallationId`, `SLDepositOfflineConfirmationId`), `SLReArm`, genuine information calls | to do (section 3) |

## 2. State

* Licenses: files as installed, kept unchanged and verified on install against the public roots (verification to
  do; the store now accepts what setup gives it).
* Keys: identified with the key configuration among the licenses; record per key (product, partial key, channel,
  derived ids). The key itself is not stored in full (the Windows service keeps the full key protected; ours keeps
  the derived values only).
* Time: grace starts with the first key of a product and does not restart when a key is removed.
* Today this lives in one process (the DLL) with files and registry under the machine root. A service version moves
  it into one process that owns the store, which fixes concurrent access and lets the state be protected from
  users.

## 3. Activation (to do; honest paths only)

1. **KMS client:** a product with a KMS client key (GVLK) asks a KMS host found by DNS SRV record or configured
   (`SLGetServiceInformation`/registry values as documented for `slmgr`), over RPC on TCP 1688; the host is the
   organization's, activated by Microsoft with its own key. The protocol must be taken from public
   documentation; none found yet that is complete. Until then: not implemented, the product stays in grace.
2. **Active Directory based activation:** the OS verifies signatures in a domain object; needs LDAP and public
   certificates (feasible with Wine's `wldap32`).
3. **MAK / retail online activation:** talks to Microsoft's servers; needs the service's identity toward Microsoft.
   Out of scope until documented.
4. Offline (phone) activation: installation id from public documentation of the id format, if any.

## 4. Process model

* A service `sppsvc` in session 0 (NetworkService) with one endpoint that the client library reaches from every
  session (requires OFFICE_BUG_NOTES.md OB-1).
* Plugins: none in the open version; the store and the activation clients are inside the service.

## 5. Tests

* Probes compared with Windows 11: `sppc-probe.c` (done for the read calls).
* Wine tests for each call (`dlls/sppc/tests`): to do.
