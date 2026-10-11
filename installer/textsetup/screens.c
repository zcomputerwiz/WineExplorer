#include "textsetup.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>

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

static void get_primary_disk(char *out_dev, size_t out_len, uint64_t *out_mb)
{
    const char *candidates[] = {"sda", "vda", "nvme0n1", "hda", NULL};
    *out_mb = 20480; /* default fallback 20GB */
    snprintf(out_dev, out_len, "/dev/sda");

    for (int i = 0; candidates[i]; ++i) {
        char syspath[128];
        snprintf(syspath, sizeof(syspath), "/sys/block/%s/size", candidates[i]);
        FILE *f = fopen(syspath, "r");
        if (f) {
            uint64_t sectors = 0;
            if (fscanf(f, "%lu", &sectors) == 1 && sectors > 0) {
                *out_mb = (sectors * 512ULL) / (1024ULL * 1024ULL);
                snprintf(out_dev, out_len, "/dev/%s", candidates[i]);
                fclose(f);
                return;
            }
            fclose(f);
        }
    }
}

SetupStage screen_welcome(SetupState *state)
{
    if (state->unattended) {
        return STAGE_LICENSE;
    }

    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(4, 6, "Welcome to Setup.");
    mvprintw(6, 6, "This portion of the Setup program prepares Windows(R) XP to run");
    mvprintw(7, 6, "on your computer.");

    mvprintw(10, 8, "- To set up Windows XP now, press ENTER.");
    mvprintw(12, 8, "- To repair a Windows XP installation using Recovery Console, press R.");
    mvprintw(14, 8, "- To quit Setup without installing Windows XP, press F3.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("ENTER=Continue  R=Repair  F3=Quit");

    while (1) {
        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
            return STAGE_LICENSE;
        } else if (ch == 'r' || ch == 'R') {
            continue;
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
    ui_draw_header("Windows XP Licensing Agreement");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(4, 6, "Please read the following license agreement. Press PAGE DOWN to view");
    mvprintw(5, 6, "the rest of the agreement.");

    /* Read license lines */
    FILE *f = fopen("/setup/eula.txt", "r");
    if (!f) f = fopen("setupinfs/eula.txt", "r");

    char lines[120][80];
    int line_count = 0;
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f) && line_count < 120) {
            buf[strcspn(buf, "\r\n")] = '\0';
            snprintf(lines[line_count++], 80, "%s", buf);
        }
        fclose(f);
    } else {
        snprintf(lines[line_count++], 80, "Debian GNU/Linux with WineExplorer");
        snprintf(lines[line_count++], 80, "Licensed under GNU GPL and Wine LGPL.");
    }

    int top_line = 0;
    int display_lines = 14;

    while (1) {
        for (int i = 0; i < display_lines; ++i) {
            int idx = top_line + i;
            move(7 + i, 6);
            clrtoeol();
            if (idx < line_count) {
                mvprintw(7 + i, 6, "%s", lines[idx]);
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

SetupStage screen_partition(SetupState *state)
{
    uint64_t disk_size_mb = 20480;
    get_primary_disk(state->target_disk, sizeof(state->target_disk), &disk_size_mb);

    if (state->unattended) {
        return STAGE_FORMAT;
    }

    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(4, 6, "The following list shows the existing partitions and unpartitioned space");
    mvprintw(5, 6, "on this computer.");
    mvprintw(7, 6, "Use the UP and DOWN ARROW keys to select an item in the list.");
    mvprintw(8, 8, "- To set up Windows XP on the selected item, press ENTER.");
    mvprintw(9, 8, "- To create a partition in the unpartitioned space, press C.");
    mvprintw(10, 8, "- To delete the selected partition, press D.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(12, 6, "--------------------------------------------------------------------");
    mvprintw(13, 8, "%5lu MB Disk 0 at Id 0 on bus 0 on atapi [%s]",
             (unsigned long)disk_size_mb, state->is_uefi ? "GPT" : "MBR");
    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
    mvprintw(15, 12, "Unpartitioned space                          %5lu MB", (unsigned long)disk_size_mb);
    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
    mvprintw(17, 6, "--------------------------------------------------------------------");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("ENTER=Install  C=Create Partition  D=Delete  F3=Quit");

    while (1) {
        int ch = ui_getch();
        if (ch == 10 || ch == 13 || ch == KEY_ENTER || ch == 'c' || ch == 'C') {
            return STAGE_FORMAT;
        } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
            return STAGE_QUIT;
        }
    }
}

SetupStage screen_format(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    uint64_t disk_size_mb = 20480;
    get_primary_disk(state->target_disk, sizeof(state->target_disk), &disk_size_mb);

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(4, 6, "A new partition for Windows XP has been created on");
    mvprintw(5, 6, "%lu MB Disk 0 at Id 0 on bus 0 on atapi [%s].",
             (unsigned long)disk_size_mb, state->is_uefi ? "GPT" : "MBR");
    mvprintw(7, 6, "The partition must now be formatted.");
    mvprintw(9, 6, "Use the UP and DOWN ARROW keys to select the file system you want,");
    mvprintw(10, 6, "and then press ENTER.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    int selection = 0;
    const char *options[] = {
        "Format the partition using the ext4 file system (Quick)",
        "Format the partition using the ext4 file system",
        "Leave the current file system intact (no changes)"
    };

    if (state->unattended) {
        state->quick_format = true;
    } else {
        while (1) {
            for (int i = 0; i < 3; ++i) {
                if (i == selection) {
                    attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
                    mvprintw(13 + (i * 2), 8, " %-60s ", options[i]);
                    attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_REVERSE);
                } else {
                    attron(COLOR_PAIR(PAIR_NORMAL));
                    mvprintw(13 + (i * 2), 8, " %-60s ", options[i]);
                    attroff(COLOR_PAIR(PAIR_NORMAL));
                }
            }

            ui_draw_status("ENTER=Continue  ESC=Cancel  F3=Quit");

            int ch = ui_getch();
            if (ch == KEY_UP) {
                if (selection > 0) selection--;
            } else if (ch == KEY_DOWN) {
                if (selection < 2) selection++;
            } else if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
                state->quick_format = (selection == 0);
                break;
            } else if (ch == 27) {
                return STAGE_PARTITION;
            } else if (ch == KEY_F(3) || ch == 'q' || ch == 'Q') {
                return STAGE_QUIT;
            }
        }
    }

    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(7, 6, "Please wait while Setup formats the partition.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("Formatting drive...");

    /* Partition disk layout */
    char part_dev[64];
    char efi_dev[64];
    bool is_nvme = (strstr(state->target_disk, "nvme") != NULL);

    char sfdisk_cmd[512];
    if (state->is_uefi) {
        if (is_nvme) {
            snprintf(efi_dev, sizeof(efi_dev), "%sp1", state->target_disk);
            snprintf(part_dev, sizeof(part_dev), "%sp2", state->target_disk);
        } else {
            snprintf(efi_dev, sizeof(efi_dev), "%s1", state->target_disk);
            snprintf(part_dev, sizeof(part_dev), "%s2", state->target_disk);
        }
        snprintf(state->efi_part, sizeof(state->efi_part), "%s", efi_dev);
        snprintf(state->target_part, sizeof(state->target_part), "%s", part_dev);

        snprintf(sfdisk_cmd, sizeof(sfdisk_cmd),
                 "printf 'label: gpt\\nsize=512M, type=U\\n,, type=L\\n' | sfdisk --wipe always %s >/dev/null 2>&1",
                 state->target_disk);
    } else {
        if (is_nvme) {
            snprintf(part_dev, sizeof(part_dev), "%sp1", state->target_disk);
        } else {
            snprintf(part_dev, sizeof(part_dev), "%s1", state->target_disk);
        }
        snprintf(state->target_part, sizeof(state->target_part), "%s", part_dev);
        state->efi_part[0] = '\0';

        snprintf(sfdisk_cmd, sizeof(sfdisk_cmd),
                 "printf 'label: dos\\n,,83,*\\n' | sfdisk --wipe always %s >/dev/null 2>&1; sfdisk -A %s 1 >/dev/null 2>&1",
                 state->target_disk, state->target_disk);
    }

    system(sfdisk_cmd);
    system("udevadm settle >/dev/null 2>&1 || sleep 1");

    for (int p = 0; p <= 50; p += 10) {
        ui_draw_progressbar(12, 10, 60, p, PAIR_PROGRESS);
        usleep(80000);
    }

    /* Format filesystems */
    if (state->is_uefi && state->efi_part[0]) {
        char fmt_efi[256];
        snprintf(fmt_efi, sizeof(fmt_efi), "mkfs.vfat -F32 %s >/dev/null 2>&1", state->efi_part);
        system(fmt_efi);
    }

    char fmt_root[256];
    snprintf(fmt_root, sizeof(fmt_root), "mkfs.ext4 -F -q %s >/dev/null 2>&1", state->target_part);
    system(fmt_root);

    for (int p = 50; p <= 100; p += 10) {
        ui_draw_progressbar(12, 10, 60, p, PAIR_PROGRESS);
        usleep(80000);
    }

    return STAGE_COPY;
}

SetupStage screen_copy(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(6, 6, "Please wait while Setup copies files to the installation folder.");
    mvprintw(7, 6, "This might take several minutes to complete.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    mkdir("/target", 0755);
    char mnt_root[256];
    snprintf(mnt_root, sizeof(mnt_root), "mount %s /target >/dev/null 2>&1", state->target_part);
    system(mnt_root);

    if (state->is_uefi && state->efi_part[0]) {
        safe_mkdir_p("/target/boot/efi");
        char mnt_efi[256];
        snprintf(mnt_efi, sizeof(mnt_efi), "mount %s /target/boot/efi >/dev/null 2>&1", state->efi_part);
        system(mnt_efi);
    }

    ui_draw_progressbar(12, 10, 60, 0, PAIR_PROGRESS);

    const char *dirs[] = {
        "bin", "boot", "etc", "lib", "lib64", "opt", "root", "sbin", "usr", "var", NULL
    };
    int weights[] = {
        5,    10,     18,    28,    32,      36,    40,     45,     85,    98
    };

    for (int i = 0; dirs[i]; ++i) {
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "Copying: /%s/...", dirs[i]);
        ui_draw_status(status_msg);
        ui_draw_progressbar(12, 10, 60, weights[i], PAIR_PROGRESS);
        char cp_cmd[256];
        snprintf(cp_cmd, sizeof(cp_cmd), "cp -a /%s /target/ 2>/dev/null || true", dirs[i]);
        system(cp_cmd);
    }

    ui_draw_status("Copying files... (100%)");
    ui_draw_progressbar(12, 10, 60, 100, PAIR_PROGRESS);

    mkdir("/target/proc", 0755);
    mkdir("/target/sys", 0755);
    mkdir("/target/dev", 0755);
    mkdir("/target/run", 0755);
    mkdir("/target/tmp", 01777);
    mkdir("/target/home", 0755);

    system("chmod 755 /target /target/etc /target/usr /target/var /target/home 2>/dev/null || true");
    system("chmod 4755 /target/usr/bin/sudo /target/usr/bin/su /target/usr/bin/pkexec /target/usr/bin/passwd 2>/dev/null || true");
    system("chmod 4755 /target/usr/lib/dbus-1/dbus-daemon-launch-helper /target/usr/lib/polkit-1/polkit-agent-helper-1 2>/dev/null || true");

    return STAGE_INIT;
}

SetupStage screen_init(SetupState *state)
{
    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(8, 6, "Please wait while Setup initializes your Windows XP configuration.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    ui_draw_status("Saving configuration...");

    /* 1. Generate /etc/fstab */
    char root_uuid[128] = "";
    char blkid_cmd[256];
    snprintf(blkid_cmd, sizeof(blkid_cmd), "blkid -s UUID -o value %s 2>/dev/null", state->target_part);
    FILE *fp = popen(blkid_cmd, "r");
    if (fp) {
        if (fgets(root_uuid, sizeof(root_uuid), fp)) {
            root_uuid[strcspn(root_uuid, "\r\n")] = '\0';
        }
        pclose(fp);
    }

    char fstab_path[256] = "/target/etc/fstab";
    FILE *f_fstab = fopen(fstab_path, "w");
    if (f_fstab) {
        fprintf(f_fstab, "# /etc/fstab: static file system information.\n");
        if (root_uuid[0]) {
            fprintf(f_fstab, "UUID=%s  /  ext4  errors=remount-ro  0  1\n", root_uuid);
        } else {
            fprintf(f_fstab, "%s  /  ext4  errors=remount-ro  0  1\n", state->target_part);
        }

        if (state->is_uefi && state->efi_part[0]) {
            char efi_uuid[128] = "";
            snprintf(blkid_cmd, sizeof(blkid_cmd), "blkid -s UUID -o value %s 2>/dev/null", state->efi_part);
            fp = popen(blkid_cmd, "r");
            if (fp) {
                if (fgets(efi_uuid, sizeof(efi_uuid), fp)) {
                    efi_uuid[strcspn(efi_uuid, "\r\n")] = '\0';
                }
                pclose(fp);
            }
            if (efi_uuid[0]) {
                fprintf(f_fstab, "UUID=%s  /boot/efi  vfat  umask=0077  0  1\n", efi_uuid);
            } else {
                fprintf(f_fstab, "%s  /boot/efi  vfat  umask=0077  0  1\n", state->efi_part);
            }
        }

        fprintf(f_fstab, "tmpfs  /tmp  tmpfs  defaults,nosuid,nodev  0  0\n");
        fclose(f_fstab);
    }

    /* 2. Setup flags */
    safe_mkdir_p("/target/etc/wineexplorer/setup");
    FILE *f_stage = fopen("/target/etc/wineexplorer/setup/stage", "w");
    if (f_stage) {
        fprintf(f_stage, "gui\n");
        fclose(f_stage);
    }

    /* Copy answer file to target for Stage 2 GUI Setup and Stage 3 OOBE */
    if (state->unattended && state->unattend_path[0]) {
        char cp_unattend[512];
        snprintf(cp_unattend, sizeof(cp_unattend), "cp -f '%s' /target/etc/wineexplorer/setup/unattend.sif 2>/dev/null || true", state->unattend_path);
        system(cp_unattend);
    }

    /* 3. New machine-id */
    system("systemd-machine-id-setup --root=/target >/dev/null 2>&1 || true");
    system("mkdir -p /target/var/lib/dbus 2>/dev/null || true");
    system("ln -sf /etc/machine-id /target/var/lib/dbus/machine-id 2>/dev/null || true");

    /* 4. Install GRUB */
    safe_mkdir_p("/target/boot/grub");

    system("mount --bind /dev /target/dev 2>/dev/null || true");
    system("mount --bind /dev/pts /target/dev/pts 2>/dev/null || true");
    system("mount --bind /proc /target/proc 2>/dev/null || true");
    system("mount --bind /sys /target/sys 2>/dev/null || true");
    system("mount --bind /run /target/run 2>/dev/null || true");

    char grub_cmd[512];
    if (state->is_uefi) {
        snprintf(grub_cmd, sizeof(grub_cmd),
                 "grub-install --target=x86_64-efi --efi-directory=/target/boot/efi --boot-directory=/target/boot --removable %s >/target/var/log/grub-install.log 2>&1 || chroot /target grub-install --target=x86_64-efi --efi-directory=/boot/efi --boot-directory=/boot --removable %s >>/target/var/log/grub-install.log 2>&1",
                 state->target_disk, state->target_disk);
    } else {
        snprintf(grub_cmd, sizeof(grub_cmd),
                 "grub-install --target=i386-pc --boot-directory=/target/boot --force %s >/target/var/log/grub-install.log 2>&1 || chroot /target grub-install --target=i386-pc --force %s >>/target/var/log/grub-install.log 2>&1",
                 state->target_disk, state->target_disk);
    }
    system(grub_cmd);
    system("chroot /target update-grub >>/target/var/log/grub-install.log 2>&1 || true");
    system("chroot /target systemctl mask plymouth-quit-wait.service plymouth-start.service plymouth.service 2>/dev/null || true");
    system("chmod 440 /target/etc/sudoers.d/* 2>/dev/null || true");
    system("chmod -R 777 /target/etc/wineexplorer 2>/dev/null || true");

    system("umount /target/run /target/sys /target/proc /target/dev/pts /target/dev 2>/dev/null || true");

    /* 5. Unattended file copy if applicable */
    if (state->unattended && state->unattend_path[0]) {
        char cp_unattend[512];
        snprintf(cp_unattend, sizeof(cp_unattend), "cp %s /target/etc/wineexplorer/setup/unattend.sif 2>/dev/null || true",
                 state->unattend_path);
        system(cp_unattend);
    }

    sleep(1);
    return STAGE_REBOOT;
}

SetupStage screen_reboot(SetupState *state)
{
    (void)state;
    ui_clear_screen();
    ui_draw_header("Windows XP Professional Setup");

    attron(COLOR_PAIR(PAIR_NORMAL));
    mvprintw(6, 6, "This portion of Setup has completed successfully.");
    mvprintw(8, 6, "If there is a CD in drive A:, remove it.");
    mvprintw(10, 6, "To restart your computer, press ENTER.");
    mvprintw(11, 6, "Your computer will restart in 15 seconds.");
    attroff(COLOR_PAIR(PAIR_NORMAL));

    /* Red reboot countdown bar */
    nodelay(stdscr, TRUE);
    for (int sec = 15; sec >= 0; --sec) {
        int pct = ((15 - sec) * 100) / 15;
        ui_draw_progressbar(14, 10, 60, pct, PAIR_REBOOT);

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

    /* Unmount target, eject CD, and reboot */
    sync();
    system("umount -R /target 2>/dev/null || true");
    system("eject /dev/cdrom 2>/dev/null || true");
    system("reboot || systemctl reboot || true");

    return STAGE_QUIT;
}
