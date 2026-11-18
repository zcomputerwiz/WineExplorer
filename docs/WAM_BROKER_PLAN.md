# Account broker (WAM step 3): initial implementation plan

Plan of 2026-11-17 (Integration), the working order for [WAM_PLAN.md](WAM_PLAN.md) step 3. It builds on the design in
[WAM_BROKER_DESIGN.md](WAM_BROKER_DESIGN.md) (parts, flows, cache, security, tests) and the research in
[WAM_RESEARCH.md](WAM_RESEARCH.md); it does not repeat them. What it adds: how Windows is structured, from Microsoft's
public documentation only, the patterns chosen from that, and the order of work. Status: proposal, for session 0
(owner of step 3) to adopt or change. No Wine code has been written for it.

## Why now

With the WebView2 Runtime registered (the optional component), Microsoft Copilot's "Continue with Microsoft" opens its
sign-in window and then calls `IWebAuthenticationCoreManagerInterop::RequestTokenForWindowAsync` (prompt type 1; client
`0ec893e0-5785-4de6-99da-4ed124e5296c`; provider `https://login.microsoft.com`, authority `consumers`). Wine's
diagnostic provider answers `ProviderError` (0x80004001) and Microsoft's page shows "Something went wrong [4wp0s]",
code -2146893805 (the design document's "Update 2026-11-17" has the trace). Cryptography, TPM and library loading were
ruled out. Answering that call with a real sign-in is this plan.

## How Windows is structured (public documentation only)

Sources: Microsoft Learn pages on the Web Account Manager, `Windows.Security.Authentication.Web.Core`,
`Windows.Security.Credentials`, `Windows.Security.Authentication.Web.Provider` and MSAL.NET's WAM page. Nothing from
Windows itself was read, and the clean-room rules of the design document hold. Two reference pages (the prompt-type
enumeration and the account monitor) could not be retrieved, so their details are not assumed here.

**Three roles.** The calling program builds a request and reads the result. The system (WAM, with the TokenBroker
service behind it) shows the account picker, remembers accounts and runs the silent and interactive flows. A web account
provider (a plug-in) names itself (id, display name, icon, authority) and issues tokens for a scope.

**The provider side is operation-shaped.** A provider is handed one operation object per kind of request: add account,
delete account, get token silently, manage account, request token (interactive), retrieve cookies, sign out account;
a provider ignores kinds it does not know. Each operation reports success or failure through a report interface (silent
operations and operations that need UI differ). Requests and responses have fixed shapes (`WebProviderTokenRequest`,
`WebProviderTokenResponse`).

**Accounts belong to a provider and an authority.** A provider is found by id, optionally with an authority (for the
Microsoft provider `https://login.microsoft.com` with `consumers` or `organizations`; a tenant for work accounts) and
optionally for a user; "system" providers serve device-wide tokens. An account has an opaque `Id`, a `UserName`, a state
(none, connected, error) and its provider. The program, not the system, persists the provider id and account id it wants
to use again, and later finds the account by them. `FindAllAccounts` lists what the user has added for a provider
(optionally for a client id).

**Requests.** A token request names provider, scope, client id and a prompt type, and carries property maps (one the
provider sees, one the system ignores for caching) and a correlation id. Silent never shows UI. Interactive may; for
desktop programs it is parented to a window handle given through the interop interface. Outcomes are six statuses:
success, user cancelled, account switched, user interaction required (silent only), provider not available, provider
error. A response holds the token, the account, a provider error and a property map.

**Behaviours programs rely on.** MSAL tries the system account, then cached accounts, then interactive; it falls back
to a browser whenever WAM cannot be used (unsupported platform or authority, or the class missing). The interactive
UI must run in an active, interactive user session. Programs use a broker redirect URI of the form
`ms-appx-web://microsoft.aad.brokerplugin/{client_id}`. The WAM error list shows the failures programs expect to see:
interaction required, account not found, key-set errors for TPM-bound keys.

**Not public**, so ours to choose: how a provider is registered and found, how requests reach it, which process draws
its UI, how the caller is identified, and where accounts and tokens are stored.

## Patterns chosen

1. **Keep Windows' three roles, and its seam.** The Wine module in the program's process stays the thin client (exists);
   a per-user broker process does the work; providers plug into the broker. The module answers exactly as today when no
   broker is reachable, so programs with a browser fallback keep working (that is also how Windows behaves when WAM is
   unusable).
2. **Provider interface = Windows' operation kinds.** Inside the broker a provider implements
   `get_token_silent`, `request_token`, `add_account`, `delete_account`, `sign_out`, `manage_account`,
   `retrieve_cookies`; unknown kinds return "not supported". Providers are looked up by (provider id, authority), as on
   Windows. First provider: Microsoft accounts (OAuth 2.0 code flow with PKCE, public documentation). Later ones fit
   without changing the pipe: a work-account provider that delegates to a separate Himmelblau daemon (step 4), and an
   in-tree **test provider** (below).
3. **One message pipe, versioned, correlated.** `\pipe\WineTokenBroker\<user SID>`; length-prefixed messages with a
   protocol version, a request id and the request's correlation id; one message kind per operation above plus `cancel`
   and `account_changed` (for monitors, later). Replies carry a status from Windows' six and a provider error
   (code, message, property map). Nothing else crosses: no tokens in logs, no secrets in errors.
4. **Interactive flow as an explicit state machine** in the broker: `created -> window_open -> code_received ->
   redeemed -> done`, with `cancelled` and `failed` reachable from any state and a timeout. One interactive request per
   calling window at a time. Destroying the parent window or a `cancel` message ends it as `UserCancel`. Each
   transition is testable without a window.
5. **Async the way Wine's module already does it.** The client keeps its `IAsyncOperation` objects; each request is a
   blocking pipe exchange on a worker thread that completes the operation. No new async machinery.
6. **Accounts are provider-scoped and persisted by the broker's cache, not by id guessing.** Account `Id` is the
   account's stable object identifier (opaque to programs), `UserName` its sign-in name, `State` connected or error.
   The cache is keyed by account, authority, client id and scope set, as in the design document; sign-out clears the
   account's tokens for that program's client.
7. **Status mapping is a table, not scattered code.** Server error to Windows status plus the `0xCAA...` codes the
   diagnostic provider already uses; anything unmapped is `ProviderError` with the server's error text kept.
8. **Staged switch, off until it works.** `HKCU\Software\Wine\WebAuthentication` `Broker` = 0 by default through steps
   1-2; the diagnostic provider stays available. Turned on by default only after the owner's real sign-in succeeds.
9. **A fake provider instead of a network switch.** The test provider implements the same interface against a local
   test authority (the design document's Python OAuth server), so the pipe, the state machine and the cache are tested
   through the real code path, and the production provider has no test hook to misuse.
10. **Own the protocol code.** The sign-in protocol is public OAuth and written here from Microsoft's identity platform
    documentation, with MSAL (MIT) as the reference for property names. No LGPL-3.0 or GPL-3.0 library code is linked
    (licences are in the research document's third pass).

## Status

- 2026-11-18, session 0: the plan is adopted as written. Step 1 is written (not yet built or run): the protocol
  (`wine/include/wine/tokenbroker.h`), the broker (`src/programs/tokenbroker`: per-user pipe for the user's SID only,
  a second broker ends, idle exit after 10 minutes, never in session 0, the client's token user checked) and the
  client (`dlls/windows.security.authentication.web.core/broker.c`: switch `Broker`, the broker started on demand,
  its server checked, fallback to the answers without it). The broker answers as the diagnostic provider in step 1.

## Order of work

| # | Deliverable | Proves | Owner (proposed) |
|---|---|---|---|
| 1 | Broker process skeleton, pipe, message framing, version and correlation, `cancel`; client forwards the diagnostic calls over it | the seam; no-broker fallback unchanged | session 0 |
| 2 | Provider interface and registry; the test provider and test authority; silent and account calls | patterns 2, 6, 9 | session 0 |
| 3 | Interactive state machine, still against the test provider (window driven by a hook) | patterns 4, 7 | session 0 |
| 4 | Microsoft account provider: authorize URL, PKCE, code exchange, refresh; WebView2 sign-in window parented to the caller's window | the real call, by hand | session 0, Integration (VM, retests) |
| 5 | Copilot acceptance: owner signs in by hand in the Integration VM; Copilot shows its signed-in state | the goal | owner, Integration |
| 6 | Missing classes the sign-in needs, only if step 5 still fails after step 4: `Windows.Internal.Security.WebAuthentication.AuthenticationManager`, `AppCapabilityAccess.AppCapability`, COM class `{daa52b27-8897-50af-ada5-c6c71bb64e17}` | whether they matter | generic ones UWP worker; the internal authentication class session 0 |
| 7 | `AccountsSettingsPane`, monitors, `login.windows.local` default account, Himmelblau provider | WAM_PLAN steps 4-5 | later |

Each step is a Wine change listed in `STOCK_CHANGES.md` with its tests, run before the next.

## Risks and stop conditions

- **The error may have a second cause.** If step 4 completes a sign-in and the page still fails, the missing classes in
  step 6 or a server-side check (the redirect URI is only validated after sign-in, see the design document) are next;
  the plan stops at that point and reports, as the design document's phase 0 rule says.
- **Parenting across processes.** The sign-in window lives in the broker but is owned by the caller's window; if Wine's
  window manager cannot do that cleanly, the window opens unowned (a known cosmetic limit, to be checked in step 4).
- **Interactive session required.** On Windows WAM works only in an active interactive session; the per-user broker
  matches that and never runs in session 0.
- **Secrets.** Refresh tokens are the only secret; the cache rules of the design document apply (Secret Service if
  present, otherwise a 0600 file). Another program of the same user can ask the broker for tokens; Windows binds them to
  the caller's package, Wine has none. Stated, not solved.

## Coordination

Session 0 owns step 3 (WAM_PLAN, B25). The UWP worker owns the generic `Windows.*` classes. I have asked session 0 to
confirm this split and to relay it to the UWP worker; until it answers, nobody writes Wine code from this plan.
Integration provides the test VM (`D:\WSL\wex-iso\open-copilot.ps1` opens it) and the retests.
