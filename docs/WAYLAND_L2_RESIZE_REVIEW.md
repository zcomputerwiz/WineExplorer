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
