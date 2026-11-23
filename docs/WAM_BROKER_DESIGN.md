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
- **The broker's sources** (`src/programs/tokenbroker`): `main.c` the process, pipe, caller checks and protocol;
  `provider.c` the providers and operations; `answer.c` the answer's shape (WAM's for a Microsoft account);
  `oauth.c` the OAuth flows (one refresh function, optionally through a broker client); `signin.c` the sign-in
  state machine and test hook; `webview.c` the WebView2 window; `cache.c` accounts and tokens; `util.c` strings,
  JSON, HTTP and the shared helpers (`user_only_descriptor`, `scope_has`, `request_login_hint`, `reply_failure`).
  The status of a `FIND_ACCOUNT` reply is `TOKENBROKER_ACCOUNT_FOUND` or `_NONE` (`wine/tokenbroker.h`).

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

## The shape of Windows' answer for a Microsoft account (measured 2026-11-22)

Found while the owner's real sign-in in Copilot ended in the identity library's own error page (tag 9yzy9, code 0,
shown after our broker had redeemed the code and returned a token). A small probe run by the owner on Windows
printed the *shape* of the answer (names, kinds, lengths, character classes; never values, no account data) for the
request Copilot sends: provider `https://login.microsoft.com`, authority `consumers`, prompt type 1, request
properties `Client_uiflow=new_account`, `api-version=2.0`, `oauth2_batch=1`, `x-client-info=1`, `telemetry=MATS`,
`x-client-SKU`, `x-client-Ver`, `mkt`, `nopa=2` (the library calls itself `MSAL.xplat.Win32`, closed source). The
answer, as a `WebTokenRequestResult` with status Success and one `WebTokenResponse`:

- **Token** is the token endpoint's answer as an URL-encoded form body, not the bare access token. The fields in this
  order: `access_token` (a JWT-shaped string, 2130 characters for our scope), `token_type` (`Bearer`), `expires_in`
  (seconds left, a number of 5 digits), `scope` (the granted scopes, space separated), `id_token` (a JWT), `client_info`
  (base64 text, no padding, about 540 characters). No `refresh_token`.
- **Response `Properties`** has one entry, `MATS`, a JSON text of 230 characters with the keys `ui_visible`,
  `correlation_id`, `account_id`, `web_flow_result`, `silent_code`, `silent_bi_sub_code`, `silent_message`,
  `silent_status`, `is_cached`. There are no `wamcompat_*` entries (those belong to the other consumers' request
  shape: they are kept when `oauth2_batch` is not asked for).
- **`WebAccount`**: `Id` is 16 upper-case hex digits (the account's `UID`), `UserName` the sign-in address,
  `State` is `None` (not Connected), `WebAccountProvider` as asked. `WebAccount.Properties` has nine entries:
  `IsRoamingEncryptionKeyAvailable` (`false`), `DisplayName`, `FirstName`, `LastName`, `CountryOrRegion` (2 letters),
  `UserName`, `UID` (the same 16 digits as `Id`), `AgeGroup` (a one digit number) and `SafeCustomerId` (the customer id,
  16 lower-case hex digits; it is the last 16 digits of the id token's object id `00000000-0000-0000-xxxx-xxxxxxxxxxxx`).
- `FindAllAccountsAsync` for the provider and client id answers the same `WebAccount` shape (state `None`, the same
  `Id` and properties).

What the library did with our earlier answer (a bare JWT as Token, six `wamcompat_*` properties, a GUID `Id`, state
Connected, no account properties): it read `ResponseStatus`, the response data, the account's `Id`, all response
properties and the `Token`, and then showed its error page without reading anything else; so the cause is in the
account `Id` form, the response properties or the `Token` form. The answer is built to the measured shape now
(`src/programs/tokenbroker/answer.c`, until 2026-11-23 in `provider.c`: `public_account_id`, `account_properties`,
`telemetry_property`, `batch_token`; `dlls/windows.security.authentication.web.core`: `TOKENBROKER_REPLY_ACCOUNT_PROPERTIES`,
`IWebAccount2::get_Properties`, state `None` for the `consumers` authority). Branches: superproject
`claude/vintner-boot-integration` (commits 287bbd88 and 1d661628), Wine `claude/webaccount-properties`. Status: see
the owner's retries in `docs/HANDOFF.md` once they have run; `AgeGroup` is not sent because the id token does not tell
it and it is not made up, and neither is `CountryOrRegion` unless the id token has `ctry`.

## What the owner's sign-ins in Copilot and Edge showed (2026-11-22/23)

Copilot signs in and chats on an image with the following broker behaviour; each item was found from the logs of the
owner's real sign-ins (no tokens kept) and, for the shapes, from the Windows probes above.

- **The answer's shape** is the one measured above (batch form body, `MATS`, hex16 `Id`, state `None`, nine account
  properties). The library only accepted the answer once the Token, the account Id and the properties all had that form
  (error tags 9yzy9, then 53u4r: `wininet` refused `INTERNET_OPTION_LISTEN_TIMEOUT`, Wine branch
  `claude/wininet-listen-timeout`, see STOCK_CHANGES.md).
- **FindAccountAsync** names no client id. A process may find the accounts of the client ids it has itself used
  (`remember_client`, `find_account`: an in-memory table of 16 processes with 4 client ids each); another process's
  accounts stay invisible. Without it Copilot kept asking and its window stayed black.
- **Nested App Authentication.** Copilot's silent requests carry `child_client_id` and `child_redirect_uri` (the web
  app inside it, with `brk-multihub://m365.cloud.microsoft`) and `is_scope_request`. The token is for the child: the
  refresh names the child as `client_id` and `redirect_uri` and the parent as `brk_client_id` and `brk_redirect_uri`
  (`oauth_refresh_nested`); the token is cached under the child's client id and the parent's refresh token stays the
  account's. Without it Copilot rejected every silent answer (`Silent token acquisition FAILED ... is_naa=1`) and the
  chat never started.
- **Windows keeps the account for the user, not per client id** (second probe, `winprobe` outside the repository,
  Edge's requests): with one account, silent requests succeed for Edge's client id `82864fa0-...`, for MSN's
  `f3222e55-...` and for OneService's `d7b530a4-...`, and for the legacy scopes `service::ssl.live.com::MBI_SSL_SHORT`,
  `service::substrate.office.com::MBI_SSL`, `service::www.msn.com::mbi_ssl` and
  `service::http://Passport.NET/purpose::PURPOSE_GETKEYDATA_ANAHEIM`. All answers have the batch form body (access
  token, `token_type`, `expires_in`, `scope`, `id_token`, `client_info`; for `service::` scopes the access token is an
  opaque base64-like string, not a JWT, and `scope` is the requested scope); the silent `MATS` has 22 keys
  (`wam_account_on_end`, `perf`, `correlation_id`, `wam_blob_version`, `client_id`, `scope`, `authority`, `provider_id`,
  `client_binary_version`, `broker_version`, `wam_account_on_start`, `prompt_behavior`, `api_error_code`, `ui_visible`,
  `ui_allowed`, `server_error_code`, `account_id`, `silent_code`, `silent_bi_sub_code`, `silent_message`,
  `silent_status`, `is_cached`; the interactive one has the nine listed above). The broker sends the nine in both cases.
- **Other client ids.** The broker kept accounts per client id, so Edge's requests under its other client ids were all
  refused ("no account": 41 of them in one run). A silent request whose client id has no account now uses the account of
  another client id the same process has used and asks for the token as the requested client through it (a brokered
  refresh, `account_of_used_client`); whether the token endpoint accepts that for every first-party client id is
  to be seen. A process still never sees another process's accounts.
- **Edge** (157.0.4341.60 from the enterprise MSI) installs and runs in the machine prefix; its sign-in works, and its
  profile showed "Verify it's you" while those requests were refused. Its window is black with GPU rendering on (the
  default) and draws with `--disable-gpu`.

### Known open after the sign-ins (2026-11-23)

- Brokered requests for other first-party client ids (MSN `f3222e55-...`, OneService `d7b530a4-...`, the Bing scope)
  are answered HTTP 400 `invalid_request` by the token endpoint; the broker logs only the code, not the text. Edge's sync
  works without them.
- ~~The sign-in window opens behind other windows~~: fixed, the broker's window is topmost while open (see "Edge and GPU
  rendering").
- Edge's window stays black with GPU rendering (the default) in the QEMU test VM (no GPU: software GL, `llvmpipe`); with
  `--disable-gpu` it draws. A try in WSL with the GPU is in "Edge and GPU rendering" (no result).
- Edge's first-run "One more step" page does not finish (see "Edge's first run, resolved").
- A Copilot VM needs about 7 GB resident at idle on software GL (16 Chromium processes, 3 GPU processes, no leak): a 4 GB
  VM is killed by the OOM killer.
- The silent answer's `MATS` has 22 keys on Windows; the broker sends the nine of the interactive answer.

## Open questions

- Personal accounts and the redirect URI: whether Copilot's client takes its consumers tokens through the v2.0
  endpoint with the broker plugin URI, or through `login.live.com` with another one (phase 0's check answers it).
- Whether the token endpoint asks a broker for more than a public client (for example a broker-specific parameter);
  if it does and that parameter is not public, the broker stops there.
- Where `tokenbroker.exe` starts from: on demand by the WinRT side (simplest), or as a per-session service that
  wexhost starts.

### Edge's first-run "Sign in" page, and `login.windows.local` (2026-11-23)

- On a PC with a local account, a probe on Windows measured: `FindAccountProviderAsync("https://login.windows.local")`
  answers null, `FirstSignInSettings.GetDefault()` is an empty map, and the user has no account name. Edge on that PC
  still signs a fresh profile in at once (an implicit sign-in from another source; not the default account).
- Tried in Wine: the same null answer for `login.windows.local` with the broker. Copilot then took a different path and
  showed a Microsoft error page ("Something went wrong [4otqj]") although the broker had signed it in; with the
  provider served again Copilot loads signed in. The null answer was reverted: keep serving the provider.
- Open: Edge's first-run Sign in button does nothing in the VM (no interactive request reaches the broker; only
  `FindAllAccounts` finds nothing, and `Windows.System.UserProfile.FirstSignInSettings` is requested and missing). The
  profile menu's sign-in works, and its window now stays in front (the broker's window is topmost while open).

### Edge's first run, resolved (2026-11-23)

- Edge's first-run "Sign in" button did nothing, and a profile reset still came up signed in, because of a shared
  account store outside the profile and outside the broker's cache: `%LOCALAPPDATA%\Microsoft\OneAuth` and
  `...\IdentityCache` (Microsoft's identity library, shared by Edge and Copilot; Edge signs a fresh profile in from
  it, "implicit sign-in"). With both folders moved aside, a natural first run (no `--force-first-run`) shows Edge's own
  "Sign in" window, then the broker's interactive sign-in (window in front, the broker's window is topmost), and the
  account and its sync come up (profile, extensions). Test recipe: stop Edge and Copilot, move `OneAuth`,
  `IdentityCache` and Edge's `User Data` aside, start Edge. Copilot needs to sign in again afterwards.
- The first-run flow ends in a "One more step: check the notification on your device" page that does not finish (its
  arrow does not skip it); restarting Edge completes the setup. Not looked into further: it likely waits for a Windows
  notification or the internal `Windows.Internal.Security.WebAuthentication.AuthenticationManager`, which is missing.
- `--force-first-run` is not a fair test: with the shared store present the flow behaves differently.

### Edge and GPU rendering (2026-11-23)

- **The sign-in window is no longer behind Edge.** It is created unowned (Edge's request has window handle 0) and
  Edge's full-screen first-run page is topmost, so the broker's window is kept `HWND_TOPMOST` while it is open and
  raised (`ShowWindow`, `SetForegroundWindow`) at creation and when the browser control is ready (`webview.c`,
  `raise_window`). A non-topmost raise was not enough.
- **Edge's black or white window is not a broker or sign-in problem.** In the QEMU test VM (software GL, `llvmpipe`) the
  window is black with GPU rendering on and draws with `--disable-gpu`. Not investigated further.
- **GPU in WSL (tried, does not work yet).** WSL2 has `/dev/dxg` and Mesa's `d3d12` driver: with
  `GALLIUM_DRIVER=d3d12`, `glxinfo` reports "D3D12 (Intel(R) UHD Graphics)", OpenGL 4.1. A WoW64 Wine build from
  `/root/WE-integ` with Edge installed in a bare prefix gave:

  | Wine display driver | Edge flags | Result |
  |---|---|---|
  | x11 (XWayland) | GPU on | Edge crashes: Mesa "Failed to attach to x11 shm" |
  | wayland | GPU on | window shows but stays white (title is right) |
  | wayland | `--disable-gpu` | the same white window |
  | x11 (XWayland) | `--disable-gpu` | `about:gpu` draws, but the window takes no input (processes busy, not hung) |

  So acceleration is blocked by both display paths in WSLg, and Wine's Wayland driver does not paint Edge's window at
  all (not specific to the GPU). Not pursued: a Hyper-V VM with GPU paravirtualisation is the way to try real
  acceleration later; the QEMU VM stays the test bed (software rendering). Recipe for a retry: unset `DISPLAY`, set
  `WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir GALLIUM_DRIVER=d3d12`, start `msedge.exe
  --user-data-dir=C:\gpu-edge --no-first-run about:gpu`. Note `pkill -f msedge` inside a `bash -c '...msedge...'` kills
  the calling shell; start such tests from a script file.
