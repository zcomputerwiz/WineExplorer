/*
 * WineExplorer Desktop Environment - screens.c
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

#include "textsetup.h"
#include "copydirs.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sendfile.h>
#include <sys/xattr.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#define MAX_DISKS 8
#define MAX_PARTS 32

/* GPT partition types. Every disk Setup prepares is GPT with the system partitions Vintner Boot needs, on BIOS and
 * UEFI machines alike (docs/VINTNER_BOOT.md section 4.1): BIOS boot, EFI system and boot storage (XBOOTLDR). */
#define GUID_BIOS_BOOT    "21686148-6449-6E6F-744E-656564454649"
#define GUID_ESP          "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
#define GUID_BOOT_STORAGE "BC13C2FF-59E6-4262-A352-B275FD6F7172"
#define GUID_LINUX        "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

/* Setup's start-up step with Vintner Boot; build-iso.sh --vintner puts it and the packages on the ISO. */
#define VINTNER_STEP "/setup/install-vintner.sh"

static void install_grub(SetupState *state);
static bool install_vintner(SetupState *state);
static SetupStage screen_start_files_failed(SetupState *state);

#define ROOT_MIN_MB 8      /* smallest new partition, as on the Create partition screen */

/* GPT attribute bit 63 (no-auto): systemd-gpt-auto-generator never mounts the partition. Boot storage carries it,
 * because Vintner Boot mounts boot storage itself, by PARTUUID, only when it needs it. */
#define GPT_ATTR_NO_AUTO (1ULL << 63)

static const struct {
    SystemPartKind kind;
    const char *guid;
    const char *gpt_name;  /* GPT partition name */
    const char *display;   /* name in the partition list */
    uint64_t size_mb;
    uint64_t gpt_attrs;    /* GPT attribute bits set when Setup creates the partition */
} system_parts[] = {
    { SYS_BIOS_BOOT,    GUID_BIOS_BOOT,    "BIOS boot partition",  "BIOS boot",    2,    0 },
    { SYS_ESP,          GUID_ESP,          "EFI system partition", "EFI System",   512,  0 },
    { SYS_BOOT_STORAGE, GUID_BOOT_STORAGE, "Boot storage",         "Boot storage", 2048, GPT_ATTR_NO_AUTO },
};
#define NUM_SYSTEM_PARTS (sizeof(system_parts) / sizeof(system_parts[0]))

static void safe_mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void find_live_medium_dev(char *out_dev, size_t out_len)
{
    out_dev[0] = '\0';
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char dev[128], mnt[128];
        if (sscanf(line, "%127s %127s", dev, mnt) == 2) {
            if (strcmp(mnt, "/run/live/medium") == 0 || strcmp(mnt, "/lib/live/mount/medium") == 0) {
                char resolved[PATH_MAX];
                if (realpath(dev, resolved)) {
                    char *b = basename(resolved);
                    /* Strip partition numbers to get disk device name */
                    size_t blen = strlen(b);
                    while (blen > 0 && isdigit((unsigned char)b[blen - 1])) {
                        b[blen - 1] = '\0';
                        blen--;
                    }
                    if (blen > 0 && b[blen - 1] == 'p') {
                        b[blen - 1] = '\0';
                    }
                    snprintf(out_dev, out_len, "%s", b);
                }
                break;
            }
        }
    }
    fclose(f);
}

static int scan_disks(DiskDevice *disks, int max_disks)
{
    char live_dev[64];
    find_live_medium_dev(live_dev, sizeof(live_dev));

    DIR *d = opendir("/sys/block");
    if (!d) return 0;

    struct dirent *de;
    int count = 0;

    while ((de = readdir(d)) != NULL && count < max_disks) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;
        if (strncmp(name, "loop", 4) == 0 ||
            strncmp(name, "ram", 3) == 0 ||
            strncmp(name, "zram", 4) == 0 ||
            strncmp(name, "sr", 2) == 0 ||
            strncmp(name, "dm-", 3) == 0) {
            continue;
        }

        if (live_dev[0] && strcmp(name, live_dev) == 0) {
            continue;
        }

        char sizepath[128];
        snprintf(sizepath, sizeof(sizepath), "/sys/block/%s/size", name);
        FILE *sf = fopen(sizepath, "r");
        if (!sf) continue;
        uint64_t sectors = 0;
        if (fscanf(sf, "%lu", &sectors) != 1 || sectors == 0) {
            fclose(sf);
            continue;
        }
        fclose(sf);

        DiskDevice *disk = &disks[count];
        memset(disk, 0, sizeof(*disk));
        snprintf(disk->name, sizeof(disk->name), "%s", name);
        snprintf(disk->device, sizeof(disk->device), "/dev/%s", name);
        disk->nsectors = sectors;
        disk->sector_size = 512;
        disk->size_mb = (sectors * 512ULL) / (1024ULL * 1024ULL);

        /* Read model */
        char modelpath[128];
        snprintf(modelpath, sizeof(modelpath), "/sys/block/%s/device/model", name);
        FILE *mf = fopen(modelpath, "r");
        if (mf) {
            if (fgets(disk->model, sizeof(disk->model), mf)) {
                disk->model[strcspn(disk->model, "\r\n")] = '\0';
                /* Trim trailing spaces */
                size_t mlen = strlen(disk->model);
                while (mlen > 0 && isspace((unsigned char)disk->model[mlen - 1])) {
                    disk->model[--mlen] = '\0';
                }
            }
            fclose(mf);
        }
        if (!disk->model[0]) {
            snprintf(disk->model, sizeof(disk->model), "Harddisk");
        }

        /* Bus */
        if (strstr(name, "nvme")) {
            snprintf(disk->bus, sizeof(disk->bus), "nvme");
        } else if (strstr(name, "vd")) {
            snprintf(disk->bus, sizeof(disk->bus), "virtio");
        } else if (strstr(name, "md")) {
            snprintf(disk->bus, sizeof(disk->bus), "raid");
        } else {
            snprintf(disk->bus, sizeof(disk->bus), "scsi");
        }

        /* Open libfdisk context */
        disk->cxt = fdisk_new_context();
        if (disk->cxt) {
            if (fdisk_assign_device(disk->cxt, disk->device, 0) == 0) {
                disk->sector_size = fdisk_get_sector_size(disk->cxt);
                disk->nsectors = fdisk_get_nsectors(disk->cxt);
                disk->size_mb = (disk->nsectors * disk->sector_size) / (1024ULL * 1024ULL);
                disk->is_gpt = fdisk_is_label(disk->cxt, GPT);
            }
        }

        count++;
    }
    closedir(d);
    return count;
}

static void format_partition_device(char *out, size_t outlen, const char *disk_dev, size_t partno)
{
    if (disk_dev && disk_dev[0]) {
        char last = disk_dev[strlen(disk_dev) - 1];
        if (isdigit((unsigned char)last)) {
            snprintf(out, outlen, "%sp%zu", disk_dev, partno);
            return;
        }
    }
    snprintf(out, outlen, "%s%zu", disk_dev, partno);
}

/* True when the kernel has this partition at this place, that is it is on disk and not only in Setup's unwritten
 * table. Otherwise the device node can still show the file system of a deleted partition that started there. */
static bool partition_on_disk(const char *dev, uint64_t start, uint64_t nsec, uint64_t sector_size)
{
    const char *name = strrchr(dev, '/');
    name = name ? name + 1 : dev;
    unsigned long long k_start = 0, k_size = 0;
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/block/%s/start", name);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    int ok = fscanf(f, "%llu", &k_start);
    fclose(f);
    snprintf(path, sizeof(path), "/sys/class/block/%s/size", name);
    f = fopen(path, "r");
    if (!f) return false;
    ok += fscanf(f, "%llu", &k_size);
    fclose(f);
    uint64_t per = sector_size / 512;   /* sysfs counts 512-byte sectors */
    return ok == 2 && k_start == start * per && k_size == nsec * per;
}

static int scan_partitions(DiskDevice *disk, int disk_idx, PartitionEntry *entries, int max_entries)
{
    if (!disk || !disk->cxt) return 0;

    int count = 0;
    struct fdisk_table *tb = NULL;

    /* A blank disk has no partition table, so libfdisk reports no free space and the disk would not be listed.
     * Give it a GPT one in memory; it is only written when a partition is created. An MBR disk whose partitions
     * were all deleted is prepared the same way, so deleting every partition is how a user frees an MBR disk. */
    if (!fdisk_has_label(disk->cxt)) {
        if (fdisk_create_disklabel(disk->cxt, "gpt") == 0) disk->is_gpt = true;
    } else if (!fdisk_is_label(disk->cxt, GPT)) {
        struct fdisk_table *used = NULL;
        size_t nused = 0;
        if (fdisk_get_partitions(disk->cxt, &used) == 0 && used) {
            nused = fdisk_table_get_nents(used);
            fdisk_unref_table(used);
        }
        if (nused == 0 && fdisk_create_disklabel(disk->cxt, "gpt") == 0) disk->is_gpt = true;
    }
    fdisk_get_partitions(disk->cxt, &tb);

    if (tb) {
        size_t n = fdisk_table_get_nents(tb);
        for (size_t i = 0; i < n && count < max_entries; ++i) {
            struct fdisk_partition *pa = fdisk_table_get_partition(tb, i);
            if (!pa) continue;

            PartitionEntry *pe = &entries[count++];
            memset(pe, 0, sizeof(*pe));
            pe->disk_index = disk_idx;
            pe->partno = fdisk_partition_get_partno(pa) + 1;
            pe->start_sector = fdisk_partition_get_start(pa);
            pe->num_sectors = fdisk_partition_get_size(pa);
            pe->size_mb = (pe->num_sectors * disk->sector_size) / (1024ULL * 1024ULL);
            pe->is_free_space = false;

            format_partition_device(pe->device, sizeof(pe->device), disk->device, pe->partno);

            /* Probe filesystem: only a partition that is on disk has one; a new one shows New (Raw) */
            blkid_probe pr = partition_on_disk(pe->device, pe->start_sector, pe->num_sectors, disk->sector_size)
                                 ? blkid_new_probe_from_filename(pe->device) : NULL;
            if (pr) {
                blkid_do_probe(pr);
                const char *val = NULL;
                if (blkid_probe_lookup_value(pr, "TYPE", &val, NULL) == 0 && val) {
                    snprintf(pe->fstype, sizeof(pe->fstype), "%s", val);
                } else {
                    snprintf(pe->fstype, sizeof(pe->fstype), "Unformatted");
                }
                if (blkid_probe_lookup_value(pr, "LABEL", &val, NULL) == 0 && val) {
                    snprintf(pe->label, sizeof(pe->label), "%s", val);
                }
                blkid_free_probe(pr);
            } else {
                snprintf(pe->fstype, sizeof(pe->fstype), "Unformatted");
            }

            /* System partitions are known by their partition type, never by a label or position. */
            struct fdisk_parttype *pt = fdisk_partition_get_type(pa);
            if (pt && disk->is_gpt) {
                const char *guid = fdisk_parttype_get_string(pt);
                for (size_t k = 0; guid && k < NUM_SYSTEM_PARTS; ++k) {
                    if (strcasecmp(guid, system_parts[k].guid) == 0) pe->sys_kind = system_parts[k].kind;
                }
            } else if (pt && fdisk_parttype_get_code(pt) == 0xEF) {
                pe->sys_kind = SYS_ESP;
            }
            pe->is_esp = (pe->sys_kind == SYS_ESP);
        }
        fdisk_unref_table(tb);
    }

    /* Freespaces */
    struct fdisk_table *fsp = NULL;
    fdisk_get_freespaces(disk->cxt, &fsp);
    if (fsp) {
        size_t n = fdisk_table_get_nents(fsp);
        for (size_t i = 0; i < n && count < max_entries; ++i) {
            struct fdisk_partition *pa = fdisk_table_get_partition(fsp, i);
            if (!pa) continue;

            uint64_t nsec = fdisk_partition_get_size(pa);
            uint64_t mb = (nsec * disk->sector_size) / (1024ULL * 1024ULL);
            if (mb < 1) continue; /* ignore alignment padding */

            PartitionEntry *pe = &entries[count++];
            memset(pe, 0, sizeof(*pe));
            pe->disk_index = disk_idx;
            pe->start_sector = fdisk_partition_get_start(pa);
            pe->num_sectors = nsec;
            pe->size_mb = mb;
            pe->is_free_space = true;
            snprintf(pe->fstype, sizeof(pe->fstype), "Unpartitioned");
        }
        fdisk_unref_table(fsp);
    }

    /* Sort entries by start sector */
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (entries[i].start_sector > entries[j].start_sector) {
                PartitionEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    return count;
}

SetupStage screen_welcome(SetupState *state)
{
    if (state->unattended) {
        return STAGE_LICENSE;
    }

    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 4;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "Welcome to Setup.");
    mvprintw(base_y + 2, margin_x, "This portion of the Setup program prepares Debian GNU/Linux with");
    mvprintw(base_y + 3, margin_x, "WineExplorer to run on your computer.");

    mvprintw(base_y + 6, margin_x + 2, "- To set up Debian GNU/Linux with WineExplorer now, press ENTER.");
    mvprintw(base_y + 8, margin_x + 2, "- To quit Setup without installing Debian GNU/Linux with WineExplorer,");
    mvprintw(base_y + 9, margin_x + 4, "press F3.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("ENTER=Continue  F3=Quit");

    while (1) {
        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
            return STAGE_LICENSE;
        } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
            return STAGE_QUIT;
        }
    }
}

SetupStage screen_license(SetupState *state)
{
    if (state->unattended) {
        return STAGE_PARTITION;
    }

    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 4 + 3 : 3;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL) | A_BOLD);
    mvprintw(base_y, margin_x, "Licensing Agreement");
    attroff(COLOR_PAIR(PAIR_NORMAL) | A_BOLD);

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y + 2, margin_x, "Please read the following license agreement. Press PAGE DOWN to view");
    mvprintw(base_y + 3, margin_x, "the rest of the agreement.");

    /* Read license lines */
    FILE *f = fopen("/run/live/medium/setup/eula.txt", "r");
    if (!f) f = fopen("/setup/eula.txt", "r");
    if (!f) f = fopen("setupinfs/eula.txt", "r");

    char lines[400][80];
    int line_count = 0;
    int width = COLS - 2 * margin_x;
    if (width > 76) width = 76;
    if (width < 20) width = 20;
    if (f) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), f) && line_count < 400) {
            char *p = buf;
            buf[strcspn(buf, "\r\n")] = '\0';
            /* word-wrap to the text column */
            do {
                int len = strlen(p), cut = len;
                if (len > width) {
                    for (cut = width; cut > 0 && p[cut] != ' '; --cut) ;
                    if (!cut) cut = width;
                }
                snprintf(lines[line_count++], 80, "%.*s", cut, p);
                p += cut;
                while (*p == ' ') ++p;
            } while (*p && line_count < 400);
        }
        fclose(f);
    } else {
        snprintf(lines[line_count++], 80, "Debian GNU/Linux with WineExplorer");
        snprintf(lines[line_count++], 80, "Licensed under GNU General Public License (GPL) and Wine LGPL.");
        snprintf(lines[line_count++], 80, "This software comes with ABSOLUTELY NO WARRANTY.");
    }

    int top_line = 0;
    int display_lines = (LINES > 25) ? (LINES - base_y - 8) : 13;
    if (display_lines < 6) display_lines = 6;

    while (1) {
        for (int i = 0; i < display_lines; ++i) {
            int idx = top_line + i;
            move(base_y + 5 + i, margin_x);
            clrtoeol();
            if (idx < line_count) {
                mvprintw(base_y + 5 + i, margin_x, "%s", lines[idx]);
            }
        }

        ui_draw_status("F8=I Agree  ESC=I Do Not Agree  PAGE DOWN=Next Page");

        int ch = ui_getch();
        if (ch == KEY_F(8) || ch == 'y' || ch == 'Y' || ch == 10 || ch == 13 || ch == KEY_ENTER) {
            return STAGE_PARTITION;
        } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q' || ch == 27) {
            return STAGE_QUIT;
        } else if (ch == KEY_NPAGE || ch == ' ' || ch == KEY_DOWN) {
            if (top_line + display_lines < line_count) {
                top_line += display_lines;
            }
        } else if (ch == KEY_PPAGE || ch == KEY_UP) {
            top_line -= display_lines;
            if (top_line < 0) top_line = 0;
        }
    }
}

/* Adds a partition in memory. gpt_type and gpt_name may be NULL: a Linux file system partition with no name. */
static int add_partition_to_disk(DiskDevice *disk, uint64_t start_sec, uint64_t num_sec,
                                 const char *gpt_type, const char *gpt_name, size_t *out_pno)
{
    if (!disk || !disk->cxt) return -1;

    struct fdisk_partition *npa = fdisk_new_partition();
    fdisk_partition_set_start(npa, start_sec);
    fdisk_partition_set_size(npa, num_sec);
    fdisk_partition_size_explicit(npa, 1);
    if (disk->is_gpt && gpt_name) fdisk_partition_set_name(npa, gpt_name);

    /* Find first free partition slot */
    size_t slot = 0;
    size_t max_slots = disk->is_gpt ? 128 : 4;
    for (size_t s = 0; s < max_slots; ++s) {
        struct fdisk_partition *chk = NULL;
        if (fdisk_get_partition(disk->cxt, s, &chk) != 0) {
            slot = s;
            break;
        }
        if (chk) fdisk_unref_partition(chk);
    }
    fdisk_partition_set_partno(npa, slot);

    struct fdisk_parttype *t = fdisk_new_parttype();
    if (disk->is_gpt) {
        fdisk_parttype_set_typestr(t, gpt_type ? gpt_type : GUID_LINUX);
    } else {
        fdisk_parttype_set_code(t, 0x83);
        fdisk_parttype_set_typestr(t, "83");
    }
    fdisk_partition_set_type(npa, t);

    size_t res_pno = slot;
    int rc = fdisk_add_partition(disk->cxt, npa, &res_pno);
    fdisk_unref_partition(npa);
    if (rc == 0) {
        if (out_pno) *out_pno = res_pno;
        return 0;
    }
    return rc;
}

static uint64_t mb_to_sectors(const DiskDevice *disk, uint64_t mb)
{
    return mb * 1024ULL * 1024ULL / disk->sector_size;
}

/* Partitions start on a 1 MB boundary, as fdisk and Windows place them. */
static uint64_t align_mb(const DiskDevice *disk, uint64_t sector)
{
    uint64_t a = mb_to_sectors(disk, 1);
    return a ? (sector + a - 1) / a * a : sector;
}

/* Whole megabytes usable in a free region once its start is aligned. */
static uint64_t region_usable_mb(const DiskDevice *disk, const PartitionEntry *region)
{
    uint64_t start = align_mb(disk, region->start_sector);
    uint64_t end = region->start_sector + region->num_sectors;
    if (end <= start) return 0;
    return (end - start) * disk->sector_size / (1024ULL * 1024ULL);
}

static bool disk_has_system_part(const PartitionEntry *entries, int n, SystemPartKind kind)
{
    for (int i = 0; i < n; ++i) {
        if (!entries[i].is_free_space && entries[i].sys_kind == kind) return true;
    }
    return false;
}

/* Size of the system partitions this disk still lacks. */
static uint64_t missing_system_mb(const PartitionEntry *entries, int n)
{
    uint64_t mb = 0;
    for (size_t k = 0; k < NUM_SYSTEM_PARTS; ++k) {
        if (!disk_has_system_part(entries, n, system_parts[k].kind)) mb += system_parts[k].size_mb;
    }
    return mb;
}

/* Creates, in memory, every system partition the disk lacks, one after another from *start, and moves *start past
 * them. The caller has checked that they fit before end (exclusive). */
static int create_missing_system_partitions(DiskDevice *disk, const PartitionEntry *entries, int n,
                                            uint64_t *start, uint64_t end)
{
    for (size_t k = 0; k < NUM_SYSTEM_PARTS; ++k) {
        if (disk_has_system_part(entries, n, system_parts[k].kind)) continue;
        uint64_t s = align_mb(disk, *start);
        uint64_t len = mb_to_sectors(disk, system_parts[k].size_mb);
        if (s + len > end) return -1;
        size_t pno = 0;
        if (add_partition_to_disk(disk, s, len, system_parts[k].guid, system_parts[k].gpt_name, &pno) != 0) return -1;
        if (system_parts[k].gpt_attrs && fdisk_gpt_set_partition_attrs(disk->cxt, pno, system_parts[k].gpt_attrs) != 0)
            return -1;
        *start = s + len;
    }
    return 0;
}

static const PartitionEntry *largest_free_region(const PartitionEntry *entries, int n)
{
    const PartitionEntry *best = NULL;
    for (int i = 0; i < n; ++i) {
        if (entries[i].is_free_space && (!best || entries[i].num_sectors > best->num_sectors)) best = &entries[i];
    }
    return best;
}

static const char *system_part_display(SystemPartKind kind)
{
    for (size_t k = 0; k < NUM_SYSTEM_PARTS; ++k) {
        if (system_parts[k].kind == kind) return system_parts[k].display;
    }
    return "System";
}

/* Drive letters as XP shows them: C: for the first partition on the disk that is not a system partition. */
static char partition_letter(const PartitionEntry *entries, int n, int idx)
{
    if (entries[idx].is_free_space || entries[idx].sys_kind != SYS_NONE) return 0;
    char letter = 'C';
    for (int i = 0; i < idx && i < n; ++i) {
        if (!entries[i].is_free_space && entries[i].sys_kind == SYS_NONE) letter++;
    }
    return letter > 'Z' ? 'Z' : letter;
}

/* Records the target partition's number, size and letter, and the disk's system partitions, from a fresh scan. */
static void record_target_layout(SetupState *state, const PartitionEntry *entries, int n)
{
    state->efi_part[0] = '\0';
    state->boot_storage_part[0] = '\0';
    state->bios_boot_part[0] = '\0';
    state->target_letter = 'C';
    for (int i = 0; i < n; ++i) {
        const PartitionEntry *pe = &entries[i];
        if (pe->is_free_space) continue;
        if (pe->sys_kind == SYS_ESP && !state->efi_part[0])
            snprintf(state->efi_part, sizeof(state->efi_part), "%s", pe->device);
        else if (pe->sys_kind == SYS_BOOT_STORAGE && !state->boot_storage_part[0])
            snprintf(state->boot_storage_part, sizeof(state->boot_storage_part), "%s", pe->device);
        else if (pe->sys_kind == SYS_BIOS_BOOT && !state->bios_boot_part[0])
            snprintf(state->bios_boot_part, sizeof(state->bios_boot_part), "%s", pe->device);
        else if (strcmp(pe->device, state->target_part) == 0) {
            state->target_partno = pe->partno;
            state->target_size_mb = pe->size_mb;
            char letter = partition_letter(entries, n, i);
            if (letter) state->target_letter = letter;
        }
    }
}

/* A message screen with a list of lines; ENTER or ESC returns. */
static void screen_setup_message(const char *const *lines, int nlines)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 5;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;
    attron(COLOR_PAIR(PAIR_NORMAL));
    for (int i = 0; i < nlines; ++i) mvprintw(base_y + i, margin_x, "%s", lines[i]);
    attroff(COLOR_PAIR(PAIR_NORMAL));
    ui_draw_status("ENTER=Continue");
    while (1) {
        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == 27) return;
    }
}

static void screen_mbr_disk(void)
{
    static const char *const lines[] = {
        "Setup cannot install Debian GNU/Linux with WineExplorer on this disk,",
        "because the disk uses the MBR partition style. WineExplorer needs a disk",
        "with the GPT partition style.",
        "",
        "To use this disk, delete all of its partitions. Setup then prepares the",
        "disk with the GPT partition style.",
        "",
        "CAUTION: Deleting a partition deletes all data on it.",
        "",
        "Press ENTER to return to the list of partitions.",
    };
    screen_setup_message(lines, sizeof(lines) / sizeof(lines[0]));
}

static void screen_system_partition_selected(void)
{
    static const char *const lines[] = {
        "The selected partition holds system files that this computer needs",
        "to start. Setup cannot install Debian GNU/Linux with WineExplorer on it.",
        "",
        "Select another partition or unpartitioned space.",
        "",
        "Press ENTER to return to the list of partitions.",
    };
    screen_setup_message(lines, sizeof(lines) / sizeof(lines[0]));
}

static void screen_no_room_for_system(uint64_t need_mb)
{
    char need[96];
    snprintf(need, sizeof(need), "unpartitioned space on this disk. They need %lu MB.", (unsigned long)need_mb);
    const char *const lines[] = {
        "Setup cannot create the partitions for system files that this computer",
        "needs to start Debian GNU/Linux with WineExplorer: there is not enough",
        need,
        "",
        "To make room, delete a partition, or select another disk.",
        "",
        "Press ENTER to return to the list of partitions.",
    };
    screen_setup_message(lines, sizeof(lines) / sizeof(lines[0]));
}

/* Windows tells the user before it adds its system partitions to a disk; so does Setup. */
static bool screen_system_partitions_notice(const PartitionEntry *entries, int n)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 5;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "To make sure that all features of WineExplorer work correctly, Setup");
    mvprintw(base_y + 1, margin_x, "will also create these partitions for system files on this disk:");
    int row = base_y + 3;
    for (size_t k = 0; k < NUM_SYSTEM_PARTS; ++k) {
        if (disk_has_system_part(entries, n, system_parts[k].kind)) continue;
        mvprintw(row++, margin_x + 4, "%-24s %5lu MB", system_parts[k].gpt_name, (unsigned long)system_parts[k].size_mb);
    }
    mvprintw(row + 1, margin_x + 2, "- To continue, press ENTER.");
    mvprintw(row + 2, margin_x + 2, "- To return to the previous screen without changing the disk, press ESC.");
    attroff(COLOR_PAIR(PAIR_NORMAL));
    ui_draw_status("ENTER=Continue  ESC=Cancel");

    while (1) {
        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER) return true;
        if (ch == 27) return false;
    }
}

/* max_mb is the free space left once any missing system partitions are taken out of it. */
static bool screen_create_partition(DiskDevice *disk, uint64_t max_mb, uint64_t *out_size_mb)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 4 + 3 : 4;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "You asked Setup to create a new partition on");
    mvprintw(base_y + 1, margin_x, "%lu MB Disk (%s) on %s.", (unsigned long)disk->size_mb, disk->model, disk->bus);
    mvprintw(base_y + 3, margin_x + 2, "- To create the new partition, enter a size below and press ENTER.");
    mvprintw(base_y + 4, margin_x + 2, "- To return to the previous screen without creating the partition,");
    mvprintw(base_y + 5, margin_x + 4, "press ESC.");

    uint64_t min_mb = ROOT_MIN_MB;
    mvprintw(base_y + 8, margin_x, "The minimum size for the new partition is %lu megabytes (MB).", (unsigned long)min_mb);
    mvprintw(base_y + 9, margin_x, "The maximum size for the new partition is %lu megabytes (MB).", (unsigned long)max_mb);

    mvprintw(base_y + 12, margin_x, "Create partition of size (in MB):  [ %-8lu ]", (unsigned long)max_mb);
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("ENTER=Create  ESC=Cancel");

    char input_buf[16];
    snprintf(input_buf, sizeof(input_buf), "%lu", (unsigned long)max_mb);
    int pos = strlen(input_buf);

    curs_set(1);
    while (1) {
        attron(COLOR_PAIR(PAIR_NORMAL) | A_REVERSE);
        mvprintw(base_y + 12, margin_x + 37, "%-8s", input_buf);
        attroff(COLOR_PAIR(PAIR_NORMAL) | A_REVERSE);
        move(base_y + 12, margin_x + 37 + pos);
        refresh();

        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
            uint64_t val = strtoull(input_buf, NULL, 10);
            if (val >= min_mb && val <= max_mb) {
                *out_size_mb = val;
                curs_set(0);
                return true;
            }
        } else if (ch == 27) {
            curs_set(0);
            return false;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                input_buf[--pos] = '\0';
            }
        } else if (isdigit(ch) && pos < 8) {
            input_buf[pos++] = (char)ch;
            input_buf[pos] = '\0';
        }
    }
}

static bool screen_delete_partition(DiskDevice *disk, PartitionEntry *pe)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 4 + 3 : 4;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "You asked Setup to delete the partition:");
    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    mvprintw(base_y + 2, margin_x + 2, "%s  [%s]  %lu MB", pe->device, pe->fstype, (unsigned long)pe->size_mb);
    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    mvprintw(base_y + 4, margin_x, "on %lu MB Disk (%s) on %s.", (unsigned long)disk->size_mb, disk->model, disk->bus);

    mvprintw(base_y + 7, margin_x + 2, "- To delete this partition, press L.");
    attron(COLOR_PAIR(PAIR_REBOOT) | A_BOLD);
    mvprintw(base_y + 8, margin_x + 4, "CAUTION: All data on this partition will be lost.");
    attroff(COLOR_PAIR(PAIR_REBOOT) | A_BOLD);
    mvprintw(base_y + 10, margin_x + 2, "- To return to the previous screen without deleting the partition,");
    mvprintw(base_y + 11, margin_x + 4, "press ESC.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("L=Delete  ESC=Cancel");

    while (1) {
        int ch = ui_getch();
        if (ch == 'l' || ch == 'L') {
            return true;
        } else if (ch == 27 || ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
            return false;
        }
    }
}

static void close_all_disks(DiskDevice *disks, int num_disks)
{
    char cmd[256];

    for (int d = 0; d < num_disks; ++d) {
        if (disks[d].cxt) {
            fdisk_deassign_device(disks[d].cxt, 0);
            fdisk_unref_context(disks[d].cxt);
            disks[d].cxt = NULL;
        }
    }
    system("udevadm settle >/dev/null 2>&1 || sleep 1");
    /* every disk, not only sda (NVMe, virtio); the names come from libblkid */
    for (int d = 0; d < num_disks; ++d) {
        snprintf(cmd, sizeof(cmd), "blockdev --rereadpt %s 2>/dev/null || partx -u %s 2>/dev/null || true",
                 disks[d].device, disks[d].device);
        system(cmd);
    }
}

SetupStage screen_partition(SetupState *state)
{
    DiskDevice disks[MAX_DISKS];
    int num_disks = scan_disks(disks, MAX_DISKS);

    if (num_disks == 0) {
        ui_clear_screen();
        ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
        int err_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 8;
        int err_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;
        attron(COLOR_PAIR(PAIR_NORMAL));
        mvprintw(err_y, err_x, "Setup did not find any hard disk drives installed in your computer.");
        mvprintw(err_y + 2, err_x, "Setup cannot continue. Press F3 to quit.");
        attroff(COLOR_PAIR(PAIR_NORMAL));
        ui_draw_status("F3=Quit");
        while (1) {
            int ch = ui_getch();
            if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') return STAGE_QUIT;
        }
    }

    PartitionEntry entries[MAX_DISKS][MAX_PARTS];
    int num_entries[MAX_DISKS];
    for (int d = 0; d < num_disks; ++d) {
        num_entries[d] = scan_partitions(&disks[d], d, entries[d], MAX_PARTS);
    }

    /* Unattended execution */
    if (state->unattended) {
        int target_d = 0;   /* no TargetDisk: the first disk, as XP's unattended Setup */
        if (state->target_disk[0]) {
            target_d = -1;
            for (int d = 0; d < num_disks; ++d) {
                if (strcmp(disks[d].device, state->target_disk) == 0 ||
                    strcmp(disks[d].name, state->target_disk) == 0) {
                    target_d = d;
                    break;
                }
            }
        }
        if (target_d < 0) {
            /* never fall back to another disk: with Repartition = Yes it would be erased */
            ui_clear_screen();
            ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
            int err_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 8;
            int err_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;
            attron(COLOR_PAIR(PAIR_NORMAL));
            mvprintw(err_y, err_x, "Setup cannot find the disk %s named by TargetDisk in the answer file.",
                     state->target_disk);
            mvprintw(err_y + 1, err_x, "No disk has been changed.");
            mvprintw(err_y + 4, err_x, "Press F3 to quit Setup.");
            attroff(COLOR_PAIR(PAIR_NORMAL));
            ui_draw_status("F3=Quit");
            while (1) {
                int ch = ui_getch();
                if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') return STAGE_QUIT;
            }
        }
        DiskDevice *disk = &disks[target_d];
        snprintf(state->target_disk, sizeof(state->target_disk), "%s", disk->device);

        /* Check existing partitions */
        int existing_parts = 0;
        for (int i = 0; i < num_entries[target_d]; ++i) {
            if (!entries[target_d][i].is_free_space) existing_parts++;
        }

        if (existing_parts > 0 && !state->repartition) {
            ui_clear_screen();
            ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
            int err_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 8;
            int err_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;
            attron(COLOR_PAIR(PAIR_NORMAL));
            mvprintw(err_y, err_x, "Setup cannot continue because the target disk contains existing partitions");
            mvprintw(err_y + 1, err_x, "and Repartition = Yes was not specified in the answer file.");
            mvprintw(err_y + 4, err_x, "Press F3 to quit Setup.");
            attroff(COLOR_PAIR(PAIR_NORMAL));
            ui_draw_status("F3=Quit");
            while (1) {
                int ch = ui_getch();
                if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') return STAGE_QUIT;
            }
        }

        /* Auto-partition: a new GPT table on BIOS and UEFI machines alike, the system partitions first (BIOS boot,
         * EFI system, boot storage) and the root in the rest of the disk. */
        fdisk_create_disklabel(disk->cxt, "gpt");
        disk->is_gpt = true;

        uint64_t start = fdisk_get_first_lba(disk->cxt);
        uint64_t end = fdisk_get_last_lba(disk->cxt) + 1;
        size_t pno_root = 0;
        if (create_missing_system_partitions(disk, NULL, 0, &start, end) != 0 ||
            align_mb(disk, start) >= end ||
            add_partition_to_disk(disk, align_mb(disk, start), end - align_mb(disk, start), NULL, NULL, &pno_root) != 0) {
            close_all_disks(disks, num_disks);
            static const char *const lines[] = {
                "Setup cannot partition the target disk: it is too small for the",
                "system partitions and Debian GNU/Linux with WineExplorer.",
                "No disk has been changed.",
                "",
                "Press ENTER to quit Setup.",
            };
            screen_setup_message(lines, sizeof(lines) / sizeof(lines[0]));
            return STAGE_QUIT;
        }
        format_partition_device(state->target_part, sizeof(state->target_part), disk->device, pno_root + 1);
        snprintf(state->target_fstype, sizeof(state->target_fstype), "Unformatted");
        num_entries[target_d] = scan_partitions(disk, target_d, entries[target_d], MAX_PARTS);
        record_target_layout(state, entries[target_d], num_entries[target_d]);
        state->format_esp = true;
        state->format_boot_storage = true;
        state->wipe_bios_boot = true;

        state->target_disk_idx = target_d;
        state->target_disk_size_mb = disk->size_mb;
        snprintf(state->target_disk_bus, sizeof(state->target_disk_bus), "%s", disk->bus);
        state->target_disk_is_gpt = disk->is_gpt;
        fdisk_write_disklabel(disk->cxt);
        close_all_disks(disks, num_disks);
        char probe_cmd[256];
        snprintf(probe_cmd, sizeof(probe_cmd), "partprobe %s >/dev/null 2>&1 || blockdev --rereadpt %s >/dev/null 2>&1 || true", disk->device, disk->device);
        system(probe_cmd);
        system("udevadm settle >/dev/null 2>&1 || sleep 1");
        state->format_choice = 0; /* Quick ext4 */
        return STAGE_FORMAT;
    }

    /* Flatten items for display list */
    typedef struct {
        int disk_idx;
        int entry_idx;
    } ListItem;

    ListItem items[MAX_DISKS * MAX_PARTS];
    int total_items = 0;
    for (int d = 0; d < num_disks; ++d) {
        for (int e = 0; e < num_entries[d]; ++e) {
            items[total_items].disk_idx = d;
            items[total_items].entry_idx = e;
            total_items++;
        }
    }

    int selected_idx = 0;

    while (1) {
        ui_clear_screen();
        ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

        int base_y = (LINES > 25) ? (LINES - 25) / 4 + 3 : 3;
        int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 4 : 4;
        int box_w = (COLS > 80) ? (COLS - margin_x * 2) : (COLS - 8);
        if (box_w < 50) box_w = 50;
        int max_row = (LINES > 25) ? (LINES - 3) : 22;

        attron(COLOR_PAIR(PAIR_NORMAL));
        mvprintw(base_y, margin_x, "The following list shows the existing partitions and unpartitioned space");
        mvprintw(base_y + 1, margin_x, "on this computer.");
        mvprintw(base_y + 3, margin_x, "Use the UP and DOWN ARROW keys to select an item in the list.");
        mvprintw(base_y + 4, margin_x + 2, "- To set up Debian GNU/Linux with WineExplorer on the selected item,");
        mvprintw(base_y + 5, margin_x + 4, "press ENTER.");
        mvprintw(base_y + 6, margin_x + 2, "- To create a partition in the unpartitioned space, press C.");
        mvprintw(base_y + 7, margin_x + 2, "- To delete the selected partition, press D.");

        mvhline(base_y + 8, margin_x, ACS_HLINE, box_w);

        /* Render items */
        int cur_row = base_y + 9;
        int cur_disk = -1;

        for (int i = 0; i < total_items && cur_row < max_row; ++i) {
            int d = items[i].disk_idx;
            int e = items[i].entry_idx;
            DiskDevice *disk = &disks[d];
            PartitionEntry *pe = &entries[d][e];

            if (d != cur_disk) {
                cur_disk = d;
                attron(COLOR_PAIR(PAIR_NORMAL) | A_BOLD);
                mvprintw(cur_row++, margin_x + 2, "%5lu MB Disk %d at Id %d on bus 0 on %s [%s]",
                         (unsigned long)disk->size_mb, d, d, disk->bus,
                         disk->is_gpt ? "GPT" : "MBR");
                attroff(COLOR_PAIR(PAIR_NORMAL) | A_BOLD);
            }

            bool is_sel = (i == selected_idx);
            if (is_sel) {
                attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
            } else {
                attron(COLOR_PAIR(PAIR_NORMAL));
            }

            if (pe->is_free_space) {
                mvprintw(cur_row++, margin_x + 4, "   Unpartitioned space                        %5lu MB        ",
                         (unsigned long)pe->size_mb);
            } else {
                const char *fs_display = pe->fstype;
                if (!fs_display[0] || strcmp(fs_display, "Unformatted") == 0) fs_display = "New (Raw)";

                /* System partitions get no drive letter, as XP shows a partition it does not mount. */
                char desc[80];
                if (pe->sys_kind != SYS_NONE) {
                    snprintf(desc, sizeof(desc), "   Partition%zu (%s) [%s]", pe->partno,
                             system_part_display(pe->sys_kind), fs_display);
                } else {
                    snprintf(desc, sizeof(desc), "%c: Partition%zu [%s]",
                             partition_letter(entries[d], num_entries[d], e), pe->partno, fs_display);
                }
                mvprintw(cur_row++, margin_x + 4, "%-44s %5lu MB (%5lu MB free)",
                         desc, (unsigned long)pe->size_mb, (unsigned long)pe->size_mb);
            }

            if (is_sel) {
                attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
            } else {
                attroff(COLOR_PAIR(PAIR_NORMAL));
            }
        }

        mvhline(cur_row, margin_x, ACS_HLINE, box_w);
        attroff(COLOR_PAIR(PAIR_NORMAL));

        ui_draw_status("ENTER=Install  C=Create Partition  D=Delete  F3=Quit");

        int ch = ui_getch();
        if (ch == KEY_UP) {
            if (selected_idx > 0) selected_idx--;
        } else if (ch == KEY_DOWN) {
            if (selected_idx < total_items - 1) selected_idx++;
        } else if (ch == 'c' || ch == 'C') {
            if (total_items > 0) {
                int d = items[selected_idx].disk_idx;
                int e = items[selected_idx].entry_idx;
                PartitionEntry *pe = &entries[d][e];
                if (pe->is_free_space) {
                    if (!disks[d].is_gpt) {
                        screen_mbr_disk();
                        continue;
                    }
                    /* The first partition on a disk without system partitions makes room for them first, at the
                     * start of the free space, as Windows does. */
                    uint64_t sys_mb = missing_system_mb(entries[d], num_entries[d]);
                    uint64_t usable_mb = region_usable_mb(&disks[d], pe);
                    if (usable_mb < sys_mb + ROOT_MIN_MB) {
                        screen_no_room_for_system(sys_mb);
                        continue;
                    }
                    uint64_t new_mb = 0;
                    if (screen_create_partition(&disks[d], usable_mb - sys_mb, &new_mb) &&
                        (sys_mb == 0 || screen_system_partitions_notice(entries[d], num_entries[d]))) {
                        uint64_t start = pe->start_sector;
                        uint64_t end = pe->start_sector + pe->num_sectors;
                        size_t new_pno = 0;
                        bool created = false;
                        if (create_missing_system_partitions(&disks[d], entries[d], num_entries[d], &start, end) == 0) {
                            start = align_mb(&disks[d], start);
                            uint64_t num_sec = mb_to_sectors(&disks[d], new_mb);
                            if (start + num_sec > end) num_sec = end - start;
                            created = (add_partition_to_disk(&disks[d], start, num_sec, NULL, NULL, &new_pno) == 0);
                        }
                        /* Re-scan */
                        num_entries[d] = scan_partitions(&disks[d], d, entries[d], MAX_PARTS);
                        total_items = 0;
                        for (int td = 0; td < num_disks; ++td) {
                            for (int te = 0; te < num_entries[td]; ++te) {
                                items[total_items].disk_idx = td;
                                items[total_items].entry_idx = te;
                                /* The new partition is selected, as in Windows Setup, so ENTER installs on it. */
                                if (created && td == d && !entries[td][te].is_free_space &&
                                    entries[td][te].partno == new_pno + 1)
                                    selected_idx = total_items;
                                total_items++;
                            }
                        }
                        if (selected_idx >= total_items && total_items > 0) selected_idx = total_items - 1;
                    }
                }
            }
        } else if (ch == 'd' || ch == 'D') {
            if (total_items > 0) {
                int d = items[selected_idx].disk_idx;
                int e = items[selected_idx].entry_idx;
                PartitionEntry *pe = &entries[d][e];
                if (!pe->is_free_space) {
                    if (screen_delete_partition(&disks[d], pe)) {
                        fdisk_delete_partition(disks[d].cxt, pe->partno - 1);
                        /* Re-scan */
                        num_entries[d] = scan_partitions(&disks[d], d, entries[d], MAX_PARTS);
                        total_items = 0;
                        for (int td = 0; td < num_disks; ++td) {
                            for (int te = 0; te < num_entries[td]; ++te) {
                                items[total_items].disk_idx = td;
                                items[total_items].entry_idx = te;
                                total_items++;
                            }
                        }
                        if (selected_idx >= total_items && total_items > 0) {
                            selected_idx = total_items - 1;
                        }
                    }
                }
            }
        } else if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
            if (total_items > 0) {
                int d = items[selected_idx].disk_idx;
                int e = items[selected_idx].entry_idx;
                const PartitionEntry *pe = &entries[d][e];
                DiskDevice *disk = &disks[d];

                if (!disk->is_gpt) {
                    screen_mbr_disk();
                    continue;
                }
                if (!pe->is_free_space && pe->sys_kind != SYS_NONE) {
                    screen_system_partition_selected();
                    continue;
                }

                /* Missing system partitions go at the start of the chosen free space, or, when an existing
                 * partition is chosen, into the largest free space on the disk. */
                uint64_t sys_mb = missing_system_mb(entries[d], num_entries[d]);
                const PartitionEntry *region = pe->is_free_space ? pe : largest_free_region(entries[d], num_entries[d]);
                if (sys_mb > 0) {
                    uint64_t need_mb = sys_mb + (pe->is_free_space ? ROOT_MIN_MB : 0);
                    if (!region || region_usable_mb(disk, region) < need_mb) {
                        screen_no_room_for_system(sys_mb);
                        continue;
                    }
                    if (!screen_system_partitions_notice(entries[d], num_entries[d])) continue;
                }

                /* Copy what is needed from the entries: creating partitions and re-scanning replaces them. */
                bool target_is_free = pe->is_free_space;
                uint64_t start = region ? region->start_sector : 0;
                uint64_t end = region ? region->start_sector + region->num_sectors : 0;
                if (!target_is_free) {
                    snprintf(state->target_part, sizeof(state->target_part), "%s", pe->device);
                    snprintf(state->target_fstype, sizeof(state->target_fstype), "%s", pe->fstype);
                }

                bool made_bios_boot = !disk_has_system_part(entries[d], num_entries[d], SYS_BIOS_BOOT);
                bool made_esp = !disk_has_system_part(entries[d], num_entries[d], SYS_ESP);
                bool made_boot_storage = !disk_has_system_part(entries[d], num_entries[d], SYS_BOOT_STORAGE);
                bool ok = (sys_mb == 0 ||
                           create_missing_system_partitions(disk, entries[d], num_entries[d], &start, end) == 0);
                if (ok && target_is_free) {
                    size_t new_pno = 0;
                    start = align_mb(disk, start);
                    ok = (start < end && add_partition_to_disk(disk, start, end - start, NULL, NULL, &new_pno) == 0);
                    if (ok) {
                        format_partition_device(state->target_part, sizeof(state->target_part), disk->device, new_pno + 1);
                        snprintf(state->target_fstype, sizeof(state->target_fstype), "Unformatted");
                    }
                }
                num_entries[d] = scan_partitions(disk, d, entries[d], MAX_PARTS);
                if (!ok) {
                    /* Nothing is written yet; show the disk as libfdisk now has it. */
                    screen_no_room_for_system(sys_mb);
                    total_items = 0;
                    for (int td = 0; td < num_disks; ++td) {
                        for (int te = 0; te < num_entries[td]; ++te) {
                            items[total_items].disk_idx = td;
                            items[total_items].entry_idx = te;
                            total_items++;
                        }
                    }
                    if (selected_idx >= total_items && total_items > 0) selected_idx = total_items - 1;
                    continue;
                }

                snprintf(state->target_disk, sizeof(state->target_disk), "%s", disk->device);
                record_target_layout(state, entries[d], num_entries[d]);
                state->format_esp = made_esp;
                state->format_boot_storage = made_boot_storage;
                state->wipe_bios_boot = made_bios_boot;
                state->target_disk_idx = d;
                state->target_disk_size_mb = disk->size_mb;
                snprintf(state->target_disk_bus, sizeof(state->target_disk_bus), "%s", disk->bus);
                state->target_disk_is_gpt = disk->is_gpt;

                /* Commit partition table upon exiting partition screen */
                fdisk_write_disklabel(disk->cxt);
                close_all_disks(disks, num_disks);
                char probe_cmd[256];
                snprintf(probe_cmd, sizeof(probe_cmd), "partprobe %s >/dev/null 2>&1 || blockdev --rereadpt %s >/dev/null 2>&1 || true", disk->device, disk->device);
                system(probe_cmd);
                system("udevadm settle >/dev/null 2>&1 || sleep 1");
                return STAGE_FORMAT;
            }
        } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
            close_all_disks(disks, num_disks);
            return STAGE_QUIT;
        }
    }
}

static void wait_for_device(const char *dev)
{
    for (int retry = 0; dev[0] && retry < 10; ++retry) {
        if (access(dev, F_OK) == 0) return;
        usleep(500000);
        system("udevadm settle >/dev/null 2>&1 || true");
    }
}

/* Removes old file system and RAID signatures, so that a new partition placed over old data shows nothing that was
 * there before (blkid would otherwise still find, for example, the ext4 of a deleted partition that started there). */
static void wipe_signatures(const char *dev)
{
    if (!dev[0]) return;
    wait_for_device(dev);
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "wipefs -a -q %s >/dev/null 2>&1", dev);
    system(cmd);
}

/* FAT32 on a system partition: always when force is set (Setup created it), else only if it has no FAT yet. */
static void format_fat_partition(const char *dev, bool force, const char *label)
{
    if (!dev[0]) return;
    wait_for_device(dev);
    bool has_vfat = false;
    blkid_probe pr = blkid_new_probe_from_filename(dev);
    if (pr) {
        blkid_do_probe(pr);
        const char *val = NULL;
        if (blkid_probe_lookup_value(pr, "TYPE", &val, NULL) == 0 && val) has_vfat = (strcmp(val, "vfat") == 0);
        blkid_free_probe(pr);
    }
    if (has_vfat && !force) return;
    wipe_signatures(dev);
    char cmd[256];
    if (label) snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 -n %s %s >/dev/null 2>&1", label, dev);
    else snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 %s >/dev/null 2>&1", dev);
    if (system(cmd) != 0) {
        sleep(1);
        system(cmd);
    }
}

SetupStage screen_format(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 4;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    char drive_letter = state->target_letter ? state->target_letter : 'C';

    const char *fs_display = state->target_fstype[0] ? state->target_fstype : "New (Raw)";
    if (strcmp(fs_display, "Unformatted") == 0) fs_display = "New (Raw)";

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "Setup will install Debian GNU/Linux with WineExplorer on partition");
    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    mvprintw(base_y + 2, margin_x + 4, "%c: Partition%zu [%s]   %lu MB ( %lu MB free)",
             drive_letter, state->target_partno, fs_display,
             (unsigned long)state->target_size_mb, (unsigned long)state->target_size_mb);
    mvprintw(base_y + 3, margin_x + 4, "on %lu MB Disk %d at Id %d on bus 0 on %s [%s].",
             (unsigned long)state->target_disk_size_mb, state->target_disk_idx, state->target_disk_idx,
             state->target_disk_bus[0] ? state->target_disk_bus : "virtio",
             state->target_disk_is_gpt ? "GPT" : "MBR");
    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    mvprintw(base_y + 5, margin_x, "The partition must now be formatted.");
    mvprintw(base_y + 7, margin_x, "Use the UP and DOWN ARROW keys to select the file system you want,");
    mvprintw(base_y + 8, margin_x, "and then press ENTER.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    bool has_ext4 = (strcmp(state->target_fstype, "ext4") == 0);
    const char *options[3];
    int opt_count = 0;

    options[opt_count++] = "Format the partition using the ext4 file system (Quick)";
    options[opt_count++] = "Format the partition using the ext4 file system";
    if (has_ext4) {
        options[opt_count++] = "Leave the current file system intact (no changes)";
    }

    int selection = 0;

    if (!state->unattended) {
        while (1) {
            for (int i = 0; i < opt_count; ++i) {
                if (i == selection) {
                    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
                    mvprintw(base_y + 9 + (i * 2), margin_x + 2, " %-60s ", options[i]);
                    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
                } else {
                    attron(COLOR_PAIR(PAIR_NORMAL));
                    mvprintw(base_y + 9 + (i * 2), margin_x + 2, " %-60s ", options[i]);
                    attroff(COLOR_PAIR(PAIR_NORMAL));
                }
            }

            ui_draw_status("ENTER=Continue  ESC=Cancel  F3=Quit");

            int ch = ui_getch();
            if (ch == KEY_UP) {
                if (selection > 0) selection--;
            } else if (ch == KEY_DOWN) {
                if (selection < opt_count - 1) selection++;
            } else if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
                state->format_choice = selection;
                break;
            } else if (ch == 27) {
                return STAGE_PARTITION;
            } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
                return STAGE_QUIT;
            }
        }
    }

    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y + 2, margin_x, "Please wait while Setup formats the partition.");
    attroff(COLOR_PAIR(PAIR_NORMAL));
    ui_draw_status("Formatting drive...");

    char probe_cmd[256];
    if (state->target_disk[0]) {
        snprintf(probe_cmd, sizeof(probe_cmd), "partprobe %s >/dev/null 2>&1 || blockdev --rereadpt %s >/dev/null 2>&1 || true", state->target_disk, state->target_disk);
        system(probe_cmd);
    }
    system("udevadm settle >/dev/null 2>&1 || sleep 1");

    for (int retry = 0; retry < 10; ++retry) {
        if (access(state->target_part, F_OK) == 0) break;
        usleep(500000);
        system("udevadm settle >/dev/null 2>&1 || true");
    }

    for (int p = 0; p <= 40; p += 10) {
        ui_draw_centered_progressbar(p, PAIR_PROGRESS);
        usleep(60000);
    }

    /* Format according to choice */
    if (state->format_choice == 0) {
        /* Quick ext4 */
        char fmt_root[256];
        snprintf(fmt_root, sizeof(fmt_root), "mkfs.ext4 -F -q %s >/dev/null 2>&1", state->target_part);
        if (system(fmt_root) != 0) {
            sleep(1);
            system(fmt_root);
        }
    } else if (state->format_choice == 1) {
        /* Full ext4 */
        char fmt_root[256];
        snprintf(fmt_root, sizeof(fmt_root), "mkfs.ext4 -F %s >/dev/null 2>&1", state->target_part);
        if (system(fmt_root) != 0) {
            sleep(1);
            system(fmt_root);
        }
    }
    /* choice == 2: Leave intact, do NOT format */

    /* The ESP and boot storage get FAT32 when Setup created them, or when they hold no FAT file system yet, on BIOS
     * and UEFI machines alike. An existing ESP with a file system (for example one Windows uses) is never
     * formatted. The BIOS boot partition stays raw: GRUB writes its core image there. */
    if (state->wipe_bios_boot) wipe_signatures(state->bios_boot_part);
    format_fat_partition(state->efi_part, state->format_esp, NULL);
    format_fat_partition(state->boot_storage_part, state->format_boot_storage, "BOOTSTORAGE");

    for (int p = 50; p <= 100; p += 10) {
        ui_draw_centered_progressbar(p, PAIR_PROGRESS);
        usleep(60000);
    }

    return STAGE_COPY;
}

static bool copy_file_in_process(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) return false;

    if (S_ISLNK(st.st_mode)) {
        char link_target[1024];
        ssize_t len = readlink(src, link_target, sizeof(link_target) - 1);
        if (len < 0) return false;
        link_target[len] = '\0';
        unlink(dst);
        if (symlink(link_target, dst) < 0) return false;
        lchown(dst, st.st_uid, st.st_gid);
        return true;
    }

    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) {
        unlink(dst);
        if (mknod(dst, st.st_mode, st.st_rdev) < 0) return false;
        chown(dst, st.st_uid, st.st_gid);
        return true;
    }

    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return false;

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (dfd < 0) {
        close(sfd);
        return false;
    }

    bool ok = true;
    off_t total_written = 0;
    off_t file_size = st.st_size;

    while (total_written < file_size) {
        loff_t off_in = total_written;
        loff_t off_out = total_written;
        size_t chunk = (file_size - total_written > 1048576) ? 1048576 : (size_t)(file_size - total_written);
        ssize_t ret = copy_file_range(sfd, &off_in, dfd, &off_out, chunk, 0);
        if (ret > 0) {
            total_written += ret;
        } else {
            /* Fallback to read/write */
            if (lseek(sfd, total_written, SEEK_SET) < 0 || lseek(dfd, total_written, SEEK_SET) < 0) {
                ok = false;
                break;
            }
            char buf[65536];
            ssize_t r;
            while ((r = read(sfd, buf, sizeof(buf))) > 0) {
                ssize_t w = 0;
                while (w < r) {
                    ssize_t res = write(dfd, buf + w, r - w);
                    if (res < 0) { ok = false; break; }
                    w += res;
                }
                if (!ok) break;
            }
            if (r < 0) ok = false;
            break;
        }
    }

    fchown(dfd, st.st_uid, st.st_gid);
    fchmod(dfd, st.st_mode);

    struct timespec times[2];
    times[0] = st.st_atim;
    times[1] = st.st_mtim;
    futimens(dfd, times);

    close(sfd);
    close(dfd);
    return ok;
}

SetupStage screen_copy(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 5;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "Please wait while Setup copies files to the installation folder.");
    mvprintw(base_y + 1, margin_x, "This might take several minutes to complete.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    mkdir("/target", 0755);
    for (int retry = 0; retry < 10; ++retry) {
        if (access(state->target_part, F_OK) == 0) break;
        usleep(500000);
        system("udevadm settle >/dev/null 2>&1 || true");
    }

    char mnt_root[256];
    snprintf(mnt_root, sizeof(mnt_root), "mount %s /target >/dev/null 2>&1", state->target_part);
    if (system(mnt_root) != 0) {
        sleep(1);
        system(mnt_root);
    }

    if (system("mountpoint -q /target") != 0) {
        ui_clear_screen();
        ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
        attron(COLOR_PAIR(PAIR_NORMAL));
        mvprintw(6, 6, "Setup was unable to mount the target partition: %s", state->target_part);
        mvprintw(8, 6, "Press F3 to quit Setup.");
        attroff(COLOR_PAIR(PAIR_NORMAL));
        ui_draw_status("F3=Quit");
        while (1) {
            int ch = ui_getch();
            if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') return STAGE_QUIT;
        }
    }

    if (state->is_uefi && state->efi_part[0]) {
        safe_mkdir_p("/target/boot/efi");
        char mnt_efi[256];
        snprintf(mnt_efi, sizeof(mnt_efi), "mount %s /target/boot/efi >/dev/null 2>&1", state->efi_part);
        system(mnt_efi);
    }

    ui_draw_centered_progressbar(0, PAIR_PROGRESS);
    ui_draw_status("Reading installation layout...");

    /* Read layout.inf */
    const char *layout_candidates[] = {
        "/run/live/medium/setup/layout.inf",
        "/setup/layout.inf",
        "setupinfs/layout.inf",
        NULL
    };
    const char *layout_path = NULL;
    for (int i = 0; layout_candidates[i]; ++i) {
        if (access(layout_candidates[i], R_OK) == 0) {
            layout_path = layout_candidates[i];
            break;
        }
    }

    uint64_t total_bytes = 0;
    uint64_t copied_bytes = 0;

    /* Parse layout.inf file list */
    typedef struct {
        char rel_path[256];
        uint64_t size;
    } FileCopyItem;

    FileCopyItem *files = NULL;
    size_t file_count = 0;
    size_t file_cap = 0;

    if (layout_path) {
        FILE *lf = fopen(layout_path, "r");
        if (lf) {
            if (!copy_layout_directories(lf, "", "/target")) {
                fclose(lf);
                ui_draw_status("Setup could not preserve installation directory ownership and permissions.");
                sleep(5);
                return STAGE_QUIT;
            }
            rewind(lf);
            char line[512];
            bool in_files = false;
            while (fgets(line, sizeof(line), lf)) {
                char *p = line;
                while (isspace((unsigned char)*p)) p++;
                if (*p == ';' || *p == '#' || *p == '\0') continue;
                if (*p == '[') {
                    char sec[64];
                    if (sscanf(p, "[%63[^]]", sec) == 1) {
                        in_files = (strcasecmp(sec, "SourceDisksFiles") == 0);
                    }
                    continue;
                }
                if (in_files) {
                    char path[256], val[256];
                    if (sscanf(p, "%255[^=]=%255[^\r\n]", path, val) == 2) {
                        char *pk = path;
                        while (*pk && isspace((unsigned char)*pk)) pk++;
                        char *pk_end = pk + strlen(pk) - 1;
                        while (pk_end > pk && isspace((unsigned char)*pk_end)) *pk_end-- = '\0';

                        uint64_t fsz = 0;
                        sscanf(val, "%*d,,%lu", &fsz);

                        if (file_count >= file_cap) {
                            file_cap = file_cap ? (file_cap * 2) : 16384;
                            files = realloc(files, file_cap * sizeof(FileCopyItem));
                        }
                        snprintf(files[file_count].rel_path, sizeof(files[file_count].rel_path), "%s", pk);
                        files[file_count].size = fsz;
                        total_bytes += fsz;
                        file_count++;
                    }
                }
            }
            fclose(lf);
        }
    }

    if (total_bytes == 0) total_bytes = 1024ULL * 1024ULL * 1024ULL; /* Fallback */

    struct timespec last_ui_time;
    clock_gettime(CLOCK_MONOTONIC, &last_ui_time);

    if (file_count > 0 && files) {
        for (size_t i = 0; i < file_count; ++i) {
            char src_path[512], dst_path[512];
            snprintf(src_path, sizeof(src_path), "/%s", files[i].rel_path);
            snprintf(dst_path, sizeof(dst_path), "/target/%s", files[i].rel_path);

            char *tmp_dname = strdup(dst_path);
            if (tmp_dname) {
                safe_mkdir_p(dirname(tmp_dname));
                free(tmp_dname);
            }

            /* In-process copy with retry prompt on error */
            while (!copy_file_in_process(src_path, dst_path)) {
                if (access(src_path, R_OK) != 0) {
                    /* Source does not exist, skip */
                    break;
                }
                /* Show XP copy error screen */
                ui_clear_screen();
                ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
                attron(COLOR_PAIR(PAIR_NORMAL));
                mvprintw(6, 6, "Setup cannot copy the file: %s", files[i].rel_path);
                mvprintw(9, 8, "- To retry, press ENTER.");
                mvprintw(11, 8, "- To skip this file, press ESC.");
                mvprintw(13, 8, "- To quit Setup, press F3.");
                attroff(COLOR_PAIR(PAIR_NORMAL));
                ui_draw_status("ENTER=Retry  ESC=Skip  F3=Quit");

                int ch = ui_getch();
                if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
                    continue; /* retry */
                } else if (ch == 27) {
                    break; /* skip */
                } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
                    free(files);
                    return STAGE_QUIT;
                }
            }

            copied_bytes += files[i].size;

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - last_ui_time.tv_sec) * 1000 +
                              (now.tv_nsec - last_ui_time.tv_nsec) / 1000000;

            if (elapsed_ms >= 250 || i == file_count - 1) {
                last_ui_time = now;
                int pct = (int)((copied_bytes * 100ULL) / total_bytes);
                if (pct > 100) pct = 100;
                ui_draw_centered_progressbar(pct, PAIR_PROGRESS);
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Copying: %s", basename(files[i].rel_path));
                ui_draw_status(status_msg);
            }
        }
        free(files);

        /* Recreate symlinks from [Links] */
        if (layout_path) {
            FILE *lf = fopen(layout_path, "r");
            if (lf) {
                char line[512];
                bool in_links = false;
                while (fgets(line, sizeof(line), lf)) {
                    char *p = line;
                    while (isspace((unsigned char)*p)) p++;
                    if (*p == ';' || *p == '#' || *p == '\0') continue;
                    if (*p == '[') {
                        char sec[64];
                        if (sscanf(p, "[%63[^]]", sec) == 1) {
                            in_links = (strcasecmp(sec, "Links") == 0);
                        }
                        continue;
                    }
                    if (in_links) {
                        char path[256], target[256];
                        if (sscanf(p, "%255[^=]=%255[^\r\n]", path, target) == 2) {
                            char *pk = path;
                            while (*pk && isspace((unsigned char)*pk)) pk++;
                            char *pk_end = pk + strlen(pk) - 1;
                            while (pk_end > pk && isspace((unsigned char)*pk_end)) *pk_end-- = '\0';

                            char *pt = target;
                            while (*pt && isspace((unsigned char)*pt)) pt++;
                            char *pt_end = pt + strlen(pt) - 1;
                            while (pt_end > pt && isspace((unsigned char)*pt_end)) *pt_end-- = '\0';
                            if (*pt == '"' && *pt_end == '"' && pt_end > pt) {
                                *pt_end = '\0';
                                pt++;
                            }

                            char dst_link[512];
                            snprintf(dst_link, sizeof(dst_link), "/target/%s", pk);
                            char *tmp_dname = strdup(dst_link);
                            if (tmp_dname) {
                                safe_mkdir_p(dirname(tmp_dname));
                                free(tmp_dname);
                            }
                            unlink(dst_link);
                            symlink(pt, dst_link);
                        }
                    }
                }
                fclose(lf);
            }
        }
    } else {
        /* Fallback if layout.inf was not found */
        const char *dirs[] = {"bin", "boot", "etc", "lib", "lib64", "opt", "root", "sbin", "usr", "var", NULL};
        for (int i = 0; dirs[i]; ++i) {
            char status_msg[128];
            snprintf(status_msg, sizeof(status_msg), "Copying: /%s/...", dirs[i]);
            ui_draw_status(status_msg);
            int pct = (i * 100) / 10;
            ui_draw_centered_progressbar(pct, PAIR_PROGRESS);
            char cp_cmd[256];
            snprintf(cp_cmd, sizeof(cp_cmd), "cp -a /%s /target/ 2>/dev/null || true", dirs[i]);
            system(cp_cmd);
        }
    }

    ui_draw_centered_progressbar(100, PAIR_PROGRESS);
    ui_draw_status("Copying files... (100%)");
    usleep(250000);

    return STAGE_INIT;
}

SetupStage screen_init(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(8, 6, "Please wait while Setup initializes your Debian GNU/Linux with");
    mvprintw(9, 6, "WineExplorer configuration.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("Saving configuration...");

    /* Essential dirs */
    mkdir("/target/proc", 0755);
    mkdir("/target/sys", 0755);
    mkdir("/target/dev", 0755);
    mkdir("/target/run", 0755);
    mkdir("/target/tmp", 01777);
    mkdir("/target/home", 0755);

    /* 1. Calculate and create swapfile: 1.5x RAM capped at 8 GB, only shrinking it to keep 8960 MB free on
     * /target (each WoW64 Wine prefix takes about 1.7 GB: the template, the first users and LightDM). */
    uint64_t ram_kb = 2097152; /* default 2GB */
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %lu kB", &ram_kb) == 1) break;
        }
        fclose(mf);
    }
    uint64_t ram_mb = ram_kb / 1024;
    uint64_t swap_mb = (ram_mb * 3) / 2;
    if (swap_mb > 8192) swap_mb = 8192;

    struct statvfs sv;
    if (statvfs("/target", &sv) == 0) {
        uint64_t free_mb = (sv.f_bavail * sv.f_frsize) / (1024 * 1024);
        const uint64_t min_free_mb = 8960;
        if (free_mb <= min_free_mb) {
            swap_mb = 0;
        } else if (swap_mb > free_mb - min_free_mb) {
            swap_mb = free_mb - min_free_mb;
        }
    }

    if (swap_mb > 0) {
        char swap_cmd[256];
        snprintf(swap_cmd, sizeof(swap_cmd),
                 "fallocate -l %luM /target/swapfile 2>/dev/null || dd if=/dev/zero of=/target/swapfile bs=1M count=%lu 2>/dev/null",
                 (unsigned long)swap_mb, (unsigned long)swap_mb);
        system(swap_cmd);
        system("chmod 600 /target/swapfile && mkswap /target/swapfile >/dev/null 2>&1 || true");
    }

    /* 2. Generate /target/etc/fstab */
    char root_uuid[128] = "";
    blkid_probe pr = blkid_new_probe_from_filename(state->target_part);
    if (pr) {
        blkid_do_probe(pr);
        const char *val = NULL;
        if (blkid_probe_lookup_value(pr, "UUID", &val, NULL) == 0 && val) {
            snprintf(root_uuid, sizeof(root_uuid), "%s", val);
        }
        blkid_free_probe(pr);
    }

    FILE *fstab = fopen("/target/etc/fstab", "w");
    if (fstab) {
        fprintf(fstab, "# /etc/fstab: static file system information.\n");
        if (root_uuid[0]) {
            fprintf(fstab, "UUID=%s  /  ext4  errors=remount-ro  0  1\n", root_uuid);
        } else {
            fprintf(fstab, "%s  /  ext4  errors=remount-ro  0  1\n", state->target_part);
        }

        if (state->is_uefi && state->efi_part[0]) {
            char efi_uuid[128] = "";
            blkid_probe epr = blkid_new_probe_from_filename(state->efi_part);
            if (epr) {
                blkid_do_probe(epr);
                const char *val = NULL;
                if (blkid_probe_lookup_value(epr, "UUID", &val, NULL) == 0 && val) {
                    snprintf(efi_uuid, sizeof(efi_uuid), "%s", val);
                }
                blkid_free_probe(epr);
            }
            if (efi_uuid[0]) {
                fprintf(fstab, "UUID=%s  /boot/efi  vfat  umask=0077  0  1\n", efi_uuid);
            } else {
                fprintf(fstab, "%s  /boot/efi  vfat  umask=0077  0  1\n", state->efi_part);
            }
        }

        if (swap_mb > 0) {
            fprintf(fstab, "/swapfile  none  swap  sw  0  0\n");
        }
        fprintf(fstab, "tmpfs  /tmp  tmpfs  defaults,nosuid,nodev  0  0\n");
        fclose(fstab);
    }

    /* 3. Purge live artifacts from target */
    system("rm -f /target/etc/sudoers.d/live /target/etc/sudoers.d/wineuser /target/etc/sudoers.d/wexsetup");
    system("rm -rf /target/etc/live /target/setup /target/home/wineuser /target/var/lib/AccountsService/users/wineuser");
    system("rm -f /target/usr/lib/systemd/system/wex-textsetup.service /target/etc/systemd/system/multi-user.target.wants/wex-textsetup.service");

    /* Remove the live desktop's automatic logon before installed setup. */
    /* live-config also edits the main file, which takes precedence over conf.d. */
    FILE *fmainlogin = fopen("/target/etc/lightdm/lightdm.conf", "w");
    if (fmainlogin) {
        fprintf(fmainlogin, "[Seat:*]\n");
        fclose(fmainlogin);
    }
    FILE *flogin = fopen("/target/etc/lightdm/lightdm.conf.d/60-wineexplorer-autologin.conf", "w");
    if (flogin) {
        fprintf(flogin, "[Seat:*]\nautologin-user=\n");
        fclose(flogin);
    }

    /* 4. Setup state and answer file */
    safe_mkdir_p("/target/etc/wineexplorer/setup");
    system("chmod 750 /target/etc/wineexplorer /target/etc/wineexplorer/setup");
    FILE *f_stage = fopen("/target/etc/wineexplorer/setup/stage", "w");
    if (f_stage) {
        fprintf(f_stage, "gui\n");
        fclose(f_stage);
    }

    if (state->unattended && state->unattend_path[0]) {
        char cp_unattend[512];
        snprintf(cp_unattend, sizeof(cp_unattend),
                 "cp -f '%s' /target/etc/wineexplorer/setup/unattend.sif 2>/dev/null || true",
                 state->unattend_path);
        system(cp_unattend);
        system("chmod 640 /target/etc/wineexplorer/setup/unattend.sif 2>/dev/null || true");
    }

    /* 5. Set up machine-id */
    system("systemd-machine-id-setup --root=/target >/dev/null 2>&1 || true");
    system("mkdir -p /target/var/lib/dbus 2>/dev/null || true");
    system("ln -sf /etc/machine-id /target/var/lib/dbus/machine-id 2>/dev/null || true");

    /* 6. Ensure root UsrMerge symlinks exist on target */
    unlink("/target/bin"); symlink("usr/bin", "/target/bin");
    unlink("/target/sbin"); symlink("usr/sbin", "/target/sbin");
    unlink("/target/lib"); symlink("usr/lib", "/target/lib");
    unlink("/target/lib64"); symlink("usr/lib64", "/target/lib64");

    /* Chroot mounts for live package purge and GRUB installation */
    system("mount --bind /dev /target/dev 2>/dev/null || true");
    system("mount --bind /dev/pts /target/dev/pts 2>/dev/null || true");
    system("mount --bind /proc /target/proc 2>/dev/null || true");
    system("mount --bind /sys /target/sys 2>/dev/null || true");
    system("mount --bind /run /target/run 2>/dev/null || true");

    /* Purge live packages and live user in chroot */
    system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /usr/sbin/userdel -r -f wineuser 2>/dev/null || true");
    system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /usr/sbin/groupdel wineuser 2>/dev/null || true");
    system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /bin/sh -c 'dpkg --purge live-boot live-config live-config-systemd live-tools live-boot-doc live-config-doc live-boot-initramfs-tools 2>/dev/null || true'");

    /* The install tests (scripts/vmtest.py) mark their QEMU machines with an fw_cfg entry; only then does the
     * installed system get a serial login, which they use to run programs.  Real machines never have it. */
    int has_serial_console = 0;
    system("modprobe qemu_fw_cfg 2>/dev/null");
    if (access("/sys/firmware/qemu_fw_cfg/by_name/opt/org.wineexplorer/serial-login/raw", R_OK) == 0)
        has_serial_console = 1;

    if (!state->target_disk[0]) {
        if (state->target_part[0]) {
            snprintf(state->target_disk, sizeof(state->target_disk), "%s", state->target_part);
            char *p = state->target_disk + strlen(state->target_disk) - 1;
            while (p > state->target_disk && isdigit((unsigned char)*p)) *p-- = '\0';
            if (p > state->target_disk && *p == 'p' && isdigit((unsigned char)*(p - 1))) *p = '\0';
        } else {
            snprintf(state->target_disk, sizeof(state->target_disk), "/dev/sda");
        }
    }

    /* 7. Start-up files: Vintner Boot on an ISO built with build-iso.sh --vintner (docs/VINTNER_BOOT.md section
     * 4.2), otherwise GRUB. */
    if (access(VINTNER_STEP, X_OK) == 0) {
        ui_draw_status("Setup is installing the start-up files...");
        if (!install_vintner(state)) {
            system("umount /target/run /target/sys /target/proc /target/dev/pts /target/dev 2>/dev/null || true");
            return screen_start_files_failed(state);
        }
    } else {
        install_grub(state);
    }
    system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /bin/systemctl mask plymouth-quit-wait.service plymouth-start.service plymouth.service 2>/dev/null || true");
    if (has_serial_console) {
        system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /bin/systemctl enable serial-getty@ttyS0.service 2>/dev/null || true");
    }

    system("umount /target/run /target/sys /target/proc /target/dev/pts /target/dev 2>/dev/null || true");

    sleep(1);
    return STAGE_REBOOT;
}

/* GRUB, the default loader: its settings, the core image on the disk and grub.cfg. */
static void install_grub(SetupState *state)
{
    safe_mkdir_p("/target/boot/grub");
    safe_mkdir_p("/target/etc/default/grub.d");
    remove("/target/etc/default/grub.d/desktop-base.cfg");

    {
        FILE *f_grub = fopen("/target/etc/default/grub.d/wineexplorer.cfg", "w");
        if (f_grub) {
            fprintf(f_grub,
                    "# Silent instant boot for single-OS WineExplorer installation (F8 for the Advanced Options menu)\n"
                    "GRUB_TIMEOUT_STYLE=hidden\n"
                    "GRUB_TIMEOUT=1\n"
                    "GRUB_TERMINAL=console\n"
                    "GRUB_THEME=\"\"\n"
                    "GRUB_BACKGROUND=\"\"\n"
                    "# The menu names WineExplorer, not Debian (Debian's 10_linux appends \" GNU/Linux\"). "
                    "/etc/os-release stays Debian's.\n"
                    "GRUB_DISTRIBUTOR=\"WineExplorer\"\n");
            fclose(f_grub);
        }
        chmod("/target/etc/grub.d/11_wineexplorer_advanced", 0755);
    }

    FILE *f_map = fopen("/target/boot/grub/device.map", "w");
    if (f_map) {
        fprintf(f_map, "(hd0) %s\n", state->target_disk);
        fclose(f_map);
    }

    /* Activate target partition and set type on MBR if BIOS. Setup prepares only GPT disks now: there GRUB puts its
     * core image in the BIOS boot partition, and these MBR commands must not run. */
    if (!state->is_uefi && !state->target_disk_is_gpt && state->target_part[0]) {
        int part_num = 1;
        char *p = state->target_part + strlen(state->target_part) - 1;
        while (p > state->target_part && isdigit((unsigned char)*(p - 1))) p--;
        if (isdigit((unsigned char)*p)) part_num = atoi(p);
        char act_cmd[256];
        snprintf(act_cmd, sizeof(act_cmd), "sfdisk -A %s %d >/dev/null 2>&1 || true", state->target_disk, part_num);
        system(act_cmd);
        char type_cmd[256];
        snprintf(type_cmd, sizeof(type_cmd), "sfdisk --part-type %s %d 83 >/dev/null 2>&1 || true", state->target_disk, part_num);
        system(type_cmd);
    }

    char grub_cmd[1024];
    if (state->is_uefi) {
        snprintf(grub_cmd, sizeof(grub_cmd),
                 "/usr/sbin/grub-install --target=x86_64-efi --efi-directory=/target/boot/efi --boot-directory=/target/boot --recheck --removable %s >/target/var/log/grub-install.log 2>&1 || chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /usr/sbin/grub-install --target=x86_64-efi --efi-directory=/boot/efi --boot-directory=/boot --recheck --removable %s >>/target/var/log/grub-install.log 2>&1",
                 state->target_disk, state->target_disk);
    } else {
        snprintf(grub_cmd, sizeof(grub_cmd),
                 "/usr/sbin/grub-install --target=i386-pc --boot-directory=/target/boot --recheck --force %s >/target/var/log/grub-install.log 2>&1 || chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /usr/sbin/grub-install --target=i386-pc --recheck --force %s >>/target/var/log/grub-install.log 2>&1",
                 state->target_disk, state->target_disk);
    }
    system(grub_cmd);
    system("chroot /target /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin /usr/sbin/update-grub >>/target/var/log/grub-install.log 2>&1 || true");
}

/* Vintner Boot (install-vintner.sh, on the ISO only with build-iso.sh --vintner). True when the disk now starts
 * through it. */
static bool install_vintner(SetupState *state)
{
    char cmd[1024];
    if (!state->boot_storage_part[0]) return false;
    snprintf(cmd, sizeof(cmd), "%s /target '%s' '%s' '%s' '%s' '%s'", VINTNER_STEP, state->target_disk,
             state->target_part, state->boot_storage_part, state->efi_part, state->bios_boot_part);
    return system(cmd) == 0;
}

/* The start-up files could not be installed (decision D5 in docs/VINTNER_BOOT.md): no fallback to another loader;
 * running Setup again starts over. */
static SetupStage screen_start_files_failed(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");
    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(6, 6, "Setup could not install the start-up files on %s.", state->target_disk);
    mvprintw(8, 6, "Your computer cannot start from this disk. The details are in");
    mvprintw(9, 6, "/var/log/vintner-install.log on the partition Setup installed to.");
    mvprintw(11, 6, "Setup cannot continue. Press F3 to quit.");
    attroff(COLOR_PAIR(PAIR_NORMAL));
    ui_draw_status("F3=Quit");
    while (1) {
        int ch = ui_getch();
        if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') return STAGE_QUIT;
    }
}

SetupStage screen_reboot(SetupState *state)
{
    (void)state;
    ui_clear_screen();
    ui_draw_header("Debian GNU/Linux with WineExplorer Setup");

    int base_y = (LINES > 25) ? (LINES - 25) / 3 + 3 : 5;
    int margin_x = (COLS > 80) ? (COLS - 80) / 4 + 6 : 6;

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(base_y, margin_x, "This portion of Setup has completed successfully.");
    mvprintw(base_y + 2, margin_x, "If there is a CD in the drive, remove it.");
    mvprintw(base_y + 4, margin_x, "To restart your computer, press ENTER.");
    mvprintw(base_y + 5, margin_x, "Your computer will restart in 15 seconds.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    /* Red countdown bar */
    nodelay(stdscr, TRUE);
    for (int sec = 15; sec >= 0; --sec) {
        int pct = ((15 - sec) * 100) / 15;
        ui_draw_centered_progressbar(pct, PAIR_REBOOT);

        char msg[128];
        snprintf(msg, sizeof(msg), "ENTER=Restart Computer   Rebooting in %d seconds...", sec);
        ui_draw_status(msg);

        for (int ms = 0; ms < 10; ++ms) {
            int ch = getch();
            if (ch == 10 || ch == KEY_ENTER) {
                sec = 0;
                break;
            }
            usleep(100000);
        }
    }
    nodelay(stdscr, FALSE);

    sync();
    system("umount -R /target 2>/dev/null || true");
    system("eject /dev/cdrom 2>/dev/null || true");
    system("reboot || systemctl reboot || true");

    return STAGE_QUIT;
}
