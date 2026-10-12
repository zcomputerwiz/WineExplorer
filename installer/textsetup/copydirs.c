/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "copydirs.h"
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool make_parents(char *path)
{
    for (char *p = path + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        int result = mkdir(path, 0755);
        *p = '/';
        if (result < 0 && errno != EEXIST) return false;
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool copy_layout_directories(FILE *layout, const char *source_root, const char *target_root)
{
    char line[1024], section[64], path[256], source[512], target[512];
    bool directories = false;
    rewind(layout);
    while (fgets(line, sizeof(line), layout)) {
        char *p = line;
        while (isspace((unsigned char)*p)) ++p;
        if (*p == ';' || *p == '#' || !*p) continue;
        if (*p == '[') {
            directories = sscanf(p, "[%63[^]]", section) == 1 &&
                          strcasecmp(section, "WinntDirectories") == 0;
            continue;
        }
        if (!directories) continue;
        if (sscanf(p, "%*u = \"%255[^\"]\"", path) != 1 ||
            path[0] != '/' || strstr(path, "/../") ||
            strcmp(path, "/..") == 0 ||
            (strlen(path) >= 3 && strcmp(path + strlen(path) - 3, "/..") == 0))
            return false;
        if (snprintf(source, sizeof(source), "%s%s", source_root, path) >= (int)sizeof(source) ||
            snprintf(target, sizeof(target), "%s%s", target_root, path) >= (int)sizeof(target))
            return false;
        struct stat st;
        if (lstat(source, &st) < 0 || !S_ISDIR(st.st_mode) || !make_parents(target) ||
            chown(target, st.st_uid, st.st_gid) < 0 || chmod(target, st.st_mode & 07777) < 0)
            return false;
    }
    return !ferror(layout);
}
