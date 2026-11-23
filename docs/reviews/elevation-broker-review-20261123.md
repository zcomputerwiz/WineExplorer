# Elevation broker design review: findings and fixes (2026-11-23)

Branch `claude/elevation-review-fixes`, from master `25c3a5e4` (Wine `f1f7317c`). The findings concern the lab-only
standard-user path of `wexelevate` (`WEXELEVATE_E1S18`, R8/R9) and the consent dialog. The shipping broker is still
built from `main.c` alone; every other unit compiles to nothing there (`scripts/test_broker_e1s18.sh` checks it).

| # | Finding | Fix | Commit | Tests |
|---|---|---|---|---|
| 1 | The standard path skipped `table_consume`, `verify_doc` and the document lease and ignored the prepared working directory; its replay list stopped recording after 64 answers (fail open). | `main.c` `commit_approved`: one post-approval commit for both paths (binding again, single use, caller alive, image and document unchanged, both leases armed, broker not stopping); the standard path runs it right after redemption (op `commit_request`; a table without it denies), arms the document lease for the child and uses the prepared working directory. The consumed-answer and burned-evidence lists grow (cap 2^20) and a record that cannot be kept denies. | `7cda3f4f` | 66-68 |
| 2 | The R8 decision sequence and the attempt loop existed twice (`e1s18_spawn_consent_dialog`, `r9b_attempt`). | `decision.c`: `r8_decide` over a link (dialog frames, rechecks, verifier) and `r8_attempt_loop`; the R8 broker's link uses D's pipes, B0's goes through the agent. | `86ed0ab0` | 69-71 |
| 3 | Two S1 digest functions; the standard path's own SHA-256 ignored encoder failures and hashed zero records on OOM. | `e1s_digest.h`: the one checked digest path for `e1s.h` and `standard.c`; a failed, empty or missing encoding is refused and the request denies before any dialog. | `6051d39a` | 65 |
| 4 | TokenUser read into a fixed buffer; the child's SID check skipped for length 0; a failed `RevertToSelf` ignored. | `token_checks.c`: TokenUser and TokenIntegrityLevel read at the reported size with the SID bounded by the bytes written; an empty or absent SID never matches; `wex_revert_to_self` ends the process when it fails. | `af432abd` | 63-64 |
| 5 | `.c` files included into `.c` files, statics of `main.c` used by other files, `struct wexelevate_reply` declared twice. | Units of their own with one shared header (`broker_internal.h`); `src/include/wexelevate_proto.h` holds the wire structures once; `cred_record.h` holds the answer record for `consent.c`. | `86e0f5aa`, `80b65c87` | units check in the broker test |
| 6 | `ConsentPromptBehaviorAdmin` when absent: 5 in both readers. Also `consent.c` showed click consent for 0 and 4, which the broker refuses. | `consent.c` defaults to 5 and shows click consent only for 2 and 5, as the broker; every other value asks for credentials (refused by the shipping dialog). | `39b3c098`, `5d4f0b8d` | |

## File layout (`src/programs/wexelevate`)

| File | Was | Contents |
|---|---|---|
| `main.c` | | the broker; the only shipping unit |
| `standard.c`, `standard.h` | `e1s18_broker.c`, `e1s18_broker.h` | the standard-user path (R8 broker) |
| `broker0.c` | `e1r9_broker0.c` | B0, the broker in session 0 (R9) |
| `decision.c`, `decision.h` | | the shared R8 decision sequence and attempt loop |
| `token_checks.c`, `token_checks.h` | | token and SID checks |
| `broker_internal.h` | | the units' shared header (`main.c` services under `WEXELEVATE_E1S18`) |
| `e1s_digest.h` | | the checked S1 digest path |
| `cred_record.h` | | the answer record, account-name rule and wiping (shared with `consent.c`) |
| `src/include/wexelevate_proto.h` | | the request and reply wire structures |

The older design documents keep the file names of their time.

## Production build

`main.c` keeps its shipping behaviour. `commit_approved` is `always_inline` there: the x86_64 production `main.o` is
byte-identical to master's; the i686 one differs by one `mov` turned `nop` in `client_thread`'s layout (equal section
sizes). Regenerate the i686 baseline of the broker test (`/tmp/main_prod32_baseline.o`) after reviewing this change.

## Follow-ups

- When `src/include/wexpreg.h` (GPO branch) is on master: both `ConsentPromptBehaviorAdmin` readers use
  `WEX_CONSENT_ADMIN_DEFAULT` and `WEX_CONSENT_ADMIN_SUPPORTED`.
- The shipping `main.c` keeps its own `token_user_sid` (same layout rule) until `token_checks.c` ships.
- `.c` files included into `.c` files elsewhere (`tools/`, `scripts/` tests) are outside this review.
- The live R8 broker's `prep_launch` converts the working directory into a 512-byte buffer, as the portable
  preparation does; a longer directory denies.
