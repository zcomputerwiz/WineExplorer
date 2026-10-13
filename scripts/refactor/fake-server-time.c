/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Test instrumentation loaded into the isolated wineserver only.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

time_t time(time_t *result)
{
    const char *path = getenv("WEX_SERVER_CLOCK");
    struct timespec now;
    char buffer[32], *end;
    time_t value;
    ssize_t count;
    int fd;

    clock_gettime(CLOCK_REALTIME, &now);
    value = now.tv_sec;
    if (path && (fd = open(path, O_RDONLY)) != -1)
    {
        count = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (count > 0)
        {
            buffer[count] = 0;
            value = strtoll(buffer, &end, 10);
            if (end == buffer) value = now.tv_sec;
        }
    }
    if (result) *result = value;
    return value;
}
