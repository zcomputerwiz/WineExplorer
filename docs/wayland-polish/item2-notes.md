<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Item 2: bounded real-program check passes (2026-10-17)

No compositor/rendering change was justified. The old whole-screen OCR fails
to report Yes/No even when the actual screenshot shows both buttons. Qt's
green/red standard button icons interfere with recognition. The first new
runner reproduced that same false failure at initial display, before any
taskbar switch. A crop containing both buttons still missed Yes; individual
label crops excluding the icons recognize both labels reliably.

The corrected runner opens QTerminal's actual Exit dialog at 150%, switches
to foot through the taskbar, switches back, then checks both rendered labels
within five seconds and clicks Yes to close the program:

- Current development runtime: both labels visible in **0.466 s**.
  [Raw log](logs/item2-current.log), [screenshot](screenshots/qt-dialog-after-switch.png).
- Preserved pre-polish ISO installation, Wine b1d3ab0 / compositor 2bde47ee /
  original owned tray code: both labels visible in **0.481 s**.
  [Raw log](logs/item2-baseline.log), [screenshot](screenshots/qt-dialog-baseline-after-switch.png).

This establishes that the bounded acceptance passes without a rendering fix;
it does not prove that every possible Qt damage sequence is bug-free.
The previous reported delayed repaint could not be reproduced with this
properly measured sequence. Source: `installer/tests/check-wayland-polish.py
VM PASSWORD_FILE OUTPUT_DIR qt-dialog`.

The initial whole-screen OCR failure remains in
[its raw log](logs/item2-fullscreen-ocr-failure.log). The final label crops and
screenshots, not a long sleep or a native repaint request, determine success.
The baseline VM received only test packages qterminal/wlr-randr and the test
helpers; its Wine/compositor/owned runtime was not replaced. It is shutting
down normally after the check. The development session is at 150% with the
real maximized-foot scale failure left visible for item 3.
