# Web Account Manager in Wine: plan

Plan of 2026-11-14, from the research in [WAM_RESEARCH.md](WAM_RESEARCH.md). Goal: programs that sign in with a
Microsoft account (Edge, Office and other MSAL-based programs, UWP apps) work under WineExplorer. Interoperability
only: Wine implements the documented API and the public OAuth flows; nothing reads or decrypts Windows' own
credential stores.

## Verified facts this plan relies on

Checked 2026-11-14 against the Windows SDK 10.0.26100 headers on the owner's Windows machine (interface IDs and
method lists are facts; no SDK text goes into the repository) and against this machine's registry:

| Interface | IID |
|---|---|
| `IWebAuthenticationCoreManagerInterop` (`RequestTokenForWindowAsync`, `RequestTokenWithWebAccountForWindowAsync`) | `F4B8E804-811E-4436-B69C-44CB67B72084` (the research's value was wrong) |
| `IAccountsSettingsPaneInterop` (`GetForWindow`, `ShowManageAccountsForWindowAsync`, `ShowAddAccountForWindowAsync`, in that order) | `D3EE12AD-3865-4362-9746-B75A682DF0E6` (the research had another IID and the last two swapped) |
| `IWebAuthenticationCoreManagerStatics` | `6aca7c92-a581-4479-9c10-752eff44fd34` |
| `IWebAuthenticationCoreManagerStatics2` / `3` / `4` / `5` | `f584184a-8b57-4820-b6a4-70a5b6fcf44a` / `2404eeb2-8924-4d93-ab3a-99688b419d56` / `54e633fe-96e0-41e8-9832-1298897c2aaf` / `d07c1ded-270f-4554-9966-27b7df05b965` |
| `IWebTokenRequest` / `2` / `3` / `IWebTokenRequestFactory` | `b77b4d68-adcb-4673-b364-0cf7b35caf97` / `d700c079-30c8-4397-9654-961c3be8b855` / `5a755b51-3bb1-41a5-a63d-90bc32c7db9a` / `6cf2141c-0ff0-4c67-b84f-99ddbe4a72c9` |
| `IWebTokenRequestResult` | `c12a8305-d1f8-4483-8d54-38fe292784ff` |
| `IWebTokenResponse` / `IWebTokenResponseFactory` | `67a7c5ca-83f6-44c6-a3b1-0eb69e41fa8a` / `ab6bf7f8-5450-4ef6-97f7-052b0431c0f0` |
| `IWebProviderError` / `IWebProviderErrorFactory` | `db191bb1-50c5-4809-8dca-09c99410245c` / `e3c40a2d-89ef-4e37-847f-a8b9d5a32910` |
| `IWebAccountMonitor` / `2`, `IFindAllAccountsResult` | `7445f5fd-aa9d-4619-8d5d-c138a4ede3e5` / `a7adc1f8-24b8-4f01-9ae5-24545e71233a`, `a5812b5d-b72e-420c-86ab-aac0d7b7261f` |

TokenBroker on Windows: `svchost.exe -k netsvcs -p`, `ServiceDll` `%SystemRoot%\System32\TokenBroker.dll` (checked).
`Windows.Security.Credentials` (`WebAccountProvider`, `WebAccount`) IIDs: to be taken from the SDK's
`windows.security.credentials.h` in step 1 the same way.

What happens today: `RoGetActivationFactory` for `WebAuthenticationCoreManager` fails ("Failed to find library").
MSAL treats a missing class as "no broker" and falls back to the browser, so MSAL programs with that fallback can
already sign in; programs that call the WinRT API directly (UWP apps, Edge's own account UI) get an activation
error instead of an answer.

## Steps

1. **The API with no provider** (new Wine module `windows.security.authentication.web.core`, IDL for it and for
   `WebAccountProvider`/`WebAccount` in `windows.security.credentials.idl`). `WebAuthenticationCoreManager` statics
   1 to 4 and the desktop interop interface: `FindAccountProviderAsync` (all overloads) completes with no provider;
   `RequestTokenAsync`/`GetTokenSilentlyAsync`/`RequestTokenForWindowAsync` complete with a `WebTokenRequestResult`
   whose status is `ProviderError` (or `AccountProviderNotAvailable`) and a `WebProviderError`; `FindAllAccountsAsync`
   returns an empty result with status `ProviderError`. `WebTokenRequest` and `WebTokenResponse` can be created and
   hold their properties (programs build them before calling). Tests in `dlls/.../tests`. Effort: a few days. Value:
   programs get valid objects and a proper "not available" answer; MSAL still falls back.
2. **`vaultcli`** on top of `advapi32`'s credential store (`VaultOpenVault`, `VaultEnumerateItems`, `VaultGetItem`,
   `VaultFree`, ...), for programs and MSAL caches that use it. Effort: about a week.
3. **A broker in WineExplorer**: a provider for Microsoft accounts (`https://login.microsoft.com`, authorities
   `organizations` / tenant and `consumers`) in a WineExplorer service (`wexhost` or a new one), so the interactive
   case works for programs without their own fallback. `RequestTokenForWindowAsync` opens a sign-in window, runs the
   authorization code flow with PKCE against `login.microsoftonline.com` / `login.live.com`, and returns a
   `WebTokenResponse` with the `wamcompat_*` properties MSAL reads (`wamcompat_id_token`, `wamcompat_client_info`,
   `wamcompat_scopes`). Open questions before starting: the redirect URI (programs register WAM's
   `ms-appx-web://Microsoft.AAD.BrokerPlugin/{client_id}`; a broker that is not Microsoft's may not be accepted for
   every client), the sign-in window (Wine's Gecko/mshtml or a WebView2 runtime), and where tokens are kept (step 2's
   vault, per user, never in the machine root). Effort: weeks; to be designed in its own document first.
4. **Work accounts with device sign-in** through a Linux-side daemon (Himmelblau, GPL-3.0-or-later, kept as a separate
   process over a socket or D-Bus) for joined-device single sign-on. After step 3, if needed.
5. **`AccountsSettingsPane`** (`Windows.UI.ApplicationSettings` and its interop interface) once a provider exists to
   list.

## Licences

MSAL, ROADtools, AADInternals and lsa-whisperer (MIT) may be used as references and their code reused with its
notice; Himmelblau only as a separate process. Wine modules stay LGPL-2.1-or-later.

## Progress

- 2026-11-14: research and verification done; step 1 started.
- 2026-11-15: step 1 in Wine (762955ab): no provider; requests complete with `AccountProviderNotAvailable`.
- 2026-11-16: a diagnostic provider for step 3's design (Wine 8b534d09 and later; off by default, switched on per user
  with `HKCU\Software\Wine\WebAuthentication` `DiagnosticProvider` = 1). Provider lookups answer with a provider
  object; token requests are written to the log and fail. A silent request that needs the user gets
  `UserInteractionRequired` with the error Windows 11's Microsoft account provider gives (checked on the owner's
  machine with a client id that is not registered, so no token could be issued): `0x80860006` "Error" for
  `consumers`, `0xCAA10001` "Need user interaction to continue." for `organizations`. The error object is always
  there (OneAuth dereferences it without a check). The request and response property maps answer
  `IIterable<IKeyValuePair<HSTRING, HSTRING>>` (Wine 46065162).
- What Microsoft Copilot (OneAuth, MSAL.xplat 11.2.2) does with it: at start, provider lookups for
  `login.microsoft.com` (`consumers`, `organizations`) and `login.windows.local`, `FindAllAccountsWithClientIdAsync`
  (no accounts), two `GetTokenSilentlyAsync` for client `0ec893e0-5785-4de6-99da-4ed124e5296c`, scope
  `profile openid 4765445b-32c6-49b0-83e6-1d93765276ca/.default`, authority `consumers`, with MSAL's telemetry
  properties. "Continue with Microsoft" then fails at once (`platformError=-21`) without any WinRT call; it never
  calls `RequestTokenForWindowAsync`. `IdentityCRL\LoginUrl` made no difference.
- Open decision (the owner's): Copilot's client is Microsoft's own and its tokens are issued only to redirect URIs
  Microsoft registered (its broker's). A WineExplorer broker would have to present itself as Microsoft's broker for
  such clients. Nothing is built toward that until the owner decides; step 3 for clients that accept the public
  native-client redirect is not affected.
