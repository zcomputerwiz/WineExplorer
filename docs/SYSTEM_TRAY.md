# Notification area (2026-10-09)

Hide inactive icons enables the per-icon behaviors in Customize Notifications:
Hide when inactive, Always hide, and Always show. Application NIS_HIDDEN remains
independent of these user preferences. Always hide applies immediately, even when
an application changes its icon. Turning off Hide inactive icons shows all icons
that their applications have not hidden. Expanding the chevron temporarily shows
user-hidden icons; collapsing restores their behavior. The expanded area closes
after five seconds with the pointer outside, unless mouse capture is active.
The XP reference VM was observed collapsing after the pointer left its tray.

The inactivity threshold is five minutes (a WineExplorer policy, not a measured
XP threshold), replacing eight seconds. Changed images, balloons, and clicking an
icon count as activity. Identical icon/tooltip refreshes do not reset the timer.
Invalid saved behavior values fall back to Hide when inactive. Customize edits
are committed on OK; Cancel leaves them unchanged. Restore Defaults selects Hide
when inactive for the current items. Image lists are released when replaced or
when the dialog closes. The dialog currently lists current registered icons;
XP's historical Past Items list is not implemented.

The Show clock checkbox previews its pending value and only updates live settings
on Apply/OK. Cancel discards it. Auto-hide was verified hiding on desktop focus
and revealing at the bottom screen edge.

Wired network activity already has four separate resources: idle (1001), receive
(1004), transmit (1005), and both (1006). Selection compares packet counter deltas
on the same interface. Counter resets, reconnects, visibility changes, and
interface changes establish a new baseline. A first packet from zero is now
recognized. Icons copied from shared resource handles are owned by the applet,
so destroying an old displayed icon does not destroy the shared resource icon.
The 500 ms sample can legitimately contain both directions, especially TCP
traffic and SSH diagnostics; it does not manufacture single-direction animation.

Validation: Explorer built and was deployed in the Debian VM. Search-to-desktop
focus and clearing, clock Cancel, Always hide, restoring notification defaults,
expanded-area automatic collapse, and taskbar auto-hide/reveal were exercised through the UI. A probe using the
production activity classifier passed zero baseline, transmit-only, receive-only,
both, idle, counter reset, interface switch, and receive-after-switch cases.

Auto-hide reveal also invalidates the whole tray and its children, with
SWP_NOCOPYBITS. Reusing off-screen pixels had left the first two revealed rows
black. Pixel checks confirmed the repaired rows match the normal taskbar at the
same y coordinates (570 and 571 in the 800x600 locked Luna test).
