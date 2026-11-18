# WineExplorer's account broker (WAM step 3): design

Design of 2026-11-16 for step 3 of [WAM_PLAN.md](WAM_PLAN.md); background in [WAM_RESEARCH.md](WAM_RESEARCH.md).
Nothing here is built yet. Goal: a program that signs in through Windows' Web Account Manager (WAM) and has no
fallback of its own, Microsoft Copilot first, can sign in with a Microsoft account under WineExplorer.

## Ground rules (owner, 2026-11-16)

- Where the protocol requires the broker to present itself as a particular component, it does so: a client that is
  registered only for Microsoft's broker gets its authorization code at that broker's redirect URI
  (`ms-appx-web://Microsoft.AAD.BrokerPlugin/{client_id}`). That is the whole of the "identity": a redirect URI and
  the request fields the protocol defines. The broker does not claim a device registration, a Primary Refresh Token
  or anything else it does not have.
- Clean room: no Microsoft code, binaries, symbols or reference sources in the repository or used to write it.
  References are the public Microsoft identity platform documentation (OAuth 2.0 authorization code flow with PKCE,
  refresh tokens, the v2.0 endpoints), the WinRT API surface as Wine's own IDL has it, and MSAL (MIT) for the
  property names programs expect. Observations of what a program asks for come from Wine's own logs.
- Windows' credential stores (TokenBroker cache, credential vault, DPAPI) are never read. Tests never use the
  owner's real tokens from the Windows host; the real sign-in is done by the owner, by hand, in WineExplorer.
- The user sees and types their password only in the sign-in window; the calling program never sees it, as on
  Windows.

## Phase 0: why OneAuth does not reach the interactive call (first, before the broker)

With Wine's diagnostic provider (WAM_PLAN.md, "Progress"), Copilot's OneAuth finds the provider, asks silently
twice, gets Windows' exact "interaction required" answer, and then fails "Continue with Microsoft" at once
(`platformError=-21`) without calling `RequestTokenForWindowAsync`. A broker only helps once that call is made, so
this comes first. Candidates, cheapest first, each checked with a Wine log in Integration's Copilot setup:

1. **The broker package.** MSAL's WAM path is known to check for the Entra ID broker plugin package
   (`Microsoft.AAD.BrokerPlugin_cw5n1h2txyewy`). Look in a `+reg,+file` trace for package queries (the
   `Local Settings\...\AppModel\Repository\Packages` keys, `C:\Windows\SystemApps\...`) and for
   `Windows.Management.Deployment.PackageManager` / `Windows.ApplicationModel.Package` activations.
2. **The window.** The interactive call needs a top-level window it can parent the UI to; OneAuth may check the
   window it has (`GetAncestor`, `IsWindowVisible`, DPI, the process's UI thread) and refuse. Look for those calls
   right before the failure line.
3. **The account the silent answer carried.** On Windows the consumers answer carries the signed-in
   `WebAccount`; Wine's carries none. Try an answer with a `WebAccount` object (no token) and see whether OneAuth
   moves on.
4. **Its own configuration.** OneAuth's gates (`oneAuthNativeInteractiveAuthQueueEnabled` and others in the
   user's registry) and the failed activations still seen at start (`AppCapabilityAccess.AppCapability`,
   `ApplicationDataManager`, `Windows.Internal.Security.WebAuthentication.AuthenticationManager`).

Phase 0 ends when the click produces a `RequestTokenForWindowAsync` line in the diagnostic log. If it turns out that
OneAuth needs something that only Windows' own components can provide, this design stops there and says so.

**Result (2026-11-16, Integration's trace on Wine 46065162):** none of the four. Copilot's "Continue with
Microsoft" does not go through WAM at all: OneAuth writes `ProviderId` and `Authority` (`common`) to
`HKCU\Software\Microsoft\Copilot\SharedStorage\Auth` and opens its own embedded WebView2 sign-in. The WebView2
loader looks for a runtime (`EdgeUpdate\Clients` and `ClientState` of the Evergreen runtime
`{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` and the Beta, Dev and Canary channels, `Policies\Microsoft\Edge\WebView2`),
finds none, and the sign-in fails 114 ms after the click (`platformError=-21`). The Copilot MSI installs a runtime
(`Program Files (x86)\Microsoft\EdgeCore\156.0.4314.58`) but nothing registers it. On Windows 11 the Evergreen runtime
is always registered (`Clients\{F3017226-...}`: `pv`, `location`; `ClientState\{F3017226-...}`: `pv`, `EBWebView` =
the version folder). So Copilot needs no broker: its blocker is WebView2 runtime discovery (why the runtime the
installer brings is not registered, or why the updater that registers it does not run under Wine). The broker
stays the design for programs that do call WAM's interactive methods; it is not started for Copilot.

Also in phase 0: which redirect URI Copilot's client accepts. Checked 2026-11-16 without signing in (authorize
requests with no cookies or credentials, `consumers`, `organizations` and `common`, five candidate URIs including
ones the client cannot have registered): the endpoint answers with the sign-in page for nearly all of them and with
no `AADSTS50011` for any, so it checks the redirect URI only after the user has signed in. The question is
therefore answered only by the owner's first real sign-in in step 2 (the broker plugin URI first, as the program's
WAM path implies).

**Update (2026-11-17, Integration, image a2d4ed34 / Wine 7a0018f7, WebView2 Runtime 157.0.4341.60 installed as the
optional component, so the runtime is registered):** the blocker above is gone, and Copilot does need the broker.
"Continue with Microsoft" opens the embedded sign-in window; once the owner types the account name, Copilot calls
`IWebAuthenticationCoreManagerInterop::RequestTokenForWindowAsync` (prompt type 1) for client
`0ec893e0-5785-4de6-99da-4ed124e5296c`, scope `profile openid 4765445b-32c6-49b0-83e6-1d93765276ca/.default`,
provider `https://login.microsoft.com`, authority `consumers`, with the request properties `Client_uiflow` =
`new_account`, `LoginHint` (the typed account name), `api-version` 2.0, `oauth2_batch` 1, `telemetry` MATS, `nopa` 2,
`mkt` and MSAL's `x-client-*` fields. The diagnostic provider answers status 5 (`ProviderError`, 0x80004001), and
Microsoft's page in the window then shows "Something went wrong [4wp0s]", code -2146893805 (0x80090013,
`NTE_BAD_PROVIDER`; the same tag and code are reported on Windows too, so it is a general failure of the sign-in
step, not a Wine-only code). Ruled out with a Wine trace of the whole session: no `NCrypt*` call (no key-provider
failure), only ordinary `CryptAcquireContext` with verify-context, no TPM or key-credential lookups, no library
load failures. Still missing at that point, for the owner's scope (generic classes belong to the UWP work): the
internal class `Windows.Internal.Security.WebAuthentication.AuthenticationManager` and
`Windows.Security.Authorization.AppCapabilityAccess.AppCapability` (activation fails), and COM class
`{daa52b27-8897-50af-ada5-c6c71bb64e17}` (not registered). So the next step is step 3 itself: answer this
`RequestTokenForWindowAsync` call with a real sign-in.

## Parts

```
program (Copilot, ...)                         per-user broker process (tokenbroker.exe, Wine program)
 windows.security.authentication.web.core  ──►  provider for login.microsoft.com (consumers, organizations,
 (in the program's process)        named pipe   tenant), login.windows.local mapped to the default account
                                                 token cache (per user), sign-in window (WebView2)
                                                        │ HTTPS (winhttp)
                                                        ▼
                                          login.microsoftonline.com /{authority}/oauth2/v2.0/{authorize,token}
```

- **The WinRT side** (Wine module `windows.security.authentication.web.core`, exists): provider lookups, token
  requests, account lookups. With a broker present it forwards them to the broker; without one it keeps today's
  "no provider" answers, so programs with a browser fallback are unaffected.
- **The broker** (new Wine program, working name `tokenbroker.exe`): one per user and session, started on demand,
  reached over a named pipe `\pipe\WineTokenBroker\<user SID>`. It holds the cache and runs the sign-in. One
  process, not one per program, so accounts are shared between programs as on Windows. Not in session 0: tokens are
  the user's, never the machine's.
- **The sign-in window**: a WebView2 control (the runtime that Edge and Copilot already install, found through its
  documented loader `CreateCoreWebView2EnvironmentWithOptions`; nothing of it ships with WineExplorer). It opens the
  authorize URL and watches navigations; the one to the redirect URI is caught and never loaded, and the code is
  taken from it. Parented to the calling program's window (the HWND of `RequestTokenForWindowAsync`), modal.
  Without a WebView2 runtime the request fails with `ProviderError` and a message that says so; Wine's Gecko is not
  used, the sign-in pages need a current engine.
- **Accounts and tokens**: `WebAccount` objects (`Id` = the account's object id, `UserName` = its sign-in name,
  `WebAccountProvider`), `FindAllAccountsAsync` from the cache, `GetTokenSilentlyAsync` by refresh token.

## Flows

- **Interactive** (`RequestTokenForWindowAsync`, `RequestTokenAsync`): authorization code with PKCE (S256), `state`
  and `nonce`, the request's scope plus `openid profile offline_access`, `login_hint`/`prompt` from the request's
  properties; the code is redeemed at the token endpoint with the verifier. Result: `Success`, a `WebTokenResponse`
  with `Token` = the access token and the `wamcompat_*` properties MSAL reads (`wamcompat_id_token`,
  `wamcompat_client_info`, `wamcompat_scopes`, `wamcompat_expires_on`, `wamcompat_user_displayable_id`,
  `wamcompat_account_id`), and the `WebAccount`. Cancel gives `UserCancel`; an error page gives `ProviderError` with
  the server's error mapped to the `0xCAA2xxxx`-`0xCAA9xxxx` codes MSAL knows.
- **Silent** (`GetTokenSilentlyAsync`): a cached access token for the same client, scope and account if it is valid
  for five more minutes; else the refresh token; else `UserInteractionRequired` with the error codes Windows gives
  (already in the diagnostic provider).
- **Accounts**: `FindAllAccountsAsync(provider, clientId)` lists the cache's accounts for that provider and
  authority; `FindAccountAsync` by id; sign-out (`WebAccount.SignOutAsync`) removes the account's tokens.

## The cache

- Per user, in the user's own home, outside every prefix: `~/.local/share/wineexplorer/tokenbroker/`, directory
  0700, files 0600, written by the broker only (temp file and rename). Never in the machine root, never synced.
- Refresh tokens are the secret. If a Secret Service is on the session bus (GNOME Keyring, KWallet), the refresh
  tokens go there (D-Bus, as the rules prefer) and the file keeps only the rest; otherwise the 0600 file holds them.
  That is the protection MSAL itself uses on Linux.
- Keys: account (home account id), authority, client id, scope set. Access tokens are kept until they expire.
- A limit stated plainly: every program the user runs is the same Unix user and could ask the broker for a token
  for any client id, as any MSAL cache on Linux allows. Windows binds tokens to the calling package; Wine has no
  package identity for desktop programs. The sign-in window always names the program asking (its executable).

## Security notes

- The pipe accepts only the user's own processes (the pipe's descriptor: the user's SID; the broker checks the
  client token's user as well). Session 0 and other users never reach it.
- The broker talks only to the authority hosts it knows (`login.microsoftonline.com`, `login.live.com`), over
  HTTPS with certificate checks (winhttp's defaults); an authority in a request that is not one of these is
  refused (`ProviderError`), so a program cannot point the broker at another server.
- Authorization codes, verifiers and tokens are never logged, also not at trace level; logs carry lengths and
  hashes at most.
- Redirect catching: only a navigation whose scheme, host and path match the request's redirect URI exactly, with
  the `state` the broker sent.

## Tests

- Conformance tests in `dlls/windows.security.authentication.web.core/tests` keep checking the no-broker path.
- The broker gets a test authority: a small local OAuth server (Python, standard library, in the suite) that
  implements authorize and token with PKCE, issues test tokens, and checks the verifier. The broker accepts it only
  when a test switch names it (`HKCU\Software\Wine\WebAuthentication`, `TestAuthority`), and the switch is refused
  for the real hosts. Cases: interactive success (the window driven by a test hook that fills the test form),
  cancel, silent from cache, silent by refresh, refresh refused, account list, sign-out, wrong `state`, a hostile
  redirect (another host, same path), a request for an unknown authority, the cache's file modes, a second user
  who cannot reach the pipe.
- The real sign-in: the owner, by hand, in Copilot on an installed WineExplorer; a log of what happened, without
  tokens.

## Steps

0. Phase 0 above (diagnosis with Integration; the redirect URI check). Stop or go.
1. The broker process, the pipe, `WebAccount`, the cache, silent and account calls, against the test authority.
2. The sign-in window (WebView2), interactive flow, against the test authority, then by hand against the real one.
3. `login.windows.local` (the default account) and `AccountsSettingsPane` (WAM_PLAN step 5) if programs need them.

Each step is a Wine change (listed in STOCK_CHANGES.md) plus its tests, reviewed and run before the next.

## Open questions

- Personal accounts and the redirect URI: whether Copilot's client takes its consumers tokens through the v2.0
  endpoint with the broker plugin URI, or through `login.live.com` with another one (phase 0's check answers it).
- Whether the token endpoint asks a broker for more than a public client (for example a broker-specific parameter);
  if it does and that parameter is not public, the broker stops there.
- Where `tokenbroker.exe` starts from: on demand by the WinRT side (simplest), or as a per-session service that
  wexhost starts.
