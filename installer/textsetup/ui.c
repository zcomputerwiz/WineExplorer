#include "textsetup.h"

void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(200);

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
    clear();
}

void ui_draw_header(const char *title)
{
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(1, 4, "%s", title ? title : "Windows XP Professional Setup");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Underline on row 2 */
    attron(COLOR_PAIR(PAIR_HEADER));
    mvhline(2, 0, ACS_HLINE, VGA_COLS);
    attroff(COLOR_PAIR(PAIR_HEADER));
}

void ui_draw_status(const char *status_text)
{
    attron(COLOR_PAIR(PAIR_STATUS));
    mvhline(VGA_ROWS - 1, 0, ' ', VGA_COLS);
    if (status_text) {
        mvprintw(VGA_ROWS - 1, 1, "%s", status_text);
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

    /* Percentage text centered or on the right */
    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    mvprintw(y - 1, start_x + (width / 2) - 3, "%3d%%", percentage);
    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);

    refresh();
}

int ui_getch(void)
{
    int ch = getch();
    if (ch == 27) {
        /* Check if part of an escape sequence */
        halfdelay(2);
        int ch2 = getch();
        cbreak();
        if (ch2 == ERR) {
            return 27; /* standalone ESC */
        }
        if (ch2 == '[' || ch2 == 'O') {
            halfdelay(2);
            int ch3 = getch();
            cbreak();
            if (ch3 == '1' || ch3 == '2') {
                halfdelay(2);
                int ch4 = getch();
                cbreak();
                if (ch3 == '1' && ch4 == '9') {
                    halfdelay(2);
                    getch(); /* '~' */
                    cbreak();
                    return KEY_F(8);
                }
                if (ch3 == '1' && ch4 == '3') {
                    halfdelay(2);
                    getch(); /* '~' */
                    cbreak();
                    return KEY_F(3);
                }
                if (ch3 == '1' && ch4 == '7') {
                    halfdelay(2);
                    getch(); /* '~' */
                    cbreak();
                    return KEY_F(6);
                }
                if (ch3 == '1' && ch4 == '8') {
                    halfdelay(2);
                    getch(); /* '~' */
                    cbreak();
                    return KEY_F(7);
                }
            } else if (ch3 == '3') {
                halfdelay(2);
                getch();
                cbreak();
                return KEY_F(3);
            } else if (ch3 == '5') {
                halfdelay(2);
                getch();
                cbreak();
                return KEY_PPAGE;
            } else if (ch3 == '6') {
                halfdelay(2);
                getch();
                cbreak();
                return KEY_NPAGE;
            } else if (ch3 == 'A') {
                return KEY_UP;
            } else if (ch3 == 'B') {
                return KEY_DOWN;
            } else if (ch3 == 'C') {
                return KEY_RIGHT;
            } else if (ch3 == 'D') {
                return KEY_LEFT;
            } else if (ch2 == '[' && ch3 == '[') {
                halfdelay(2);
                int ch4 = getch();
                cbreak();
                if (ch4 == 'C') return KEY_F(3);
                if (ch4 == 'E') return KEY_F(8);
            } else if (ch2 == 'O') {
                if (ch3 == 'R') return KEY_F(3);
            }
        }
    }
    return ch;
}
