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
#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
    uid_t r, e, s;
    gid_t rg, eg, sg;
    DIR *dir;
    struct dirent *de;
    char *p;
    char buf[8];
    int i, self;

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
            if (de->d_name[0] != '.' && i != self) printf("FD %d\n", i);
        }
        closedir(dir);
    }
    printf("READY\n");
    fflush(stdout);
    if (read(0, buf, sizeof(buf)) < 0) return 1;  /* waits while the test keeps the pipe open */
    return 0;
}
