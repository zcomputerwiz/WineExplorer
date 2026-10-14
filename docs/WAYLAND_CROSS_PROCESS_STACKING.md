<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Private protocol extension for cross-process stacking

Status: user approved the compositor extension on 2026-10-13. Implementation and
acceptance tests are in progress on codex/wex-window-handles in the Proton VM
clone. Gemini's WSL distributions remain untouched.

The v1 place_above/place_below sibling arguments are Wayland objects. They can
reference only objects on the caller's connection. All shell Wine processes
connecting to wex-shell-0 does not make their objects interchangeable.
Wine's HWND list spans processes; current v1 integration can preserve layers
and same-process relative order, but cannot express a foreign-process sibling.

## Version 2

Keep every v1 request unchanged. Advertise manager/window version 2 and add:

- Window event handle(string), emitted once with an opaque compositor-generated
  random token identifying that view for its lifetime.
- Window requests place_above_handle(string) and place_below_handle(string).
  Resolve the token to a live private window, then reuse existing same-layer
  stacking code. Unknown/stale handles are harmless no-ops; cross-layer requests
  cannot violate desktop/normal/topmost/shell bands. No imported Wayland objects.
- Window request restore(), which clears minimization without activating or
  raising the view. Wine can restore a background window while foreground-lock
  rules deny activation; using activate() for that case would steal focus.

The existing privileged-socket gate applies. Normal-socket clients cannot obtain
these controls. Do not accept client-chosen names or expose raw pointer values.

Wine publishes the token via an NtAddAtom session-global atom stored in a scalar HWND
property, removes/releases it with its private window object, and reads sibling
tokens through the shared Win32 HWND/atom APIs. Only strings cross Wayland
connections. Re-evaluate local stacking when a newly mapped neighbour's handle
arrives, rather than pretending a missing object belongs to this connection.

## Validation and compatibility

Add compositor tests with two clients and three normal windows, checking both
relative orders, stale handles, cross-layer immunity and normal-socket isolation.
Re-run the existing 25 compositor checks. In Wine, exercise SetWindowPos with
a sibling in another process, SWP_NOACTIVATE, HWND_BOTTOM, topmost transitions,
and activation/minimize/restore. Confirm v1/stock-labwc fallback and X11.
Use the Proton VM clone and a separate codex branch; do not enter Gemini's WSL
distributions or alter ISO/session defaults.
