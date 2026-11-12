#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The Advanced Options menu: XP's F8 menu, on tty1 when the boot has wexmode=menu (docs/SAFE_MODE.md), which
Vintner Boot's Advanced Options variant of each generation passes (installer/vintner/advanced.conf).

wex-advanced-options.service runs it before basic.target, so no timer, socket or service of the normal boot has
started yet. A choice writes the mode to /run/wineexplorer (gone at the next boot), reloads systemd, whose
wex-bootmode-generator then reads it, and starts default.target. Nothing on disk changes, except with Last Known Good
Configuration: wex-lastgood puts the machine registry's control set from the last successful start back first.
When this boot's generation has not started successfully yet (`vintner status --json`), Last Known Good also
restarts into the previous good generation (`vintner next-boot previous-good`).
Return to OS Choices Menu restarts into the boot loader's menu (`vintner next-boot menu`); when Vintner cannot set
that, it shows the local list (one entry).

An ordinary start after failed ones shows XP's "did not start successfully" screen: the three Safe Modes, Last Known
Good and Start Normally, which a 30-second countdown chooses. wex-failure-check.service runs `wex-advanced-options
--failure-check`, which starts wex-failure-screen.service (`--failure-screen`) when `vintner status --json` reports
failed starts of this generation, or a start through the menu did not finish. That start is counted, so Start
Normally and Last Known Good go on with it.

As on XP, a start through the menu counts unless it is a Safe Mode (measured on the XP VM for Enable VGA Mode,
2026-11-11: it updated Last Known Good, and a reset during it brought the failure screen; a Safe Mode start did
neither). Vintner Boot counts nothing for its variant, so the menu records such a start on disk (MENU_START), and the
start-up check (wex-startup-check) removes the record once the logon screen is up and saves the control set.
A Safe Mode choice restarts into the Advanced Options variant (`vintner next-boot variant advanced`), which counts
nothing, and the menu there starts the recorded mode without showing itself.

Installed as /usr/local/sbin/wex-advanced-options. The WEX_* variables let the tests use other paths.
"""

import curses
import fcntl
import json
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
VINTNER = os.environ.get("WEX_VINTNER", "/usr/sbin/vintner")
PLYMOUTH = os.environ.get("WEX_PLYMOUTH", "/usr/bin/plymouth")
# The Safe Mode the failure screen chose for the next start, through the variant (failure_screen)
PENDING = os.environ.get("WEX_BOOTMODE_PENDING", "/var/lib/wineexplorer/next-bootmode")
# A start through the menu that counts (any choice but a Safe Mode), until the start-up check sees it succeed
MENU_START = os.environ.get("WEX_MENU_START", "/var/lib/wineexplorer/menu-start")
VARIANT = "advanced"     # installer/vintner/advanced.conf
SAFE_MODES = ("safe", "safenet", "safecmd")
KDSETMODE, KD_TEXT = 0x4B3A, 0x00   # linux/kd.h
OS_NAME = "WineExplorer"
LASTGOOD_LABEL = "Last Known Good Configuration (your most recent settings that worked)"
COUNTDOWN = 30      # seconds, as XP's failure screen, unless boot.conf says otherwise
# Startup and Recovery's "Time to display recovery options when needed" (sysdm.cpl, through wex-boot1): whether a
# failed start shows the failure screen (recovery_menu=; off, the start goes on, as on XP) and its countdown
# (recovery_timeout=). XP keeps them in bootstat.dat (bytes 8 and 9, measured 2026-11-12).
BOOT_CONF = os.environ.get("WEX_BOOT_CONF", "/etc/wineexplorer/boot.conf")
COUNTDOWN_MAX = 999

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
OPTIONS = ("bootlog", "debug", "norestart", "lastgood", "vga")
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
        # XP: the display driver it has, at low resolution; the greeter and the sessions read the option
        # (wex-bootmode --option) and start at 640x480 without the saved display mode (work order D2)
        Item("Enable VGA Mode", "boot", option="vga"),
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


def vintner_booted(path=None):
    """True when Vintner Boot started this system: its vintner.bootstorage= is on the command line and its tool is
    installed."""
    try:
        with open(path or CMDLINE, encoding="ascii", errors="replace") as f:
            tokens = f.read().split()
    except OSError:
        return False
    return any(t.startswith("vintner.bootstorage=") for t in tokens) and os.access(VINTNER, os.X_OK)


def loader_menu(run=subprocess.run):
    """Return to OS Choices Menu with Vintner Boot: its loader shows its own menu (the generations, the Advanced
    Options variant and the Recovery Console) at the next start, for the default 30 seconds, as XP's boot.ini list
    does. True once the request is set; the caller restarts. Vintner Boot's docs/next-boot.md (WO-0010 4.4)."""
    try:
        result = run([VINTNER, "next-boot", "menu"], check=False, capture_output=True, text=True)
    except OSError:
        return False
    return result.returncode == 0


def vintner_status(run=subprocess.run):
    """Vintner Boot's report on the installation and this boot (`vintner status --json`, format 1, its
    docs/status.md, WO-0010 4.5), or None when Vintner Boot did not start this system or gives none."""
    if not vintner_booted():
        return None
    try:
        result = run([VINTNER, "status", "--json"], check=False, capture_output=True, text=True, timeout=30)
        status = json.loads(result.stdout) if result.returncode == 0 else None
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None
    return status if isinstance(status, dict) and status.get("format") == 1 else None


def previous_good(status):
    """The generation that Last Known Good goes back to with Vintner Boot, or None. XP's last known good
    configuration is the one of the last start that succeeded. When this boot's generation (or its variant) is newer
    than the newest GOOD one, it has not started successfully yet, so going back means the previous good generation
    as well as the registry. When it is GOOD, only the registry goes back."""
    booted = (status or {}).get("booted") or {}
    if booted.get("kind") not in ("generation", "variant"):
        return None
    generation, newest, previous = booted.get("generation"), status.get("newest_good"), status.get("previous_good")
    if not all(isinstance(n, int) and not isinstance(n, bool) for n in (generation, newest, previous)):
        return None
    return previous if generation > newest else None


def go_back_to_previous_good(run=subprocess.run):
    """Last Known Good with Vintner Boot: the next start boots the previous good generation (`vintner next-boot
    previous-good`), with the registry's control set from the last start that succeeded, if one was saved. True
    once both are done; the caller restarts. False when Vintner refuses: then this boot continues with the registry
    alone, as from a generation that started successfully."""
    try:
        result = run([VINTNER, "next-boot", "previous-good"], check=False, capture_output=True, text=True)
    except OSError:
        return False
    if result.returncode != 0:
        return False
    if lastgood_saved(run):
        try:
            run([LASTGOOD, "restore"], check=True)
        except (OSError, subprocess.CalledProcessError) as error:   # the older generation still starts
            print(f"wex-advanced-options: the registry's control set was not put back: {error}", file=sys.stderr)
    return True


def failed_starts(status):
    """How many earlier starts of this generation failed, when this is an ordinary Vintner Boot start of it, else 0
    (`booted.failed_attempts`, which does not count this start). Above 0, XP's failure screen is due."""
    booted = (status or {}).get("booted") or {}
    count = booted.get("failed_attempts")
    if booted.get("kind") != "generation" or not isinstance(count, int) or isinstance(count, bool):
        return 0
    return max(count, 0)


def write_pending(mode, generation):
    """Record MODE for the next start, the Advanced Options variant of GENERATION (root only, on disk: /run does not
    survive the restart)."""
    os.makedirs(os.path.dirname(PENDING), mode=0o755, exist_ok=True)
    tmp = PENDING + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as f:
        f.write(f"{mode} {generation}\n")
    os.replace(tmp, PENDING)


def restart_into_variant(generation, run=subprocess.run):
    """Sets the next start to the Advanced Options variant of GENERATION, the generation that showed the failure
    screen (`vintner next-boot variant --generation`). A variant has no try counter, so this works on the
    generation's last try too. An older vintner without --generation (a usage error, exit 2) takes the generation that
    would boot next, which on a last try is an older one or none; a request for another generation's variant is taken
    back (`vintner next-boot clear`). Returns None once the request is set, else why it is not."""
    command = [VINTNER, "next-boot", "--json", "variant", VARIANT]
    known = isinstance(generation, int) and not isinstance(generation, bool)
    result = run(command + (["--generation", str(generation)] if known else []),
                 check=False, capture_output=True, text=True)
    if known and result.returncode == 2:
        result = run(command, check=False, capture_output=True, text=True)
    try:
        reply = json.loads(result.stdout or "{}")
    except ValueError:
        reply = {}
    if not isinstance(reply, dict):
        reply = {}
    if result.returncode != 0 or reply.get("ok") is not True:
        return str(reply.get("error") or (result.stderr or "").strip()
                   or f"vintner next-boot ended with {result.returncode}")
    if known and reply.get("generation") != generation:
        run([VINTNER, "next-boot", "clear"], check=False, capture_output=True, text=True)
        return f"vintner would start the variant of generation {reply.get('generation')}, not of {generation}"
    return None


def remove_pending():
    try:
        os.remove(PENDING)
    except FileNotFoundError:
        pass


def take_pending(status):
    """The Safe Mode that the failure screen chose for this start, or None. It applies only to the Advanced Options
    variant of the generation that showed the screen; the record is removed once read, whatever it says, so it
    never applies twice."""
    try:
        with open(PENDING, encoding="ascii", errors="replace") as f:
            words = f.read().split()
    except OSError:
        return None
    remove_pending()
    booted = (status or {}).get("booted") or {}
    if len(words) != 2 or words[0] not in SAFE_MODES or not words[1].isdigit():
        return None
    if (booted.get("kind"), booted.get("variant"), booted.get("generation")) != ("variant", VARIANT, int(words[1])):
        return None
    return words[0]


PLYMOUTH_TIMEOUT = 2   # seconds for one `plymouth` request


def plymouthd_running(run=subprocess.run):
    try:
        return run(["pgrep", "-x", "plymouthd"], check=False, capture_output=True).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def end_plymouth(run=subprocess.run, sleep=time.sleep, tries=40, grace=6):
    """End Plymouth's splash. plymouthd from the initramfs keeps the screen in graphics mode and reads tty1 until it
    quits (its quit units are masked), so a menu drawn under it is not seen and its keys are split.
    wex-plymouth-quit.service runs this (`--quit-plymouth`) before the menu's and the failure screen's units take
    tty1: systemd's set-up of tty1 for those units reads the console's answer to a query, and while plymouthd also
    read tty1 it took that answer in about 1 start in 16, and the set-up waited, the screen black, until a key was
    pressed (2026-11-10 and -11; a task dump showed the unit's process before exec in n_tty_read). That unit has no
    terminal. The menu and the screen call it again, which then finds nothing to do.
    - Ask until `plymouth --ping` says plymouthd is gone, at most `tries` times a quarter second apart.
    - Each request gets PLYMOUTH_TIMEOUT seconds; one that runs out counts as no answer.
    - Then wait for the process itself, `grace` quarter seconds; end it (TERM) if it is still there, and kill it
      after as long again."""
    for asked in range(tries):
        try:
            if run([PLYMOUTH, "--ping"], check=False, timeout=PLYMOUTH_TIMEOUT).returncode != 0:
                if asked > 1:
                    print(f"wex-advanced-options: plymouthd quit after {asked} requests", file=sys.stderr)
                break
            run([PLYMOUTH, "quit"], check=False, timeout=PLYMOUTH_TIMEOUT)
        except OSError:      # no Plymouth installed
            break
        except subprocess.TimeoutExpired as expired:
            print(f"wex-advanced-options: {' '.join(expired.cmd[1:])} got no answer from plymouthd in "
                  f"{PLYMOUTH_TIMEOUT} seconds", file=sys.stderr)
            break
        sleep(0.25)
    else:
        print("wex-advanced-options: plymouthd did not quit", file=sys.stderr)
    for signal in ("TERM", "KILL"):
        for _ in range(grace):
            if not plymouthd_running(run):
                break
            sleep(0.25)
        else:
            print(f"wex-advanced-options: plymouthd still runs; sending {signal}", file=sys.stderr)
            try:
                run(["pkill", f"-{signal}", "-x", "plymouthd"], check=False)
            except OSError:
                pass
            continue
        break


def quit_plymouth(run=subprocess.run, sleep=time.sleep, console=None, tries=40, grace=6):
    """Before the menu or the failure screen draws: Plymouth ended (end_plymouth), and the console in text mode."""
    end_plymouth(run, sleep, tries, grace)
    try:
        fcntl.ioctl(sys.stdin.fileno() if console is None else console, KDSETMODE, KD_TEXT)
    except (OSError, ValueError, AttributeError):   # not a virtual console (tests, a serial line), or no stdin
        pass


def lastgood_saved(run=subprocess.run):
    """True when wex-lastgood has a control set from a successful start to go back to."""
    try:
        return run([LASTGOOD, "status"], check=False).returncode == 0
    except OSError:
        return False


def first_selectable(items):
    return next(i for i, item in enumerate(items) if item is not None and item.selectable)


def start_normally(items):
    """Where both of XP's lists open: Start Normally."""
    return next(i for i, item in enumerate(items) if item is not None and item.action == "start")


def move(items, index, step):
    """The next selectable line up (step -1) or down (step 1). As NTLDR's lists do, it wraps around at either end
    (Up on the first line goes to the last); it skips blank and grayed lines."""
    i = index
    for _ in range(len(items)):
        i = (i + step) % len(items)
        if items[i] is not None and items[i].selectable:
            return i
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


def write_menu_start(mode, option):
    """Record a start through the menu that counts (MENU_START, on disk), so that the next ordinary start shows the
    failure screen if this one never reaches the logon screen. Root only; a failure only loses that screen."""
    try:
        os.makedirs(os.path.dirname(MENU_START), mode=0o755, exist_ok=True)
        tmp = MENU_START + ".tmp"
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        with os.fdopen(fd, "w", encoding="ascii") as f:
            f.write(f"{mode} {option or '-'}\n")
        os.replace(tmp, MENU_START)
    except OSError as error:
        print(f"wex-advanced-options: this start is not recorded: {error}", file=sys.stderr)


def apply_choice(mode, option=None, run=subprocess.run):
    """Continue this boot in MODE, with one OPTION of the menu. Any start but a Safe Mode counts, as on XP
    (write_menu_start)."""
    if mode not in MODES:
        raise ValueError(f"unknown mode {mode!r}")
    if option is not None and option not in OPTIONS:
        raise ValueError(f"unknown option {option!r}")
    if mode not in SAFE_MODES:
        write_menu_start(mode, option)
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
    """Text mode drawing in the NTLDR style, laid out as XP's 80x25 screens (rows and columns measured on the XP
    reference VM, work order C5): light gray on black, headings and text from column 0, list lines from column 4,
    and the choice as a light gray bar from column 0 to the label's last letter."""

    LEFT = 0        # headings and text
    ITEM = 4        # list lines
    TEXT_WIDTH = 80  # XP's text mode; a wider console keeps XP's line breaks

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
        """The highlight: the indent and the label, nothing after it."""
        self.put(row, 0, " " * self.ITEM + text, curses.A_REVERSE)

    def bottom(self):
        return self.scr.getmaxyx()[0] - 1

    def list_lines(self, row, items, index):
        """ITEMS from ROW down, the one at INDEX highlighted; returns the footer's row, one empty row below."""
        for n, item in enumerate(items):
            if item is None:
                continue
            if n == index:
                self.bar(row + n, item.label)
            else:
                self.put(row + n, self.ITEM, item.label, curses.A_NORMAL if item.selectable else self.gray)
        footer = row + len(items) + 1
        self.put(footer, self.LEFT, "Use the up and down arrow keys to move the highlight to your choice.")
        return footer

    def advanced_menu(self, items, index):
        """Title on row 1, the list from row 4 (the last item on row 17), the footer on row 19."""
        self.scr.erase()
        self.put(1, self.LEFT, "WineExplorer Advanced Options Menu")
        self.put(2, self.LEFT, "Please select an option:")
        self.list_lines(4, items, index)
        self.scr.refresh()

    def os_choices(self, chosen_label):
        """Rows 2, 5, 7 and 8 from the top; the F8 line and the chosen mode on rows 21 and 23 of 25, from the bottom."""
        self.scr.erase()
        self.put(2, self.LEFT, "Please select the operating system to start:")
        self.bar(5, OS_NAME)
        self.put(7, self.LEFT, "Use the up and down arrow keys to move the highlight to your choice.")
        self.put(8, self.LEFT, "Press ENTER to choose.")
        bottom = self.bottom()
        self.put(bottom - 3, self.LEFT,
                 f"For troubleshooting and advanced startup options for {OS_NAME}, press F8.")
        if chosen_label:
            self.put(bottom - 1, self.LEFT, chosen_label, self.blue)
        self.scr.refresh()

    def failure_screen(self, items, index, seconds):
        """The explanation from row 1, broken as on an 80-column screen; the choices after an empty row; the footer
        one empty row below them, and the countdown on the row directly under it while it runs."""
        self.scr.erase()
        width = min(self.scr.getmaxyx()[1], self.TEXT_WIDTH) - self.LEFT - 1
        row = 1
        for paragraph in FAILURE_TEXT:
            for line in textwrap.wrap(paragraph, width):
                self.put(row, self.LEFT, line)
                row += 1
            row += 1
        footer = self.list_lines(row, items, index)
        if seconds is not None:
            self.put(footer + 1, self.LEFT, f"Seconds until {OS_NAME} starts: {seconds}")
        self.scr.refresh()


ENTER_KEYS = (curses.KEY_ENTER, 10, 13)


def run_menu(stdscr, previous=None):
    """Returns (action, mode, option): ("boot", mode, option) or ("reboot", None, None). previous: the generation
    Last Known Good goes back to (previous_good), which offers it even without a saved control set."""
    screen = Screen(stdscr)
    items = menu_items(os.path.exists(SETUP_STAGE), previous is not None or lastgood_saved())
    index = start_normally(items)
    chosen = None         # the Item chosen in the menu, shown at the bottom of the OS list
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
                # The loader's own list, after a restart; if Vintner cannot set it, the local list (one entry)
                if item.action == "choices" and vintner_booted() and loader_menu():
                    return ("reboot", None, None)
                chosen = item if item.action == "boot" else None
                in_menu = False
        else:
            if key in ENTER_KEYS:
                if chosen:
                    return ("boot", chosen.mode, chosen.option)
                return ("boot", "normal", None)
            if key == curses.KEY_F8:   # back to the menu, which opens on Start Normally again, as on XP
                in_menu = True
                index = start_normally(items)


def boot_conf(key, path=None):
    """A value from boot.conf (wex-boot1 writes it), or None."""
    try:
        with open(path or BOOT_CONF, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                name, value = line.split("=", 1)
                if name.strip() == key:
                    return value.strip()
    except OSError:
        pass
    return None


def failure_countdown(path=None):
    """The failure screen's countdown in seconds: recovery_timeout= in boot.conf, COUNTDOWN when the file or the
    value is missing or not a number."""
    try:
        return min(max(int(boot_conf("recovery_timeout", path)), 0), COUNTDOWN_MAX)
    except (TypeError, ValueError):
        return COUNTDOWN


def recovery_enabled(path=None):
    """Whether a failed start shows the failure screen: recovery_menu= in boot.conf, on unless it says 0."""
    return boot_conf("recovery_menu", path) != "0"


def run_failure(stdscr, previous=None, clock=time.monotonic, countdown=None):
    """The failure screen. Returns ("boot", mode, option). Start Normally is highlighted and chosen when the
    countdown ends; an arrow key stops the countdown and removes its line, as on XP. A choice starts at once.
    A countdown of 0 starts normally at once. previous: as for run_menu."""
    screen = Screen(stdscr)
    items = failure_items(os.path.exists(SETUP_STAGE), previous is not None or lastgood_saved())
    index = start_normally(items)
    seconds_left = failure_countdown() if countdown is None else countdown
    if seconds_left <= 0:
        return ("boot", "normal", None)
    deadline = clock() + seconds_left
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


def failure_check(run=subprocess.run):
    """wex-failure-check.service, on an ordinary Vintner Boot start: start the failure screen and wait for it when
    earlier starts of this generation failed, or a start through the menu that counts did not reach the logon screen
    (MENU_START is still there; a successful start removes it). Setup's starts are left alone: Setup runs again at
    the next start anyway. Returns the exit code; a failed check lets the start go on."""
    if os.path.exists(SETUP_STAGE):
        return 0
    count = failed_starts(vintner_status(run))
    if count:
        print(f"wex-advanced-options: {count} earlier start(s) of this generation failed", file=sys.stderr)
    elif os.path.exists(MENU_START):
        print("wex-advanced-options: a start through the Advanced Options menu did not finish", file=sys.stderr)
    else:
        return 0
    # Startup and Recovery's "Time to display recovery options when needed" unchecked (or 0): as on XP, no screen
    if not recovery_enabled() or failure_countdown() == 0:
        print("wex-advanced-options: recovery options are turned off (Startup and Recovery); starting normally",
              file=sys.stderr)
        return 0
    run(["systemctl", "start", "wex-failure-screen.service"], check=False)
    return 0


def finish_failure(status, mode, option, run=subprocess.run):
    """What a choice on the failure screen does on an ordinary Vintner Boot start, which the loader counts:
    - a Safe Mode: recorded for the next start, which is the Advanced Options variant of this generation
      (restart_into_variant); a variant start counts nothing, as a Safe Mode start never does (owner decision);
    - Last Known Good: the previous good generation, as from the menu, when this one never started successfully;
      otherwise the registry's control set goes back and this start goes on, and counts, as on XP;
    - Start Normally: this start goes on.
    Returns True when the machine restarts."""
    if mode in SAFE_MODES:
        generation = ((status or {}).get("booted") or {}).get("generation")
        try:
            write_pending(mode, generation)
            why = restart_into_variant(generation, run)
        except OSError as error:
            why = str(error)
        if why is None:
            reboot(run)
            return True
        remove_pending()
        print(f"wex-advanced-options: cannot restart into {mode} ({why}); starting normally", file=sys.stderr)
        return False
    if option == "lastgood":
        if previous_good(status) is not None and go_back_to_previous_good(run):
            reboot(run)
            return True
        try:
            run([LASTGOOD, "restore"], check=True)
        except (OSError, subprocess.CalledProcessError) as error:
            print(f"wex-advanced-options: the registry's control set was not put back: {error}", file=sys.stderr)
    return False


def failure_screen():
    """wex-failure-screen.service: XP's "did not start successfully" screen on tty1 (failure_check)."""
    status = None
    try:
        quit_plymouth()
        status = vintner_status()
        _, mode, option = curses.wrapper(run_failure, previous_good(status))
    except Exception as error:  # never leave the machine stuck at the screen: this start goes on
        print(f"wex-advanced-options: {error}; starting normally", file=sys.stderr)
        return 0
    finish_failure(status, mode, option)
    return 0


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if args == ["--quit-plymouth"]:   # wex-plymouth-quit.service
        end_plymouth()
        return 0
    if args == ["--failure-check"]:
        return failure_check()
    if args == ["--failure-screen"]:
        return failure_screen()
    previous = None
    try:
        quit_plymouth()
        status = vintner_status()
        previous = previous_good(status)
        pending = take_pending(status)
        if pending:   # chosen on the failure screen before the restart: no menu
            print(f"wex-advanced-options: {pending}, chosen on the failure screen", file=sys.stderr)
            action, mode, option = "boot", pending, None
        else:
            action, mode, option = curses.wrapper(run_menu, previous)
    except Exception as error:  # never leave the machine stuck at the menu: start normally
        print(f"wex-advanced-options: {error}; starting normally", file=sys.stderr)
        action, mode, option = "boot", "normal", None
    if action == "reboot":
        reboot()
        return 0
    # Last Known Good when this generation has not started successfully yet: restart into the previous good one
    if option == "lastgood" and previous is not None and go_back_to_previous_good():
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
