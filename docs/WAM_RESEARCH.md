# Web Account Manager (WAM): research for Wine and WineExplorer

Research of 2026-11-14 (Antigravity, first pass; checked by Claude where marked). Needed later for Edge, Office and
UWP apps that sign in with Microsoft accounts. Interoperability only: architecture, public APIs, the broker as
applications see it, the network flows. Extracting or decrypting credentials is out of scope and not covered.
The second pass (files, registry keys, data fields, interfaces, verified licences) is the addendum below.

## What Wine has today (checked in the tree)

- Absent: `windows.security.authentication.web.core` (no DLL, no IDL), `vaultcli`, TokenBroker,
  `Windows.UI.ApplicationSettings` (`AccountsSettingsPane`). Programs get
  `RoGetActivationFactory` "Failed to find library" for `WebAuthenticationCoreManager`.
- Present: `windows.security.authentication.onlineid` (stubs: `OnlineIdSystemAuthenticator`,
  `OnlineIdServiceTicketRequest`), `windows.security.credentials.ui.userconsentverifier` (stub), `credui`
  (`CredUIPromptForCredentialsW` and others), `advapi32/cred.c` (`CredReadW`, `CredWriteW`, `CredEnumerateW`).

## How WAM fits together on Windows

- Applications call the WinRT API `Windows.Security.Authentication.Web.Core` (`WebAuthenticationCoreManager`,
  `WebAccountProvider`, `WebTokenRequest`, `WebTokenResponse`); desktop programs use
  `IWebAuthenticationCoreManagerInterop::RequestTokenForWindowAsync(HWND, request, riid, asyncOp)` for the interactive
  case and `GetTokenSilentlyAsync` for the silent one. Account UI: `AccountsSettingsPane`
  (`IAccountsSettingsPaneInterop` for desktop programs).
- The TokenBroker service ("Web Account Manager", `TokenBroker.dll` in a `svchost`) coordinates providers and
  caches.
- Providers: Entra ID (work and school) is the `Microsoft.AAD.BrokerPlugin` package, run in an app container; the
  personal Microsoft account provider belongs to the system's account components. Their UI runs isolated, so the
  calling program never sees the password.
- Device sign-in: the CloudAP security package in LSA holds the Primary Refresh Token (PRT) of a joined device;
  TokenBroker asks it for SSO material through `LsaCallAuthenticationPackage` (conceptual only here).
- Cached accounts and tokens: the user's local application data (TokenBroker cache) and the credential vault
  (`vaultcli.dll`).

## What programs do

- MSAL (.NET, C++, Python) with `.WithBroker(...)` prefers WAM. It falls back on its own when WAM is unavailable:
  the class not registered, no provider found, or a provider-unavailable status. The fallback is a system browser
  with a loopback redirect (`http://localhost:<port>`) or an embedded web view (WebView2).
- So the first useful step for Wine is a clean "no broker" answer: MSAL programs then sign in through the browser.

## Network side

- Entra ID: `https://login.microsoftonline.com/{tenant}/oauth2/v2.0/authorize` and `.../token`; device registration
  service `https://enterpriseregistration.windows.net/...`.
- Personal accounts: `https://login.live.com/oauth20_authorize.srf` and `oauth20_token.srf`.
- Flows: authorization code with PKCE (`code_challenge_method=S256`), refresh tokens; a joined device adds signed
  JWT-bearer grants bound to device keys (conceptual).
- Broker redirect URI for Entra ID: `ms-appx-web://Microsoft.AAD.BrokerPlugin/{client_id}`.

## Prior art and licences (first pass; to be verified from each LICENSE file in the addendum)

| Project | Licence (reported) | Relevant for |
|---|---|---|
| MSAL .NET (github.com/AzureAD/microsoft-authentication-library-for-dotnet) | MIT | how programs call WAM and when they fall back; WAM wrapper code |
| ROADtools (github.com/dirkjanm/ROADtools) | MIT | Entra ID protocol flows, device registration, PRT at protocol level |
| AADInternals (github.com/Gerenios/AADInternals) | MIT | Entra ID endpoints and protocol structures |
| lsa-whisperer (github.com/EvanMcBroom/lsa-whisperer) | reported MIT, unverified | the LSA authentication package messages (CloudAP) as documentation |
| Himmelblau (github.com/himmelblau-idm/himmelblau) | reported GPL-3.0, unverified | Entra ID on Linux (daemon, PAM/NSS, device enrollment, PRT): usable as a separate host daemon over IPC; GPL-3.0 code cannot go into LGPL Wine or GPL-2.0-only code |
| ReactOS | GPL-2.0-or-later | nothing for WAM |

MIT code can be reused in Wine (LGPL-2.1-or-later) and in WineExplorer's GPL-2.0-or-later code, keeping its notice.

## Proposed path (first pass)

1. `windows.security.authentication.web.core` with a `WebAuthenticationCoreManager` that answers "no provider"
   (`FindAccountProviderAsync` completes with no provider), so MSAL programs fall back to the browser. Low effort,
   low risk; unblocks programs that have the fallback.
2. `vaultcli` on top of `advapi32`'s credential store. Low to medium effort.
3. An in-Wine broker: `RequestTokenForWindowAsync` showing a sign-in window (WebView2 or Gecko), doing the PKCE code
   exchange itself and returning a `WebTokenResponse`. Medium effort; MFA and conditional access pages in an embedded
   control are the risk.
4. Device SSO for Entra ID by delegating to a Linux-side daemon (Himmelblau) over a socket or D-Bus. High effort.

## Addendum: files, registry, fields, interfaces (second pass, 2026-11-14)

Antigravity's second pass, condensed. Checked by Claude on this Windows host where marked; everything else as
reported, with the sources it cited (Microsoft Learn, the MSAL source, community and SpecterOps write-ups). The
report's description of decrypted cache contents is left out on purpose (out of scope).

### Files and services

- **TokenBroker** ("Web Account Manager"): `HKLM\SYSTEM\CurrentControlSet\Services\TokenBroker`, ImagePath
  `%SystemRoot%\system32\svchost.exe -k netsvcs -p`, `Parameters\ServiceDll` =
  `%SystemRoot%\System32\TokenBroker.dll` (checked); demand / trigger start, LocalSystem (reported).
- **Entra ID provider**: package `Microsoft.AAD.BrokerPlugin_cw5n1h2txyewy` in
  `C:\Windows\SystemApps\Microsoft.AAD.BrokerPlugin_cw5n1h2txyewy`; its state in
  `%LOCALAPPDATA%\Packages\Microsoft.AAD.BrokerPlugin_cw5n1h2txyewy\LocalState`.
- **Personal account provider**: `Microsoft.AccountsControl_cw5n1h2txyewy` (`C:\Windows\SystemApps\...`), with
  `Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy` hosting the sign-in web views.
- **Token cache**: `%LOCALAPPDATA%\Microsoft\TokenBroker\Cache\*.tbres`, UTF-16LE JSON
  (`TBDataStoreObject.ObjectData.SystemDefinedProperties`, with the response protected by DPAPI). A Wine
  implementation keeps its own cache and needs no compatibility with these files.

### Registry

- `HKLM\SYSTEM\CurrentControlSet\Control\CloudDomainJoin\JoinInfo\{id}` (`TenantId`, `UserEmail`, `DeviceId`) and
  `TenantInfo\{tenant}` (`DisplayName`, `MdmEnrollmentUrl`, `MdmTermsOfUseUrl`, `MdmComplianceUrl`): a joined
  device's state.
- `HKCU\Software\Microsoft\Windows\CurrentVersion\WorkplaceJoin` (work accounts on a personal device);
  policies `HKLM\SOFTWARE\Policies\Microsoft\Windows\WorkplaceJoin` (`AutoWorkplaceJoin`, `BlockAADWorkplaceJoin`).
- `HKCU\Software\Microsoft\IdentityCRL` (`StoredIdentities`, `UserExtendedProperties\<account>`: `CID`, `PUID`,
  `DisplayName`, `DefaultIdp`): personal accounts.
- `HKLM\SOFTWARE\Microsoft\IdentityStore\Providers\{D7F9888F-...}` (MicrosoftAccount, `wlidsvc.dll`) and
  `{B16898C6-...}` (AzureAD, `aadcloudap.dll`) (reported, unverified).

### Provider IDs (corrects the first pass)

- Work and school: provider `https://login.microsoft.com`, authority `organizations` or a tenant ID.
- Personal accounts: provider `https://login.microsoft.com`, authority `consumers`.
- The default account of the system: provider `https://login.windows.local`, no authority.

### Interfaces

- `IWebAuthenticationCoreManagerInterop` (IID reported `F4B3CA97-EFE8-4F7D-B3BF-9EFA69808EE4`, unverified; in-process,
  not in the registry): IInspectable, then `RequestTokenForWindowAsync(HWND, IInspectable *request, REFIID, void **)`
  and `RequestTokenWithWebAccountForWindowAsync(HWND, IInspectable *request, IInspectable *account, REFIID, void **)`.
- `IAccountsSettingsPaneInterop` (IID reported `323543DD-3EAB-43DC-BCDE-45671CE800C8`, CONFLICTS with Claude's
  recollection `D3EE12AD-3865-4362-9746-B75A682DF0E6`: take it from the Windows SDK header
  `accountssettingspaneinterop.h` before use): `GetForWindow`, `ShowAddAccountForWindowAsync`,
  `ShowManageAccountsForWindowAsync`.
- WinRT classes (method lists; IIDs to be taken from the Windows SDK's `windows.security.authentication.web.core.idl`
  / metadata when implementing): `WebAuthenticationCoreManager` statics 1 to 4 (`GetTokenSilentlyAsync`,
  `RequestTokenAsync`, `FindAccountAsync`, `FindAccountProviderAsync` (+WithAuthority, ForUser),
  `CreateWebAccountMonitor`, `FindAllAccountsAsync`); `WebTokenRequest` (+2 `AppProperties`, +3 `CorrelationId`);
  `WebTokenResponse`; `WebAccountProvider` (+2 `Authority`, +3 `User`, +4 `IsSystemProvider`); `WebAccount` (+2);
  `WebProviderError`.

### Request and response fields (from MSAL)

- `WebTokenRequest.Properties` set by MSAL: `client_id`, `authority`, `wam_compat` (`2.0`), `prompt`,
  `login_hint`, `resource` (v1 only), `scope`, `correlation_id`, `msal_request_type`.
- `WebTokenResponse`: `Token` (the access token); `Properties` `wamcompat_id_token`, `wamcompat_client_info` (base64
  JSON with `uid`, `utid`), `wamcompat_scopes`, `wamcompat_user_displayable_id`, `wamcompat_account_id`.
- `WebTokenRequestStatus` (checked against the SDK; the research's values were wrong): Success 0, UserCancel 1,
  AccountSwitch 2, UserInteractionRequired 3, AccountProviderNotAvailable 4, ProviderError 5. MSAL falls back to the browser on `REGDB_E_CLASSNOTREG` (class missing) and on a
  provider that is not available; provider error codes it interprets are in the `0xCAA2xxxx`-`0xCAA9xxxx` range
  (authority discovery, network, TLS, interaction required, cancel).

### Licences (second pass: from each repository's LICENSE)

| Project | SPDX | Wine (LGPL-2.1-or-later) | WineExplorer code (GPL-2.0-or-later) |
|---|---|---|---|
| MSAL .NET | MIT | reusable | reusable |
| MSAL C++ | not public (its NuGet interop headers are MIT, unverified) | headers only | headers only |
| ROADtools | MIT | reusable | reusable |
| AADInternals | MIT | reusable | reusable |
| lsa-whisperer | MIT | reusable | reusable |
| Himmelblau | GPL-3.0-or-later | not linkable; usable as a separate daemon over IPC | only by making the combined work GPL-3.0 (a deliberate decision per `CLAUDE.md`); as a daemon, no issue |

MIT code keeps its notice when reused.

## To check (first pass claims marked doubtful by Claude)

- Settled by the second pass: the provider IDs (above), the TokenBroker registration (checked), the licences.
- Checked against the Windows SDK 10.0.26100 headers (2026-11-14): the interop IIDs were WRONG in the second pass
  (`IWebAuthenticationCoreManagerInterop` is `F4B8E804-811E-4436-B69C-44CB67B72084`; `IAccountsSettingsPaneInterop`
  is `D3EE12AD-3865-4362-9746-B75A682DF0E6` with the order GetForWindow, ShowManageAccounts, ShowAddAccount), and so
  were the `WebTokenRequestStatus` values. Use [WAM_PLAN.md](WAM_PLAN.md)'s verified table, not the values above.

### Third pass (2026-11-17, Integration): licences read from each repository, and what each could contribute

Read through the GitHub API (LICENSE and REUSE files, file lists, public function signatures; nothing copied).

| Project | Licence (from its files) | What it is | Use here |
|---|---|---|---|
| libhimmelblau (`himmelblau-idm/libhimmelblau`, mirror of the Samba team's repository) | LGPL-3.0 | Rust MSAL-style client: authorization code with PKCE, refresh token, device flow, MFA, and (beyond step 3) device enrolment, PRT, Hello for Business | Reference for the OAuth side of step 3 only. LGPL-3.0-only cannot go into a Wine module (LGPL-2.1-or-later); as a separate process it is a deliberate licence decision (`CLAUDE.md`). The PKCE and refresh flows are public OAuth, so writing them from the Microsoft identity platform documentation costs little |
| `identity_dbus_broker` (`himmelblau-idm`) | LGPL-3.0 | Rust traits and a D-Bus server for Microsoft's Linux broker interface (`com.microsoft.identity.broker1`: `acquireTokenInteractively`, `acquireTokenSilently`, `getAccounts`, `removeAccount`, `acquirePrtSsoCookie`, `generateSignedHttpRequest`, `cancelInteractiveFlow`, `getLinuxBrokerVersion`). Interface and plumbing only | Not the WinRT API, so it does not complete our interface. It shows how that broker's methods line up with WAM's (request token, silent token, find accounts, remove account) |
| Himmelblau (`himmelblau-idm/himmelblau`) | GPL-3.0 | Entra ID login for Linux (PAM, NSS, broker daemon, Intune) | Separate daemon only (step 4), as already decided |
| sso-mib (`siemens/sso-mib`) | library LGPL-2.1-only; `sso-mib-tool` GPL-2.0-only; examples, README and `dbus/spec/com.microsoft.identity.broker1.xml` MIT | C client for that Linux broker's D-Bus interface | The MIT interface description is reusable with its notice, as a description of the Linux broker's methods. LGPL-2.1-only code stays out of Wine modules |
| MSAL.NET (`AzureAD/microsoft-authentication-library-for-dotnet`) | MIT | Microsoft's client library; at this point its Windows broker path calls a native runtime, not the WinRT API | Reusable with notice; useful for the request property names programs send (for example `msal_request_type`, `instance_aware`, `msal_client_sku`) |

None of them implements `Windows.Security.Authentication.Web.Core` or its desktop interop interface, so the WinRT surface
(statics, `WebTokenRequest`/`Response`, accounts, monitors) still has to come from the interface descriptions and Wine's
own IDL; the projects above can only inform the broker behind it.
