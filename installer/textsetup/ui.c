/*
 * WineExplorer Desktop Environment - ui.c
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "textsetup.h"
#include <termios.h>

void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(50);

    /* Enforce non-canonical, no-echo mode on stdin */
    struct termios tio;
    if (tcgetattr(STDIN_FILENO, &tio) == 0) {
        tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL);
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    }

    if (has_colors()) {
        start_color();
        /* XP text-mode colors:
         * Blue background (#0000AA) with white text.
         * Status bar: black text on white/cyan background.
         * Progress bar: yellow (#FFFF55) on blue or solid yellow blocks.
         */
        init_pair(PAIR_NORMAL, COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(PAIR_STATUS, COLOR_BLACK, COLOR_WHITE);
        init_pair(PAIR_HIGHLIGHT, COLOR_YELLOW, COLOR_BLUE);
        init_pair(PAIR_PROGRESS, COLOR_YELLOW, COLOR_BLUE);
        init_pair(PAIR_REBOOT, COLOR_WHITE, COLOR_RED);
    }

    bkgd(COLOR_PAIR(PAIR_NORMAL));
    refresh();
}

void ui_shutdown(void)
{
    curs_set(1);
    endwin();
}

void ui_clear_screen(void)
{
    bkgd(COLOR_PAIR(PAIR_NORMAL));
    erase();
    clear();
    flushinp();
}

void ui_draw_header(const char *title)
{
    int cols = (COLS > 0) ? COLS : VGA_COLS;
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(1, 4, "%s", title ? title : "Debian GNU/Linux with WineExplorer Setup");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Underline across entire display width */
    attron(COLOR_PAIR(PAIR_HEADER));
    mvhline(2, 0, ACS_HLINE, cols);
    attroff(COLOR_PAIR(PAIR_HEADER));
}

void ui_draw_status(const char *status_text)
{
    int row = (LINES > 0) ? (LINES - 1) : (VGA_ROWS - 1);
    int cols = (COLS > 0) ? COLS : VGA_COLS;
    attron(COLOR_PAIR(PAIR_STATUS));
    mvhline(row, 0, ' ', cols);
    if (status_text) {
        mvprintw(row, 1, "%s", status_text);
    }
    attroff(COLOR_PAIR(PAIR_STATUS));
    refresh();
}

void ui_draw_progressbar(int y, int start_x, int width, int percentage, int color_pair)
{
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    int filled_width = (width * percentage) / 100;

    /* Frame */
    attron(COLOR_PAIR(PAIR_NORMAL));
    mvhline(y, start_x, ACS_HLINE, width);
    attroff(COLOR_PAIR(PAIR_NORMAL));

    /* Solid bar */
    attron(COLOR_PAIR(color_pair) | A_REVERSE);
    for (int i = 0; i < filled_width; ++i) {
        mvaddch(y, start_x + i, ' ');
    }
    attroff(COLOR_PAIR(color_pair) | A_REVERSE);

    /* Percentage text centered above or on the bar (omitted for countdown reboot bar) */
    if (color_pair != PAIR_REBOOT) {
        attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        mvprintw(y - 1, start_x + (width / 2) - 3, "%3d%%", percentage);
        attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    }

    refresh();
}

void ui_get_centered_bar_bounds(int *out_y, int *out_x, int *out_width)
{
    int cols = (COLS > 0) ? COLS : VGA_COLS;
    int lines = (LINES > 0) ? LINES : VGA_ROWS;
    int width = (cols > 80) ? (cols * 3 / 4) : (cols - 16);
    if (width > 90) width = 90;
    if (width < 40) width = 40;
    if (out_width) *out_width = width;
    if (out_x) *out_x = (cols - width) / 2;
    if (out_y) *out_y = lines / 2;
}

void ui_draw_centered_progressbar(int percentage, int color_pair)
{
    int y, x, width;
    ui_get_centered_bar_bounds(&y, &x, &width);
    ui_draw_progressbar(y, x, width, percentage, color_pair);
}

int ui_getch(void)
{
    struct termios tio;
    if (tcgetattr(STDIN_FILENO, &tio) == 0) {
        if ((tio.c_lflag & (ICANON | ECHO)) != 0) {
            tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL);
            tio.c_cc[VMIN] = 1;
            tio.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &tio);
        }
    }
    return getch();
}
