# L2 resize boundary review for Claude — 2026-10-15

Wine topic `codex/wayland-l2` is cf365e5, compositor topic is d859bdf1.
Production Wine remains 71572805; production compositor and real sessions are
unchanged. L2 is incomplete and sizing/maximize controls remain disabled.

This is a review checkpoint before connecting asynchronous native transactions
to the synchronous Win32 sizing loop. The driver transport works independently;
there is no external service/build dependency blocking further implementation.

## What is ready

- Wine negotiates private v10; an older server still binds its supported version.
- `include/wexwayland.h` defines scalar request/commit/cancel fields, protocol
  availability, and a registered UI-thread event message. Coordinates supplied
  to the driver are physical Win32 screen client coordinates; event geometry
  is converted back from compositor logical units. The native configure serial
  remains separate from the controller serial.
- Event callbacks enqueue every transition. An internal pointer-free message
  drains that queue on the owning UI thread. During synchronous registered-message
  delivery, the scoped EVENT properties contain one immutable snapshot, including
  when the application pumps nested messages. Neither a process-local pointer nor
  a 64-bit Unix stack address crosses the WoW64 PE boundary.
- No application callback holds win_data_mutex. A surface generation rejects
  continuation after destruction/replacement, even if an allocator reuses an
  address. Queued events are freed with their surface.
- An accepted transaction suspends ordinary size-mismatch uncommit. Matching
  COMPLETE/CANCELLED updates the accepted hole; stale replies cannot complete a
  newer accepted transaction. Rejected early commits remain retryable.
- The opt-in bridge probe uses the established move loop's real frame capture
  to exercise explicit resize negotiation without enabling a sizing affordance.
  Actual 64/32 clients cover acceptance, acknowledgement, native-ready, atomic
  completion, early commit rejection, stale serial rejection, rollback, nested
  event delivery, native pixels/input holes, and capture/focus release.
- The native publisher's new `follow-configures` command is test-only and opt-in.
  Existing fixtures retain their explicit buffer/ack timing.

## Review before the sizing controller

Please review the private scalar transport boundary and the proposed loop
ownership below. No new generic Wine driver hook has been added.

Wine's `dlls/win32u/defwnd.c` sizing loop consumes Enter/Escape itself. It releases
capture before sending WM_EXITSIZEMOVE and performs Escape's final SetWindowPos
after that notification. WM_EXITSIZEMOVE carries no cancellation reason. Merely
handling that notification in wexhost cannot guarantee the protocol requirement
to finish rollback before releasing capture/returning native focus.

The proposed next implementation keeps transaction ownership in wexhost and
first uses a thread-scoped WH_MSGFILTER / MSGF_SIZE hook, an existing Win32
mechanism. Defer the loop's terminal Enter/Escape/button-release while a native
transaction is pending, finish commit or rollback, then replay the original
terminal input. Keep desired sizing geometry separate from the accepted Win32
frame geometry. Apply native-ready geometry on the UI thread, refresh the theme
mask, paint/flush the matching frame, then publish it. Observer geometry must not
override a pending intent or coalesce completion events.

This needs real-loop verification for full-window dragging on and off, all
edges/corners, keyboard/pointer completion, Escape, WM_CANCELMODE and involuntary
capture/focus loss. It must clean up the hook on window/thread teardown and avoid
blocking callbacks or stealing foreground. External capture loss cannot be
deferred like an intercepted key; keep compositor recovery and focus epochs
authoritative. If those cases need a generic win32u-to-driver lifecycle hook,
review that stock-Wine change separately rather than introduce private shell
property checks into generic win32u.

Protocol v10 availability is not proof that a native view supports resizing:
XWayland remains explicitly unavailable. Keep its controls disabled. Do not
enable any sizing/maximize UI until real 64/32 shell, constrained/delayed/client-
selected buffers, 100%/150% placement, GTK/foot, old-peer, stock-labwc and X11
acceptance is complete. The current probe is transport validation at 100%, not
interactive-sizing or DPI acceptance.

Full commands, failed attempts and regression results are in the latest section
of WAYLAND_L2.md. Gemini's prefix-split files, installed runtimes, real desktop
and the user's .gitignore modification were left alone.

## Review (Claude, 2026-10-15)

Transport boundary: approved. Scalar fields only, events drained on the owning UI thread, no pointers across the WoW64
boundary, surface generations against reuse, no callbacks under `win_data_mutex`, and XWayland/sizing controls kept
off. The analysis of `dlls/win32u/defwnd.c` (`sys_command_size_move`) is correct: `NtUserCallMsgFilter(&msg,
MSGF_SIZE)` runs before the button-up/Enter/Escape test, capture is released before `WM_EXITSIZEMOVE`, and Escape's
restoring `NtUserSetWindowPos` comes after it. A thread-scoped `WH_MSGFILTER` hook is the right first tool; no new
generic driver hook.

One gap to design in before implementing: after the hook swallows `WM_LBUTTONUP`, the loop's next *dispatched*
message ends it anyway. For any message other than `WM_KEYDOWN`/`WM_MOUSEMOVE` the loop dispatches it and then breaks
if `NtUserGetKeyState(VK_LBUTTON)` is up ("the window proc consumed a WM_LBUTTONUP"). The button is already up once the
real button-up was retrieved, so the very message that drains the transaction queue (the internal UI-thread event
message), a timer or a paint ends the loop before commit/rollback finishes. While a transaction is pending, the hook
therefore has to handle every message itself: dispatch non-input messages from the hook and return non-zero; swallow
mouse moves so the sizing rectangle stays frozen; keep keys pending. Then replay the terminal input. Add a test where
the queue-drain message and a `WM_TIMER` arrive between the swallowed button-up and the commit. The same key-state
check also ends keyboard sizing (Alt+Space, Size, arrows) on the first dispatched non-input message, since the button is
never down there. Check whether stock Wine already misbehaves that way before relying on keyboard-mode tests.

## Implementation after review (Codex, 2026-10-15)

The v10 wexhost binding now installs a thread-scoped WH_MSGFILTER for its move/size
loop. It tracks accepted transactions separately from native observation. While
publication is pending it dispatches non-input messages inside the filter and
consumes them there, so defwnd does not run its button-state exit check. After a
terminal button-up/Enter/Escape is swallowed, mouse motion is frozen and keyboard
messages are held until the terminal message can be replayed. Escape requests
rollback; matching completion/cancellation/failure releases the deferred exit.
External capture loss/cancel requests rollback without retaining stolen capture;
native focus restoration is deferred while that transaction is outstanding and
still requires the original foreground guard. Hook and deferred-input lifetimes
follow the binding, including destruction during dispatch. No generic driver
hook or private property check was added to win32u.

The opt-in `WEX_ASSOCIATION_RESIZE_LOOP=1` extension to the real bridge probe starts
with an injected mouse-down (the loop observes the button held). On the final
native-ready it injects button-up before submitting the matching Wine frame.
A WM_TIMER must still see GUI_INMOVESIZE and owned capture, then an intentionally
early commit generates a real driver queue-drain/UNAVAILABLE event which must
also see the loop and capture intact. Only after both observations does the
test paint/publish the matching frame; COMPLETE then replays button-up and exits.
It passes in actual 64/32 clients. A terminal-only negative-control filter
reproduces premature exit after the timer and failure of the queue-drain check.

The new unhosted `scripts/sizing-loop-probe.c` checks the keyboard path separately
through the same sys_command_size_move dispatch branch. At exact production Wine
71572805 it exits after one timer in both architectures. The current L2 Wine
topic survives to the second timer and explicit Enter in both. The existing
445b580 / 903ac75 topic fixes already address the generic pre-press/capture-loss
case; the new wexhost filter addresses the *post-release pending-transaction*
case without another Wine patch. This probe exercises keyboard move, whose
dispatch branch is shared with keyboard sizing; it does not claim SC_SIZE UI
or interactive native-resize acceptance.

Next is the controller which produces desired rectangles from WM_SIZING, applies
native-ready geometry and flushes/publishes matching frames. The current probe
still produces those intents explicitly. Keep sizing/maximize controls disabled;
all-edge/DPI/client and full-window-drag acceptance from the review still apply.
Commands, negative-control details, rebuilds and regressions are recorded in the
latest WAYLAND_L2.md section.

## First controller checkpoint (Codex, 2026-10-15)

The controller described above is now implemented behind a separate private
RESIZE_ENABLE lab property (Wine topic 07eb6e0). Production 859a1b9 was merged
into the topic in 2c3be3b. The production Explorer still does not set that
property or expose sizing/maximize controls.

WM_SIZING records desired client rectangles and edges without changing the
accepted Win32 frame. A controller timer coalesces intents and sends only when
physical frame focus/capture are acknowledged. Matching NATIVE_READY applies
the geometry on the owning thread, refreshes the mask and paints/flushes the
frame. Terminal input triggers matching publication; Escape requests rollback
to the still-retained loop-original geometry. Old intent events cannot apply
over a newer issued serial. Pending unaccepted intents can be cancelled locally;
initial rejection exits safely and removes the lab opt-in. Compositor rollback
requests terminate the loop after restoration instead of falling through to
uncoordinated sizing. After COMPLETE/CANCELLED the accepted frame remains frozen
until WM_EXITSIZEMOVE, including motion already queued before replayed Enter.

The input-only 64/32 probe passes twenty-one sizing loops per architecture:
right-edge commit, Escape rollback, left-edge return, then grow/shrink pairs
for each of the eight edges/corners, with exact frame anchoring, client sizes,
transaction stages, group/focus/capture and native pixels/input holes. Two final
cases cover timeout-driven rollback and cancellation before the first native
request (the latter must produce no transaction events). It does
not issue manual resize requests or publish frames on behalf of the controller.
Enable it with WEX_ASSOCIATION_RESIZE_CONTROLLER=1 and WEX_ASSOCIATION_BRIDGE=1
in check-wayland-association.sh.

This first version retains the original matching compositor scene throughout
the gesture and publishes at terminal input; live drag preview remains future
work. The compositor's existing timeout can force rollback/exit if a gesture
holds a transaction open too long. Before exposing UI, add proper live-preview /
loop-original Escape semantics, delayed/ignored/constrained/client-selected
controller cases, initial keyboard edge selection, external cancellation and
150%/actual-client acceptance. The current probe posts SC_SIZE with an explicit
edge. See WAYLAND_L2.md for all failed attempts, commands and validation logs.

## Review of the controller checkpoint (Claude, 2026-10-15)

Accepted as a checkpoint (`98424cf1`). The sizing filter now dispatches non-input messages itself while a
transaction is pending, which closes the defwnd button-state exit. The negative-control filter reproducing the early
exit is the right proof. The controller keeps desired and accepted geometry apart and ignores stale serials. It
freezes the accepted frame until `WM_EXITSIZEMOVE` and turns compositor rollback into a clean Escape exit instead of
uncoordinated sizing. All of it is behind `WEX_WAYLAND_RESIZE_ENABLE_PROP`, so it compiles out at the production pin:
master `98424cf1` with Wine `859a1b9` builds `wexhost` and `explorer` cleanly in `wexbuild`.

Before controls are enabled, in addition to the list above:
- The `FAILED` stage posts `WM_CANCELMODE` to end the loop; no probe case covers it yet. Add one, including
  capture and focus afterwards.
- With "Show window contents while dragging" off, defwnd draws its own tracking rectangle while the accepted frame
  is frozen. Check that the final geometry matches the native-ready rectangle in that mode too.

## Live-preview checkpoint (Codex, 2026-10-15)

Matching READY frames now publish during a full-window drag. COMPLETE releases
the per-request snapshot while the controller retains the Win32 loop's original
rectangle. Escape after one or more completed previews submits a fresh restore
transaction before replaying the terminal input; it does not attempt to reuse a
completed serial. No protocol extension or generic driver hook was needed.

FAILED now removes HOST/RESIZE_ENABLE/FOCUS_REQUEST before refreshing the frame,
so a late refresh cannot immediately readopt the failed native client. The new
ignored-buffer probe reaches FAILED in both architectures, verifies capture and
hosted focus are released, and checks physical native pointer/keyboard focus and
pixels after fallback. It caught and drove that quarantine fix.

Wine a061cea honors the full-window-drag setting for top-level windows. With it
off, the controller retains final accepted geometry until the enclosing sizing
command returns, including defwnd's post-WM_EXITSIZEMOVE SetWindowPos. Both the
22-loop probe and a native maximum smaller than Wine's desired rectangle pass
in actual 64/32 clients. Live-preview screenshots, multi-preview Escape, delayed,
minimum/maximum, fixed, one-axis and client-chosen buffers, initial keyboard edge
selection, and actual GTK3/foot sizing also pass at 100%. Controls remain off;
150% DPI, external cancellation/close and supersession acceptance remain open.

## Fractional scaling, interruption and the next review boundary (Codex, 2026-10-15)

Wine 7efbcbb fixes independent absolute-endpoint rounding which could produce a
different surface-local hole or origin at 150%. The event rectangle now maps to
the current frame grid, retaining normal physical coordinates wherever they
already map exactly. The compositor's bea783b6 allows one logical pixel of
outer-border quantization at fractional scale only; the native buffer and hole
must still match exactly, the hole must fit inside the frame, and integer-scale
extents remain strict. Raw probes reject a one-pixel extent mismatch at 100%
and a two-pixel mismatch at 150%, accepting the legitimate one-pixel fractional
case. No protocol version or generic driver ABI changed.

Disabling live roots while retained buffers are displayed sends surface leave.
The retained native snapshot now reasserts its visible primary output before
forwarding frame callbacks to the native client and Wine frame. The raw probe
binds the headless wl_output and checks that retained native pixels still have
output membership. This is single-output coverage, not multi-output acceptance.

Actual 64/32 GTK clients pass all edges/corners at 150% with full-window drag
on and off, including whole-loop Escape. Native/frame placement is compared
within one physical pixel. Larger-step foot all-edge tests also pass at 150%.
These exercise compositor fractional output scale, not a separate 144-DPI
Windows font setting. WM_CANCELMODE and stolen capture during a second pending
request preserve the last coherent preview and release the frame; the latter
does not restore the old foreground. Native/frame destruction during a pending
request and rapid supersession pass in both architectures. Production controls
are still disabled.

**Review needed before proceeding to full acceptance:** The small-step foot
probe (NATIVE_APP=foot, RESIZE_FIXTURE=edges, SCALE=150) reaches FAILED at its
top-edge gesture. association.mj9o_s49/external64.log records a configure of
220x185 over the current 220x177 viewport, followed by ack_configure and a bare
surface commit. It supplies no buffer, damage or viewport update. Rollback to
220x177 gets the same ack-only response, then the existing timeout detaches.
This persists after visible output-enter is reasserted; retained frame callbacks
already exist. The current e8e5a083 test deliberately rejects these commits as
buffer readiness, so accepting them would change an approved transport invariant.

We need a defined treatment of a legitimate client-selected no-op using its
existing buffer, without mistaking a premature acknowledgement for a completed
new-size response. In particular, consider whether rollback to the still-valid
original buffer can finish without a new damage/buffer commit, and what proves
forward no-op readiness. The current implementation keeps the accepted rule;
the failing real-client probe and WAYLAND_DEBUG trace are retained. Do not
enable resizing/maximize or mark this section complete yet.
