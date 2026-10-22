/*
 * Test helper for scripts/test_wex_sysrun.sh: stands in for the Wine loader and the wineserver, prints what the
 * launcher passed on, and (when stdin is a pipe that stays open) waits so that the test can try to ptrace it.
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
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static int serve_fd = -1;
static char serve_path[PATH_MAX];

static void remove_socket(void)
{
    if (serve_path[0]) unlink(serve_path);
}

/* As the server does: listening in /tmp/.wine-<uid>/server-<dev>-<ino>/socket of $WINEPREFIX, with the effective
 * group already the real one (the saved group stays). Used when the program is named wineserver. */
static void serve(const char *argv0)
{
    const char *prefix = getenv("WINEPREFIX");
    char dir[PATH_MAX];
    struct sockaddr_un addr;
    struct stat st;

    if (!prefix || strcmp(strrchr(argv0, '/') ? strrchr(argv0, '/') + 1 : argv0, "wineserver") || stat(prefix, &st)) return;
    if (setegid(getgid())) return;
    snprintf(dir, sizeof(dir), "/tmp/.wine-%u", (unsigned int)getuid());
    mkdir(dir, 0700);
    snprintf(dir, sizeof(dir), "/tmp/.wine-%u/server-%llx-%llx", (unsigned int)getuid(),
             (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
    mkdir(dir, 0700);
    snprintf(serve_path, sizeof(serve_path), "%s/socket", dir);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, serve_path);
    serve_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serve_fd < 0 || bind(serve_fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(serve_fd, 5))
    {
        serve_path[0] = 0;
        return;
    }
    chmod(serve_path, 0600);
    atexit(remove_socket);
    printf("SERVING %s\n", serve_path);
}

int main(int argc, char **argv, char **envp)
{
    uid_t r, e, s;
    gid_t rg, eg, sg;
    DIR *dir;
    struct dirent *de;
    char *p;
    char buf[8];
    int i, self;

    serve(argv[0]);
    for (i = 0; i < argc; i++) printf("ARG %d %s\n", i, argv[i]);
    for (; envp && *envp; envp++) printf("ENV %s\n", *envp);
    getresuid(&r, &e, &s);
    getresgid(&rg, &eg, &sg);
    printf("UID %u %u %u\nGID %u %u %u\n", r, e, s, rg, eg, sg);
    printf("DUMPABLE %d\nAT_SECURE %lu\n", prctl(PR_GET_DUMPABLE), getauxval(AT_SECURE));
    p = getcwd(NULL, 0);
    printf("CWD %s\n", p ? p : "?");
    printf("PID %d\n", getpid());
    if ((dir = opendir("/proc/self/fd")))
    {
        self = dirfd(dir);
        while ((de = readdir(dir)))
        {
            i = atoi(de->d_name);
            if (de->d_name[0] != '.' && i != self && i != serve_fd) printf("FD %d\n", i);
        }
        closedir(dir);
    }
    printf("READY\n");
    fflush(stdout);
    if (read(0, buf, sizeof(buf)) < 0) return 1;  /* waits while the test keeps the pipe open */
    return 0;
}
