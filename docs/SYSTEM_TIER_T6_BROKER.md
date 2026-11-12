<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# System tier T6: the elevation decision in session 0, and session ids (design)

Date: 2026-11-12. Status: design, not implemented. Context: [SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md),
[SYSTEM_TIER_T5_SERVICES.md](SYSTEM_TIER_T5_SERVICES.md), the E1 elevation work ([E1_S18E_R8_SPEC.md](E1_S18E_R8_SPEC.md),
[E1_S18E_WORK_ORDERS.md](E1_S18E_WORK_ORDERS.md)).

## Goal

On Windows the elevation decision is made in session 0 by the Application Information service (AppInfo, running
as SYSTEM): it reads the policy, starts `consent.exe` on the user's secure desktop, checks the credentials through
LSA and creates the elevated process in the user's session with the full token. Nothing of that runs with the
user's own rights.

WineExplorer's broker (`wexelevate`, E1) runs today as a system-role process inside the user's own server: it is
protected by the launcher (system group, not dumpable) and by the server's checks, and the password check is a
Unix service (the verifier) outside Wine. T6 moves the decision part to session 0, so that it is one process for
all sessions, as AppInfo is.

## What has to stay in the user's session

* The consent dialog: it is drawn on the user's secure desktop, which belongs to the user's server and display.
* The elevated program: it runs in the user's session, under the user's server, with the elevated token.
* So a token for the user's server must be created there; session 0 cannot hand a token object to another server.

## Design

### Split of the broker

* **Decision in session 0** (the AppInfo part): policy (`ConsentPromptBehaviorAdmin`, `PromptOnSecureDesktop`,
  `EnableStandardUserElevation`, the lockout policy; all now in session 0's registry), the request record, the
  talk to the verifier (R8), and the result.
* **Agent in the session** (small, system role, started by the user's server through the launcher as today): it
  receives the request from the program (`ShellExecute runas` and manifests, as today), forwards it to session 0,
  starts the consent dialog on order of session 0, and creates the elevated process when session 0 says so.

### Trust in the other direction

T1 to T5 have session 0 trusting the user's launcher-started server. T6 needs the user's server to trust orders
from session 0 ("create the elevated token for request N for account A"). Rule: the user's server accepts such an
order only on the link it opened itself to the configured `session0_socket`, after checking the peer with
`SO_PEERCRED`: the configured `session0_user` account and the system group. The order names a request the agent
in that session registered, so session 0 cannot create tokens for requests nobody made in that session.

### The R8 protocol

R8's states and messages stay; the broker's role (B) moves to session 0, the dialog (D) and the server's
endorsement (S) stay in the session, and the verifier (V) is unchanged. The endorsement record gets the session's
server identity so that session 0 can bind a decision to one session. This is a protocol revision (R9 of the E1
spec) and needs the E1 review procedure (Codex review of the trust changes).

### Session ids

Session 0 becomes session 0 in the token sense too: it gives each logon a session id (1, 2, ...) when the user's
server first connects with its hive (T4a) and the user's server uses it instead of the constant 1
(`default_session_id`). Programs see their real session id (`ProcessIdToSessionId`, `WTSGetActiveConsoleSessionId`
in the session's own server), and session 0's registry-only clients carry it, so that per-session state can later
be kept per session inside session 0 instead of in each session's server.

## Order of work

| Step | Content |
|---|---|
| T6.1 | Session ids from session 0 per logon; `default_session_id` replaced in user servers with session 0 |
| T6.2 | The reverse link: the user's server accepts orders from session 0 on its own link, peer checked |
| T6.3 | R9 of the E1 spec: B in session 0, D and S in the session; review |
| T6.4 | The agent in the session; the decision in session 0; the lab suite's R8 cases run against it |

## Open questions

* Whether the verifier should become part of session 0 (a Wine program as SYSTEM) or stay a Unix service. Today's
  answer: stay a Unix service; PAM is a Unix interface.
* Fast user switching: two sessions on one display; out of scope until the display manager supports it.
