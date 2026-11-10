#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The Advanced Options menu: XP's F8 menu, on tty1 when the boot has wexmode=menu (docs/SAFE_MODE.md).

wex-advanced-options.service runs it before basic.target, so no timer, socket or service of the normal boot has
started yet. A choice writes the mode to /run/wineexplorer (gone at the next boot), reloads systemd, whose
wex-bootmode-generator then reads it, and starts default.target. Nothing on disk changes, except with Last Known Good
Configuration: wex-lastgood puts the machine registry's control set from the last successful start back first.

With wexfailed=1 as well (GRUB found that the last start did not finish), it shows XP's "did not start
successfully" screen instead: the three Safe Modes, Last Known Good and Start Normally, which a 30-second countdown
chooses.

Installed as /usr/local/sbin/wex-advanced-options. The WEX_* variables let the tests use other paths.
"""

import curses
import math
import os
import subprocess
import sys
import textwrap
import time

STATE_DIR = os.environ.get("WEX_BOOTMODE_STATE_DIR", "/run/wineexplorer")
SETUP_STAGE = os.environ.get("WEX_BOOTMODE_SETUP_STAGE", "/etc/wineexplorer/setup/stage")
PANIC_SYSCTL = os.environ.get("WEX_PANIC_SYSCTL", "/proc/sys/kernel/panic")
CMDLINE = os.environ.get("WEX_BOOTMODE_CMDLINE", "/proc/cmdline")
LASTGOOD = os.environ.get("WEX_LASTGOOD_HELPER", "/usr/local/sbin/wex-lastgood")
OS_NAME = "WineExplorer"
LASTGOOD_LABEL = "Last Known Good Configuration (your most recent settings that worked)"
COUNTDOWN = 30      # seconds, as XP's failure screen (its boot.ini timeout)

# The failure screen's explanation. XP's layout (three paragraphs, then the choices); our own wording.
FAILURE_TEXT = (
    f"{OS_NAME} did not start successfully last time. A recent hardware or software change might have "
    "caused this.",
    "If your computer stopped responding, restarted by itself, or was shut down to protect your files and "
    "folders, choose Last Known Good Configuration to go back to the most recent settings that worked.",
    "If the last start was interrupted by a power failure or by the Power or Reset button, or if you are not "
    f"sure what caused the problem, choose Start {OS_NAME} Normally.",
)

MODES = ("normal", "safe", "safenet", "safecmd")
OPTIONS = ("bootlog", "debug", "norestart", "lastgood")
NO_LASTGOOD = "no start has succeeded since Setup"


class Item:
    """One line of the menu. action: "boot" (a mode or an option, then the OS list), "start", "reboot",
    "choices"; unavailable: why the item is grayed, or None."""

    def __init__(self, label, action, mode="normal", option=None, unavailable=None):
        self.label = label
        self.action = action
        self.mode = mode
        self.option = option
        self.unavailable = unavailable

    @property
    def selectable(self):
        return self.unavailable is None


def menu_items(setup_pending, lastgood=False):
    """The XP list, with what does not apply here grayed. None is a blank line. lastgood: a copy to go back to."""
    safe = "Setup has not finished" if setup_pending else None
    return [
        Item("Safe Mode", "boot", mode="safe", unavailable=safe),
        Item("Safe Mode with Networking", "boot", mode="safenet", unavailable=safe),
        Item("Safe Mode with Command Prompt", "boot", mode="safecmd", unavailable=safe),
        None,
        Item("Enable Boot Logging", "boot", option="bootlog"),
        Item("Enable VGA Mode", None, unavailable="needs an X fallback without the modesetting driver"),
        Item(LASTGOOD_LABEL, "boot", option="lastgood", unavailable=None if lastgood else NO_LASTGOOD),
        Item("Directory Services Restore Mode (Windows domain controllers only)", None,
             unavailable="does not apply"),
        Item("Debugging Mode", "boot", option="debug"),
        Item("Disable automatic restart on system failure", "boot", option="norestart"),
        None,
        Item(f"Start {OS_NAME} Normally", "start"),
        Item("Reboot", "reboot"),
        Item("Return to OS Choices Menu", "choices"),
    ]


def failure_items(setup_pending, lastgood=False):
    """XP's "did not start successfully" list: the Safe Modes, Last Known Good and Start Normally only."""
    safe = "Setup has not finished" if setup_pending else None
    return [
        Item("Safe Mode", "boot", mode="safe", unavailable=safe),
        Item("Safe Mode with Networking", "boot", mode="safenet", unavailable=safe),
        Item("Safe Mode with Command Prompt", "boot", mode="safecmd", unavailable=safe),
        None,
        Item(LASTGOOD_LABEL, "boot", option="lastgood", unavailable=None if lastgood else NO_LASTGOOD),
        None,
        Item(f"Start {OS_NAME} Normally", "start"),
    ]


def start_failed(path=None):
    """True when GRUB found that the last start did not finish: wexfailed=1 on the command line."""
    try:
        with open(path or CMDLINE, encoding="ascii", errors="replace") as f:
            return "wexfailed=1" in f.read().split()
    except OSError:
        return False


def lastgood_saved(run=subprocess.run):
    """True when wex-lastgood has a control set from a successful start to go back to."""
    try:
        return run([LASTGOOD, "status"], check=False).returncode == 0
    except OSError:
        return False


def first_selectable(items):
    return next(i for i, item in enumerate(items) if item is not None and item.selectable)


def move(items, index, step):
    """The next selectable line up (step -1) or down (step 1); stays put at either end, as NTLDR does."""
    i = index + step
    while 0 <= i < len(items):
        if items[i] is not None and items[i].selectable:
            return i
        i += step
    return index


def write_state(name, value):
    """Write /run/wineexplorer/<name> atomically, readable by everyone (the greeter and sessions ask)."""
    os.makedirs(STATE_DIR, mode=0o755, exist_ok=True)
    path = os.path.join(STATE_DIR, name)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="ascii") as f:
        f.write(value + "\n")
    os.chmod(tmp, 0o644)
    os.replace(tmp, path)


def apply_choice(mode, option=None, run=subprocess.run):
    """Continue this boot in MODE, with one OPTION of the menu."""
    if mode not in MODES:
        raise ValueError(f"unknown mode {mode!r}")
    if option is not None and option not in OPTIONS:
        raise ValueError(f"unknown option {option!r}")
    write_state("bootmode", mode)
    if option:
        write_state("bootoption", option)
    if option == "norestart":
        with open(PANIC_SYSCTL, "w", encoding="ascii") as f:
            f.write("0\n")
    if option == "debug":
        run(["systemd-analyze", "log-level", "debug"], check=False)
    if option == "lastgood":
        # before any Wine program reads the machine registry; a failure starts normally (main)
        run([LASTGOOD, "restore"], check=True)
    # the generator runs again and reads the mode: Safe Mode targets and masks, or nothing for a normal start
    run(["systemctl", "daemon-reload"], check=True)
    units = ["default.target"]
    if option == "bootlog":
        units.append("wex-bootlog.service")
    run(["systemctl", "--no-block", "start", *units], check=True)


def reboot(run=subprocess.run):
    run(["systemctl", "reboot"], check=False)


class Screen:
    """Text mode drawing in the NTLDR style: light gray on black, the choice as a white bar."""

    LEFT = 4        # headings
    ITEM = 8        # menu lines
    BAR = (7, 74)   # the highlight bar's first and last column

    def __init__(self, stdscr):
        self.scr = stdscr
        curses.curs_set(0)
        stdscr.keypad(True)
        # Draw every key at once: by default curses defers drawing while input is waiting, and the F8 presses
        # that opened the menu are still arriving. A short escape delay keeps arrow keys from waiting on it.
        curses.typeahead(-1)
        if hasattr(curses, "set_escdelay"):
            curses.set_escdelay(50)
        self.blue = curses.A_NORMAL
        self.gray = curses.A_DIM
        if curses.has_colors():
            curses.start_color()
            curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLACK)
            curses.init_pair(2, curses.COLOR_BLUE, curses.COLOR_BLACK)
            curses.init_pair(3, curses.COLOR_BLACK, curses.COLOR_BLACK)
            stdscr.bkgd(" ", curses.color_pair(1))
            self.blue = curses.color_pair(2) | curses.A_BOLD
            # dark gray: the Linux console shows bold black as bright black, and does not render A_DIM
            self.gray = curses.color_pair(3) | curses.A_BOLD

    def flush_input(self):
        curses.flushinp()

    def put(self, row, col, text, attr=curses.A_NORMAL):
        rows, cols = self.scr.getmaxyx()
        if 0 <= row < rows and col < cols - 1:
            self.scr.addstr(row, col, text[:cols - 1 - col], attr)

    def bar(self, row, text):
        width = self.BAR[1] - self.BAR[0] + 1
        self.put(row, self.BAR[0], (" " + text).ljust(width), curses.A_REVERSE)

    def bottom(self):
        return self.scr.getmaxyx()[0] - 1

    def advanced_menu(self, items, index):
        self.scr.erase()
        self.put(1, self.LEFT, "WineExplorer Advanced Options Menu")
        self.put(2, self.LEFT, "Please select an option:")
        for n, item in enumerate(items):
            row = 4 + n
            if item is None:
                continue
            if n == index:
                self.bar(row, item.label)
            else:
                self.put(row, self.ITEM, item.label, curses.A_NORMAL if item.selectable else self.gray)
        self.put(self.bottom() - 1, self.LEFT, "Use the up and down arrow keys to move the highlight to your choice.")
        self.scr.refresh()

    def os_choices(self, chosen_label):
        self.scr.erase()
        self.put(1, self.LEFT, "Please select the operating system to start:")
        self.bar(4, OS_NAME)
        bottom = self.bottom()
        self.put(bottom - 6, self.LEFT, "Use the up and down arrow keys to move the highlight to your choice.")
        self.put(bottom - 5, self.LEFT, "Press ENTER to choose.")
        self.put(bottom - 3, self.LEFT,
                 f"For troubleshooting and advanced startup options for {OS_NAME}, press F8.")
        if chosen_label:
            self.put(bottom - 1, self.LEFT, chosen_label, self.blue)
        self.scr.refresh()

    def failure_screen(self, items, index, seconds):
        """The explanation, the choices, and the countdown line under the footer while it runs."""
        self.scr.erase()
        width = self.scr.getmaxyx()[1] - self.LEFT - 1
        row = 1
        for paragraph in FAILURE_TEXT:
            for line in textwrap.wrap(paragraph, width):
                self.put(row, self.LEFT, line)
                row += 1
            row += 1
        for n, item in enumerate(items):
            if item is None:
                continue
            if n == index:
                self.bar(row + n, item.label)
            else:
                self.put(row + n, self.ITEM, item.label, curses.A_NORMAL if item.selectable else self.gray)
        bottom = self.bottom()
        self.put(bottom - 2, self.LEFT, "Use the up and down arrow keys to move the highlight to your choice.")
        if seconds is not None:
            self.put(bottom - 1, self.LEFT, f"Seconds until {OS_NAME} starts: {seconds}")
        self.scr.refresh()


ENTER_KEYS = (curses.KEY_ENTER, 10, 13)


def run_menu(stdscr):
    """Returns (action, mode, option): ("boot", mode, option) or ("reboot", None, None)."""
    screen = Screen(stdscr)
    items = menu_items(os.path.exists(SETUP_STAGE), lastgood_saved())
    index = first_selectable(items)
    chosen = None          # the Item chosen in the menu, shown at the bottom of the OS list
    in_menu = True
    flush = True
    while True:
        if in_menu:
            screen.advanced_menu(items, index)
        else:
            screen.os_choices(chosen.label if chosen else None)
        if flush:          # keys pressed before the menu appeared (the F8 presses) choose nothing
            screen.flush_input()
            flush = False
        key = stdscr.getch()
        if in_menu:
            if key == curses.KEY_UP:
                index = move(items, index, -1)
            elif key == curses.KEY_DOWN:
                index = move(items, index, 1)
            elif key in ENTER_KEYS:
                item = items[index]
                if item.action == "start":
                    return ("boot", "normal", None)
                if item.action == "reboot":
                    return ("reboot", None, None)
                chosen = item if item.action == "boot" else None
                in_menu = False
        else:
            if key in ENTER_KEYS:
                if chosen:
                    return ("boot", chosen.mode, chosen.option)
                return ("boot", "normal", None)
            if key == curses.KEY_F8:
                in_menu = True


def run_failure(stdscr, clock=time.monotonic):
    """The failure screen. Returns ("boot", mode, option). Start Normally is highlighted and chosen when the
    countdown ends; an arrow key stops the countdown and removes its line, as on XP. A choice starts at once."""
    screen = Screen(stdscr)
    items = failure_items(os.path.exists(SETUP_STAGE), lastgood_saved())
    index = len(items) - 1
    deadline = clock() + COUNTDOWN
    counting = True
    flush = True
    stdscr.timeout(250)
    while True:
        seconds = max(0, math.ceil(deadline - clock())) if counting else None
        if seconds == 0:
            return ("boot", "normal", None)
        screen.failure_screen(items, index, seconds)
        if flush:
            screen.flush_input()
            flush = False
        key = stdscr.getch()
        if key in (curses.KEY_UP, curses.KEY_DOWN):
            counting = False
            index = move(items, index, -1 if key == curses.KEY_UP else 1)
        elif key in ENTER_KEYS:
            item = items[index]
            if item.action == "start":
                return ("boot", "normal", None)
            return ("boot", item.mode, item.option)


def main():
    try:
        action, mode, option = curses.wrapper(run_failure if start_failed() else run_menu)
    except Exception as error:  # never leave the machine stuck at the menu: start normally
        print(f"wex-advanced-options: {error}; starting normally", file=sys.stderr)
        action, mode, option = "boot", "normal", None
    if action == "reboot":
        reboot()
        return 0
    try:
        apply_choice(mode, option)
    except Exception as error:
        if (mode, option) == ("normal", None):
            raise  # the unit's OnFailure= starts graphical.target
        print(f"wex-advanced-options: {error}; starting normally", file=sys.stderr)
        apply_choice("normal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
