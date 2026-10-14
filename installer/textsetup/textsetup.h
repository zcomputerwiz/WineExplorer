/*
 * WineExplorer Desktop Environment - textsetup.h
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef TEXTSETUP_H
#define TEXTSETUP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <curses.h>
#include <libfdisk/libfdisk.h>
#include <blkid/blkid.h>

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
    char name[32];         /* e.g. sda */
    char model[128];       /* disk model e.g. "VBOX HARDDISK" */
    char bus[32];          /* e.g. "atapi", "scsi", "nvme" */
    uint64_t size_mb;      /* total size in MB */
    uint64_t sector_size;
    uint64_t nsectors;
    bool is_gpt;
    struct fdisk_context *cxt;
} DiskDevice;

typedef struct {
    char device[64];       /* e.g. /dev/sda1 or empty for free space */
    char fstype[32];       /* e.g. ext4, vfat, ntfs, Unformatted */
    char label[64];
    uint64_t size_mb;
    uint64_t start_sector;
    uint64_t num_sectors;
    size_t partno;
    bool is_free_space;
    bool is_esp;
    int disk_index;
} PartitionEntry;

typedef struct {
    char target_disk[64];
    char target_part[64];
    char efi_part[64];
    char target_fstype[32];
    bool is_uefi;
    int format_choice;     /* 0=Quick ext4, 1=Full ext4, 2=Leave intact */
    bool unattended;
    char unattend_path[256];
    bool repartition;
    int target_disk_idx;
    size_t target_partno;
    uint64_t target_size_mb;
    uint64_t target_disk_size_mb;
    char target_disk_bus[32];
    bool target_disk_is_gpt;
} SetupState;

/* Screen helper routines */
void ui_init(void);
void ui_shutdown(void);
void ui_clear_screen(void);
void ui_draw_header(const char *title);
void ui_draw_status(const char *status_text);
void ui_draw_progressbar(int y, int start_x, int width, int percentage, int color_pair);
void ui_draw_centered_progressbar(int percentage, int color_pair);
void ui_get_centered_bar_bounds(int *out_y, int *out_x, int *out_width);
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
