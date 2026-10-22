/*
 * wex-sysrun: the setgid-wexsys launcher for the Wine system role.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer Desktop Environment Project
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
 *
 * Installed root:wexsys, mode 2755. A desktop user runs it as "wex-sysrun <mode>" and the program starts one of
 * a fixed set of programs of the machine root with the effective group wexsys (see
 * docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md, decisions S1, S2). The real uid and the real gid stay the user's, so
 * the kernel keeps the new process non-dumpable and sets AT_SECURE: the user cannot ptrace it, and the dynamic
 * linker ignores LD_*. Nothing from the caller's environment or argument list reaches the new program.
 *
 * Modes:
 *   wineserver      the Wine server, started with the effective group wexsys.
 *   wineboot-init   the machine root's wineboot.exe --init through the Wine loader, so that the services.exe it
 *                   starts inherits wexsys.
 *
 * The paths come from one root-owned file (SYSRUN_CONF, default /etc/wineexplorer/sysrun.conf), lines "key=value":
 * group, loader, ntdll, wineserver, machine_root, and admin_group (read by the Wine server, ignored here).
 * No file descriptor except 0-2 is passed on, so the WINESERVERSOCKET variable is never set: both programs
 * start a fresh connection to their own server.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SYSRUN_CONF
#define SYSRUN_CONF "/etc/wineexplorer/sysrun.conf"
#endif

#define EXIT_REFUSED 77

struct config
{
    char group[64];
    char loader[PATH_MAX];
    char ntdll[PATH_MAX];
    char wineserver[PATH_MAX];
    char machine_root[PATH_MAX];
};

/* The configured system group; until the configuration is read only root's group may have write permission. */
static gid_t sys_gid = (gid_t)-1;

static void refuse(const char *what, const char *path)
{
    fprintf(stderr, "wex-sysrun: refused: %s%s%s\n", what, path ? ": " : "", path ? path : "");
    exit(EXIT_REFUSED);
}

/* A file or directory the launcher trusts: root-owned and not writable by others or by the invoking user. */
static void check_node(const char *path)
{
    struct stat st;

    if (lstat(path, &st)) refuse("cannot stat", path);
    if (st.st_uid != 0) refuse("not owned by root", path);
    if (S_ISLNK(st.st_mode)) return;  /* the target is checked through the resolved path */
    if (st.st_mode & S_IWOTH) refuse("writable by others", path);
    /* access() uses the real uid and the real and supplementary groups, ACLs included, but not wexsys. */
    if (!access(path, W_OK)) refuse("writable by the invoking user", path);
    /* Group write is only fine for root's group and the system group (the machine root is wexsys-writable). */
    if ((st.st_mode & S_IWGRP) && st.st_gid != 0 && st.st_gid != sys_gid)
        refuse("writable by a group other than root and the system group", path);
}

/* Check every component of an absolute path, from / down to the path itself. */
static void check_components(const char *path)
{
    char buf[PATH_MAX];
    const char *p = path;
    size_t len = 0;

    check_node("/");
    while (*p)
    {
        const char *end;

        while (*p == '/') p++;
        if (!*p) break;
        end = strchr(p, '/');
        if (!end) end = p + strlen(p);
        if (len + (end - p) + 2 > sizeof(buf)) refuse("path too long", path);
        buf[len++] = '/';
        memcpy(buf + len, p, end - p);
        len += end - p;
        buf[len] = 0;
        check_node(buf);
        p = end;
    }
}

/* Check a path as written and as resolved: both the symlinks on the way and what they lead to. */
static void check_trusted(const char *path)
{
    char real[PATH_MAX];

    if (path[0] != '/' || strstr(path, "/../") || strstr(path, "/./") || strstr(path, "//"))
        refuse("path is not absolute and canonical", path);
    check_components(path);
    if (!realpath(path, real)) refuse("cannot resolve", path);
    if (strcmp(real, path)) check_components(real);
}

static void set_value(char *dst, size_t size, const char *value, const char *key)
{
    if (strlen(value) >= size) refuse("config value too long", key);
    strcpy(dst, value);
}

static void read_config(struct config *c)
{
    char buf[4096], *line, *next;
    struct stat st;
    ssize_t n;
    int fd;

    strcpy(c->group, "wexsys");
    c->loader[0] = c->ntdll[0] = c->wineserver[0] = c->machine_root[0] = 0;
    check_components(SYSRUN_CONF);
    fd = open(SYSRUN_CONF, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) refuse("cannot open config", SYSRUN_CONF);
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)))
        refuse("config is not a regular root-owned file that only root can write", SYSRUN_CONF);
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) refuse("cannot read config", SYSRUN_CONF);
    buf[n] = 0;
    for (line = buf; line; line = next)
    {
        char *eq;

        if ((next = strchr(line, '\n'))) *next++ = 0;
        if (!*line || *line == '#') continue;
        if (!(eq = strchr(line, '='))) refuse("bad config line", line);
        *eq++ = 0;
        if (!strcmp(line, "group")) set_value(c->group, sizeof(c->group), eq, line);
        else if (!strcmp(line, "loader")) set_value(c->loader, sizeof(c->loader), eq, line);
        else if (!strcmp(line, "ntdll")) set_value(c->ntdll, sizeof(c->ntdll), eq, line);
        else if (!strcmp(line, "wineserver")) set_value(c->wineserver, sizeof(c->wineserver), eq, line);
        else if (!strcmp(line, "machine_root")) set_value(c->machine_root, sizeof(c->machine_root), eq, line);
        else if (!strcmp(line, "admin_group")) continue;
        else refuse("unknown config key", line);
    }
    if (!c->loader[0] || !c->ntdll[0] || !c->wineserver[0] || !c->machine_root[0])
        refuse("config lacks loader, ntdll, wineserver or machine_root", SYSRUN_CONF);
}

/* Fds 0-2 must be open, so that nothing the launcher opens lands on them. */
static void fix_std_fds(void)
{
    int fd;

    for (fd = 0; fd < 3; fd++)
    {
        if (fcntl(fd, F_GETFD) >= 0) continue;
        if (open("/dev/null", O_RDWR) != fd) _exit(EXIT_REFUSED);
    }
}

static void close_other_fds(void)
{
    long fd, max;

    if (!syscall(SYS_close_range, 3U, ~0U, 0U)) return;
    max = sysconf(_SC_OPEN_MAX);  /* shortcut: no close_range (kernel < 5.9), bounded loop; fine for a launcher */
    if (max < 0 || max > 65536) max = 65536;
    for (fd = 3; fd < max; fd++) close(fd);
}

static int safe_value(const char *v, const char *extra)
{
    size_t i;

    if (!v || !*v || strlen(v) > 200) return 0;
    for (i = 0; v[i]; i++)
    {
        char ch = v[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            ch == '_' || ch == '.' || ch == '-' || (extra && strchr(extra, ch)))
            continue;
        return 0;
    }
    return 1;
}

#define MAX_ENV 40

static void add_env(char **env, int *count, const char *key, const char *value)
{
    char *s;

    if (*count >= MAX_ENV - 1 || asprintf(&s, "%s=%s", key, value) < 0) refuse("environment too large", key);
    env[(*count)++] = s;
}

/* The environment of the new program: nothing but what is derived here or taken from a short list, checked. */
static void build_env(char **env, int with_display, const struct passwd *pw)
{
    extern char **environ;
    char **e;
    int count = 0;
    char *prefix;

    add_env(env, &count, "HOME", pw->pw_dir);
    add_env(env, &count, "USER", pw->pw_name);
    add_env(env, &count, "LOGNAME", pw->pw_name);
    add_env(env, &count, "PATH", "/usr/bin:/bin");
    if (asprintf(&prefix, "%s/.wine", pw->pw_dir) < 0) refuse("out of memory", NULL);
    add_env(env, &count, "WINEPREFIX", prefix);
    for (e = environ; e && *e; e++)
    {
        char *eq = strchr(*e, '=');
        size_t klen = eq ? (size_t)(eq - *e) : 0;
        char key[32];

        if (!eq || klen == 0 || klen >= sizeof(key)) continue;
        memcpy(key, *e, klen);
        key[klen] = 0;
        if (!strcmp(key, "LANG") || !strncmp(key, "LC_", 3))
        {
            if (safe_value(eq + 1, "@")) add_env(env, &count, key, eq + 1);
        }
        else if (with_display && (!strcmp(key, "DISPLAY") || !strcmp(key, "WAYLAND_DISPLAY") ||
                                  !strcmp(key, "XDG_RUNTIME_DIR")))
        {
            if (safe_value(eq + 1, ":/")) add_env(env, &count, key, eq + 1);
        }
    }
    env[count] = NULL;
}

int main(int argc, char **argv)
{
    struct config cfg;
    struct passwd *pw;
    struct group *gr;
    gid_t rgid, egid, sgid, groups[NGROUPS_MAX];
    uid_t uid;
    int i, ngroups, wineboot_mode;
    char wineboot[PATH_MAX], services[PATH_MAX], *env[MAX_ENV], *args[4];

    fix_std_fds();
    /* The table of modes. Anything else, and any extra argument, is refused. */
    if (argc != 2) refuse("usage: wex-sysrun wineserver|wineboot-init", NULL);
    if (!strcmp(argv[1], "wineserver")) wineboot_mode = 0;
    else if (!strcmp(argv[1], "wineboot-init")) wineboot_mode = 1;
    else refuse("unknown mode", argv[1]);

    /* A setgid bit that was not applied (nosuid mount, no_new_privs, a copy of the file) must not go on. */
    if (prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 0) refuse("no_new_privs is set, the setgid bit is ignored", NULL);
    uid = getuid();
    if (getresgid(&rgid, &egid, &sgid)) refuse("getresgid failed", NULL);
    read_config(&cfg);
    if (!(gr = getgrnam(cfg.group))) refuse("no such group", cfg.group);
    sys_gid = gr->gr_gid;
    if (egid != gr->gr_gid || sgid != gr->gr_gid) refuse("the effective group is not the system group", cfg.group);
    if (rgid == gr->gr_gid) refuse("the invoking user's primary group is the system group", cfg.group);
    if ((ngroups = getgroups(NGROUPS_MAX, groups)) < 0) refuse("getgroups failed", NULL);
    for (i = 0; i < ngroups; i++)
        if (groups[i] == gr->gr_gid) refuse("the invoking user is a member of the system group", cfg.group);

    if (!(pw = getpwuid(uid)) || pw->pw_dir[0] != '/' || !safe_value(pw->pw_name, NULL) ||
        !safe_value(pw->pw_dir, "/"))
        refuse("cannot use the passwd entry of the invoking user", NULL);

    /* Everything that gets executed or loaded on the way, and the directories above it. */
    if (snprintf(wineboot, sizeof(wineboot), "%s/drive_c/windows/system32/wineboot.exe", cfg.machine_root) >= (int)sizeof(wineboot) ||
        snprintf(services, sizeof(services), "%s/drive_c/windows/system32/services.exe", cfg.machine_root) >= (int)sizeof(services))
        refuse("machine root path too long", cfg.machine_root);
    check_trusted(cfg.loader);
    check_trusted(cfg.ntdll);
    check_trusted(cfg.wineserver);
    check_trusted(wineboot);
    check_trusted(services);

    build_env(env, wineboot_mode, pw);
    /* Keep the real gid: egid != rgid is what keeps the new image non-dumpable and AT_SECURE. */
    if (setresgid(rgid, gr->gr_gid, gr->gr_gid)) refuse("setresgid failed", NULL);
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) refuse("PR_SET_DUMPABLE failed", NULL);
    umask(022);
    if (chdir("/")) refuse("cannot chdir to /", NULL);
    close_other_fds();

    if (wineboot_mode)
    {
        args[0] = "wine";
        args[1] = wineboot;
        args[2] = "--init";
        args[3] = NULL;
        execve(cfg.loader, args, env);
    }
    else
    {
        args[0] = "wineserver";
        args[1] = NULL;
        execve(cfg.wineserver, args, env);
    }
    fprintf(stderr, "wex-sysrun: exec failed: %s\n", strerror(errno));
    return 1;
}
