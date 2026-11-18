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
 *   safe-shell W H  in Safe Mode, for an administrator only: the machine root's explorer.exe as the desktop and shell
 *                   (/desktop=Default,WxH /shell) of the user's own prefix, with wexsys, so that the administrator's
 *                   desktop runs elevated, as on Windows where UAC is off in Safe Mode
 *                   (docs/SAFE_MODE_ELEVATED_DESKTOP.md). The server gives it the elevated desktop only for an
 *                   administrator's full token in Safe Mode, and ends it otherwise.
 *
 * The paths come from one root-owned file (SYSRUN_CONF, default /etc/wineexplorer/sysrun.conf), lines "key=value":
 * group, loader, ntdll, wineserver, machine_root, and admin_group (read by the Wine server, ignored here). An
 * optional winedebug= is passed on as WINEDEBUG (tracing in a lab; only root can set it).
 * wineserver mode starts a fresh server for the user's own prefix (<passwd home>/.wine). The server drops its
 * effective group to the user's real group at once and keeps the system group as its saved group; it raises it
 * only to load and save the machine hives and for clients that have the system group themselves.
 * wineboot-init mode never reads the user's prefix: the launcher connects to the user's server itself (the path
 * is computed from the user's own prefix with stat, not from the environment), checks that the server is a
 * launcher-started one, and passes the connected socket as WINESERVERSOCKET (descriptor 3). WINEPREFIX is then
 * the machine root, whose dosdevices and drive_c are root-owned.
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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../wine/include/wine/wex_sysrun_conf.h"

#ifndef SYSRUN_CONF
#define SYSRUN_CONF "/etc/wineexplorer/sysrun.conf"
#endif
/* where wex-is-admin is installed, beside this launcher */
#ifndef SYSRUN_LIBEXEC
#define SYSRUN_LIBEXEC "/usr/local/libexec/wineexplorer"
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

static void read_config(struct config *c)
{
    struct wex_sysrun_conf conf;
    char *line, *end, *eq;
    const char *keys[] = { "group", "loader", "ntdll", "wineserver", "machine_root", "admin_group",
                           "builtin_administrator", "session0_socket", "session0_user", "winedebug" };
    size_t i;

    /* the path's directories first, for the administrator's diagnostic (which one is writable by whom); the shared
     * reader then checks the same again on descriptors and is the one that decides */
    check_components(SYSRUN_CONF);
    if (!wex_sysrun_conf_load(SYSRUN_CONF, &conf)) refuse("invalid or unprotected config", SYSRUN_CONF);
    /* The launcher has always refused unknown keys. Other readers may ignore them. */
    for (line = conf.text; *line; line = end + 1)
    {
        end = strchr(line, '\n');
        if (line == end || *line == '#') continue;
        eq = strchr(line, '=');
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
            if (strlen(keys[i]) == (size_t)(eq - line) && !strncmp(line, keys[i], eq - line)) break;
        if (i == sizeof(keys) / sizeof(keys[0])) refuse("unknown config key", SYSRUN_CONF);
    }
    if (wex_sysrun_conf_get(&conf, "group", c->group, sizeof(c->group)) == WEX_SYSRUN_CONF_INVALID)
        refuse("invalid config group", SYSRUN_CONF);
    if (!c->group[0]) strcpy(c->group, "wexsys");
    if (wex_sysrun_conf_get(&conf, "loader", c->loader, sizeof(c->loader)) != WEX_SYSRUN_CONF_VALUE ||
        wex_sysrun_conf_get(&conf, "ntdll", c->ntdll, sizeof(c->ntdll)) != WEX_SYSRUN_CONF_VALUE ||
        wex_sysrun_conf_get(&conf, "wineserver", c->wineserver, sizeof(c->wineserver)) != WEX_SYSRUN_CONF_VALUE ||
        wex_sysrun_conf_get(&conf, "machine_root", c->machine_root, sizeof(c->machine_root)) != WEX_SYSRUN_CONF_VALUE)
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

/* The account name of the invoking user: a plain safe name, or "DOMAIN\user" of a domain account (Winbind with
 * winbind use default domain = no, docs/DOMAIN_LOGON.md): exactly one backslash, a safe flat name of 1 to 15
 * characters before it and a safe user name of 1 to 63 after it. Nothing else is let through. */
static int safe_account_name(const char *v)
{
    const char *bs;
    char flat[16];

    if (!v || !(bs = strchr(v, '\\'))) return safe_value(v, NULL);
    if (bs == v || bs - v > 15 || strchr(bs + 1, '\\') || strlen(bs + 1) > 63) return 0;
    memcpy(flat, v, bs - v);
    flat[bs - v] = 0;
    return safe_value(flat, NULL) && safe_value(bs + 1, NULL);
}

/* Is the process at the other end of the socket a server that this launcher started? The kernel gives the pid
 * and uid at listen time; the process must have the user's own ids and the system group as its saved group (its
 * effective group is the user's, or the system group while it is raised). Only a program run through a setgid
 * program of the system group has that saved group, and the only ones that this launcher runs are the
 * configured server and the loader. /proc/<pid>/exe is not readable for a non-dumpable process, so the image
 * cannot be compared. */
static int server_ok(int fd, uid_t uid, gid_t rgid, gid_t sys_gid)
{
    unsigned int ruid, euid, suid, fuid, r, e, sg, f;
    int got_uid = 0, got_gid = 0, sfd;
    struct ucred cred;
    socklen_t len = sizeof(cred);
    char path[64], buf[8192], *p;
    ssize_t n;

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) || cred.pid <= 0 || cred.uid != uid) return 0;
    snprintf(path, sizeof(path), "/proc/%d/status", (int)cred.pid);
    if ((sfd = open(path, O_RDONLY | O_CLOEXEC)) < 0) return 0;
    n = read(sfd, buf, sizeof(buf) - 1);
    close(sfd);
    if (n <= 0) return 0;
    buf[n] = 0;
    for (p = buf; p; p = strchr(p, '\n'))
    {
        if (*p == '\n') p++;
        if (!strncmp(p, "Uid:", 4)) got_uid = sscanf(p + 4, "%u %u %u %u", &ruid, &euid, &suid, &fuid) == 4;
        else if (!strncmp(p, "Gid:", 4)) got_gid = sscanf(p + 4, "%u %u %u %u", &r, &e, &sg, &f) == 4;
    }
    return got_uid && got_gid && ruid == uid && euid == uid && suid == uid && fuid == uid &&
           r == rgid && sg == sys_gid && (e == rgid || e == sys_gid);
}

/* Connect to the socket of the server of the user's own prefix and check the server. The directory name is what
 * Wine computes: /tmp/.wine-<uid>/server-<device>-<inode> of the prefix. */
static int connect_server(const struct passwd *pw, uid_t uid, gid_t rgid, gid_t sys_gid)
{
    char prefix[PATH_MAX], dir[PATH_MAX], path[PATH_MAX];
    struct sockaddr_un addr;
    struct stat st;
    int fd;

    if (snprintf(prefix, sizeof(prefix), "%s/.wine", pw->pw_dir) >= (int)sizeof(prefix)) refuse("prefix path too long", NULL);
    if (stat(prefix, &st) || st.st_uid != uid) refuse("the prefix does not exist or is not the user's", prefix);
    snprintf(dir, sizeof(dir), "/tmp/.wine-%u", (unsigned int)uid);
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != uid || (st.st_mode & 077))
        refuse("the server directory is not the user's private directory", dir);
    if (stat(prefix, &st)) refuse("cannot stat", prefix);
    if (snprintf(dir, sizeof(dir), "/tmp/.wine-%u/server-%llx-%llx", (unsigned int)uid,
                 (unsigned long long)st.st_dev, (unsigned long long)st.st_ino) >= (int)sizeof(dir) ||
        snprintf(path, sizeof(path), "%s/socket", dir) >= (int)sizeof(addr.sun_path))
        refuse("server socket path too long", dir);
    if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != uid || (st.st_mode & 077))
        refuse("the server directory is not the user's private directory", dir);
    if (lstat(path, &st) || !S_ISSOCK(st.st_mode) || st.st_uid != uid) refuse("no server socket of the user", path);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) refuse("socket failed", NULL);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) refuse("cannot connect to the server", path);
    if (!server_ok(fd, uid, rgid, sys_gid)) refuse("the server was not started by this launcher", path);
    return fd;
}

/* Safe Mode (docs/SAFE_MODE.md): /run/wineexplorer/bootmode, a regular file that root owns and others cannot write,
 * in a root-owned directory that others cannot write, says safe or safenet (safecmd has no desktop). The same
 * source of truth as the server's and the broker's. Returns the mode (the desktop's WEXMODE), or NULL. */
static const char *safe_mode_desktop(void)
{
    struct stat st;
    char buf[16];
    ssize_t n;
    int fd;

    if (lstat("/run/wineexplorer", &st) || !S_ISDIR(st.st_mode) || st.st_uid || (st.st_mode & 022)) return NULL;
    if ((fd = open("/run/wineexplorer/bootmode", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)) < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 022) || (n = read(fd, buf, sizeof(buf) - 1)) <= 0)
    {
        close(fd);
        return NULL;
    }
    close(fd);
    buf[n] = 0;
    buf[strcspn(buf, "\r\n")] = 0;
    if (!strcmp(buf, "safe")) return "safe";
    if (!strcmp(buf, "safenet")) return "safenet";
    return NULL;
}

/* Is the invoking user an administrator? The shared rule of the server (wex-is-admin, which reads the same protected
 * configuration with the same parser), with nothing of the caller's environment. The helper keeps the system group
 * as its effective and saved group (it only reads): so it stays non-dumpable and AT_SECURE, and the caller, whose
 * ids it shares, cannot trace it or write its memory to change the answer. Anything but a plain exit 0 is a no. */
static int caller_is_administrator(const char *helper, const struct passwd *pw, gid_t rgid, gid_t sys)
{
    char *args[3] = { "wex-is-admin", pw->pw_name, NULL };
    char *env[] = { "PATH=/usr/bin:/bin", NULL };
    int status;
    pid_t pid;

    if ((pid = fork()) < 0) return 0;
    if (!pid)
    {
        if (setresgid(rgid, sys, sys) || prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) _exit(EXIT_REFUSED);
        execve(helper, args, env);
        _exit(EXIT_REFUSED);
    }
    if (waitpid(pid, &status, 0) != pid) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* WIDTH or HEIGHT of the Safe Mode desktop: a number of pixels in a sane range, nothing else */
static unsigned int desktop_size(const char *v)
{
    unsigned long n;
    char *end;

    if (!v || !*v || strlen(v) > 5 || strspn(v, "0123456789") != strlen(v)) refuse("invalid desktop size", v);
    n = strtoul(v, &end, 10);
    if (*end || n < 320 || n > 16384) refuse("invalid desktop size", v);
    return (unsigned int)n;
}

#define MAX_ENV 40

static void add_env(char **env, int *count, const char *key, const char *value)
{
    char *s;

    if (*count >= MAX_ENV - 1 || asprintf(&s, "%s=%s", key, value) < 0) refuse("environment too large", key);
    env[(*count)++] = s;
}

/* The environment of the new program: nothing but what is derived here or taken from a short list, checked. */
static void build_env(char **env, int with_display, const struct passwd *pw, const char *prefix, int server_fd,
                      const char *mode, const char *boot_mode)
{
    extern char **environ;
    char **e;
    int count = 0;
    char fdstr[16];

    /* which mode started the program: ntdll skips the session's wineboot only in the wineboot-init it is */
    add_env(env, &count, "WEXSYSRUN_MODE", mode);
    /* the Safe Mode desktop: the verified boot mode, which Explorer and the shell read as the session's WEXMODE */
    if (boot_mode) add_env(env, &count, "WEXMODE", boot_mode);
    add_env(env, &count, "HOME", pw->pw_dir);
    add_env(env, &count, "USER", pw->pw_name);
    add_env(env, &count, "LOGNAME", pw->pw_name);
    add_env(env, &count, "PATH", "/usr/bin:/bin");
    add_env(env, &count, "WINEPREFIX", prefix);
    if (server_fd >= 0)
    {
        snprintf(fdstr, sizeof(fdstr), "%d", server_fd);
        add_env(env, &count, "WINESERVERSOCKET", fdstr);
    }
    /* Tracing for the lab: "winedebug=" of the root-owned configuration becomes WINEDEBUG of the system role's
     * programs (and of the elevated programs the broker among them starts). Never the caller's WINEDEBUG. */
    {
        struct wex_sysrun_conf conf;
        char debug[256];

        if (wex_sysrun_conf_load(SYSRUN_CONF, &conf) &&
            wex_sysrun_conf_get(&conf, "winedebug", debug, sizeof(debug)) == WEX_SYSRUN_CONF_VALUE &&
            debug[strspn(debug, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+-,:.")] == 0)
            add_env(env, &count, "WINEDEBUG", debug);
    }
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
        else if (with_display == 2 && !strcmp(key, "WEX_SHELL_SOCKET"))
        {
            /* the compositor's private socket: a name in XDG_RUNTIME_DIR, never a path */
            if (safe_value(eq + 1, "-") && !strchr(eq + 1, '/')) add_env(env, &count, key, eq + 1);
        }
    }
    if (with_display == 2) add_env(env, &count, "WEX_WAYLAND_NATIVE_WINDOWS", "1");
    env[count] = NULL;
}

int main(int argc, char **argv)
{
    struct config cfg;
    struct passwd *pw;
    struct group *gr;
    gid_t rgid, egid, sgid, groups[NGROUPS_MAX];
    uid_t uid;
    int i, ngroups, wineboot_mode = 0, shell_mode = 0, server_fd = -1;
    unsigned int width = 0, height = 0;
    char wineboot[PATH_MAX], services[PATH_MAX], explorer[PATH_MAX], is_admin[PATH_MAX], user_prefix[PATH_MAX];
    char desktop[64], *env[MAX_ENV], *args[5];
    const char *boot_mode = NULL;

    fix_std_fds();
    /* The table of modes. Anything else, and any extra argument, is refused. */
    if (argc == 2 && !strcmp(argv[1], "wineserver")) ;
    else if (argc == 2 && !strcmp(argv[1], "wineboot-init")) wineboot_mode = 1;
    else if (argc == 4 && !strcmp(argv[1], "safe-shell"))
    {
        /* an administrator's elevated desktop in Safe Mode (docs/SAFE_MODE_ELEVATED_DESKTOP.md) */
        shell_mode = 1;
        width = desktop_size(argv[2]);
        height = desktop_size(argv[3]);
    }
    else if (argc == 2 && !strcmp(argv[1], "safe-shell-wayland"))
        shell_mode = 2;  /* the same under the Wayland compositor: Explorer's shell on its private socket */
    else refuse("usage: wex-sysrun wineserver|wineboot-init|safe-shell WIDTH HEIGHT|safe-shell-wayland", NULL);

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

    if (!(pw = getpwuid(uid)) || pw->pw_dir[0] != '/' || !safe_account_name(pw->pw_name) ||
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

    if (snprintf(user_prefix, sizeof(user_prefix), "%s/.wine", pw->pw_dir) >= (int)sizeof(user_prefix))
        refuse("home directory path too long", pw->pw_dir);
    if (shell_mode)
    {
        /* Safe Mode and an administrator, both checked here; the server checks both again (it gives a launcher-started
         * explorer.exe the elevated desktop only for an administrator's full token in Safe Mode, else ends it) */
        if (snprintf(explorer, sizeof(explorer), "%s/drive_c/windows/explorer.exe", cfg.machine_root) >= (int)sizeof(explorer) ||
            snprintf(is_admin, sizeof(is_admin), "%s/wex-is-admin", SYSRUN_LIBEXEC) >= (int)sizeof(is_admin))
            refuse("path too long", NULL);
        check_trusted(explorer);
        check_trusted(is_admin);
        if (!(boot_mode = safe_mode_desktop())) refuse("not a Safe Mode start with a desktop", NULL);
        if (!caller_is_administrator(is_admin, pw, rgid, gr->gr_gid))
            refuse("the invoking user is not an administrator", pw->pw_name);
        if (shell_mode == 1) snprintf(desktop, sizeof(desktop), "/desktop=Default,%ux%u", width, height);
    }
    /* Keep the real gid: egid != rgid is what keeps the new image non-dumpable and AT_SECURE. */
    if (setresgid(rgid, gr->gr_gid, gr->gr_gid)) refuse("setresgid failed", NULL);
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) refuse("PR_SET_DUMPABLE failed", NULL);
    umask(022);
    if (chdir("/")) refuse("cannot chdir to /", NULL);
    close_other_fds();

    if (wineboot_mode)
    {
        /* the new process is a client of the user's server and never reads the user's prefix */
        server_fd = connect_server(pw, uid, rgid, gr->gr_gid);
        if (server_fd != 3 && (dup2(server_fd, 3) != 3 || close(server_fd))) refuse("cannot place the server socket", NULL);
        server_fd = 3;
    }
    build_env(env, shell_mode == 2 ? 2 : wineboot_mode || shell_mode, pw, wineboot_mode ? cfg.machine_root : user_prefix, server_fd,
              wineboot_mode ? "wineboot-init" : shell_mode ? "safe-shell" : "wineserver", boot_mode);

    if (wineboot_mode)
    {
        args[0] = "wine";
        args[1] = wineboot;
        args[2] = "--init";
        args[3] = NULL;
        execve(cfg.loader, args, env);
    }
    else if (shell_mode)
    {
        /* the user's own prefix and server: explorer.exe of the machine root (through the prefix's link) runs the
         * desktop and the shell, as the session does outside Safe Mode */
        args[0] = "wine";
        args[1] = explorer;
        if (shell_mode == 1)
        {
            args[2] = desktop;
            args[3] = "/shell";
            args[4] = NULL;
        }
        else
        {
            args[2] = "/shell";  /* Wayland: the compositor is the desktop */
            args[3] = NULL;
        }
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
