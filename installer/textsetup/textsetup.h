#ifndef TEXTSETUP_H
#define TEXTSETUP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <curses.h>

#define VGA_COLS 80
#define VGA_ROWS 25

/* Color Pairs */
#define PAIR_NORMAL     1   /* White on Blue */
#define PAIR_HEADER     2   /* White on Blue (Reverse/Bold) */
#define PAIR_STATUS     3   /* Black on Gray/White */
#define PAIR_HIGHLIGHT  4   /* Yellow/Bright on Blue */
#define PAIR_PROGRESS   5   /* Yellow on Blue */
#define PAIR_REBOOT     6   /* White on Red */

/* Screen Steps */
typedef enum {
    STAGE_WELCOME = 0,
    STAGE_LICENSE,
    STAGE_PARTITION,
    STAGE_FORMAT,
    STAGE_COPY,
    STAGE_INIT,
    STAGE_REBOOT,
    STAGE_QUIT
} SetupStage;

typedef struct {
    char device[64];       /* e.g. /dev/sda */
    char model[128];      /* disk model */
    uint64_t size_mb;     /* total size in MB */
} DiskDevice;

typedef struct {
    char device[64];       /* e.g. /dev/sda1 */
    char fstype[32];       /* e.g. ext4, vfat, Unformatted */
    char label[64];
    uint64_t size_mb;
    bool is_free_space;
    int disk_index;
    int part_num;
} PartitionEntry;

typedef struct {
    char target_disk[64];
    char target_part[64];
    char efi_part[64];
    bool is_uefi;
    bool quick_format;
    bool unattended;
    char unattend_path[256];
} SetupState;

/* Screen helper routines */
void ui_init(void);
void ui_shutdown(void);
void ui_clear_screen(void);
void ui_draw_header(const char *title);
void ui_draw_status(const char *status_text);
void ui_draw_progressbar(int y, int start_x, int width, int percentage, int color_pair);
int ui_getch(void);

/* Stage Screens */
SetupStage screen_welcome(SetupState *state);
SetupStage screen_license(SetupState *state);
SetupStage screen_partition(SetupState *state);
SetupStage screen_format(SetupState *state);
SetupStage screen_copy(SetupState *state);
SetupStage screen_init(SetupState *state);
SetupStage screen_reboot(SetupState *state);

#endif /* TEXTSETUP_H */
