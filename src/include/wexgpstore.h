/*
 * wexgpstore: the local Group Policy store, for gpsvc (wexpolicy) and the clients that hand it files
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __WEXGPSTORE_H
#define __WEXGPSTORE_H

#include <string.h>
#include <windef.h>

/* docs/GROUP_POLICY.md, "Writing the local files: the spool". The kinds are the fixed list of what may be stored,
 * and the only way a caller names a file. */
#define WEX_GP_MACHINE_POL       0   /* GroupPolicy\Machine\Registry.pol; request Machine.Registry.pol */
#define WEX_GP_USER_POL          1   /* GroupPolicy\User\Registry.pol; request User.Registry.pol */
#define WEX_GP_MACHINE_TEMPLATE  2   /* GroupPolicy\Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf; Machine.GptTmpl.inf */
#define WEX_GP_USER_TARGET_POL   3   /* GroupPolicyUsers/TARGET/User/Registry.pol, TARGET a SID; request UserPolicy.TARGET.Registry.pol */
#define WEX_GP_KIND_COUNT        4

/* The target of WEX_GP_USER_TARGET_POL: the SID of the Administrators (S-1-5-32-544), of the Non-Administrators
 * (S-1-5-32-545) or of a person (S-1-5-21-A-B-C-RID), as ANSI text; every other kind takes NULL. */
#define WEX_GP_TARGET_MAX        64

/* TRUE for a target in the forms above, and for nothing else (the same grammar as the unix side's gps_target_valid: digits
 * and dashes, no leading zero, a subauthority up to 4294967295). A caller checks it before it leaves a request, which
 * gpsvc would otherwise not look at. */
static inline int wex_gp_target_valid( const char *target )
{
    const char *p;
    int i;

    if (!target) return 0;
    if (!strcmp( target, "S-1-5-32-544" ) || !strcmp( target, "S-1-5-32-545" )) return 1;
    if (strncmp( target, "S-1-5-21-", 9 )) return 0;
    p = target + 9;
    for (i = 0; i < 4; i++)
    {
        unsigned long long value = 0;
        int digits = 0;

        if (*p == '0' && p[1] >= '0' && p[1] <= '9') return 0;
        while (*p >= '0' && *p <= '9')
        {
            if (++digits > 10) return 0;
            value = value * 10 + (*p++ - '0');
        }
        if (!digits || value > 0xffffffffULL) return 0;
        if (i < 3 && *p++ != '-') return 0;
    }
    return !*p && (p - target) < WEX_GP_TARGET_MAX;
}

#define WEX_GP_REQUEST_LIMIT     (4 * 1024 * 1024)
#define WEX_GP_TEMPLATE_LIMIT    (1024 * 1024)
#define WEX_GP_ERROR_LIMIT       200

/* gpsvc's control code that asks it to look at the spool (GPSVC_CONTROL_REFRESH is 128) */
#define GPSVC_CONTROL_STORE      129

/* These run as session 0's account (the unix side of the module); in any other process they fail with
 * ERROR_NOT_SUPPORTED or ERROR_INVALID_DATA, because the store is not as setup makes it for them. */

/* the tree is as setup makes it (owner, group, modes, no link), and stale STORE.new files are removed */
DWORD WINAPI WexGpStoreOpen(void);

/* a request of the system group, if there is one: its bytes, validated by the caller afterwards. ERROR_FILE_NOT_FOUND
 * when there is none, ERROR_ACCESS_DENIED for a link, a FIFO or a file with a second link, ERROR_FILE_TOO_LARGE over
 * the limit; *length 0 with ERROR_SUCCESS asks to remove the store file. ERROR_INSUFFICIENT_BUFFER (and *length)
 * when the buffer is too small. */
DWORD WINAPI WexGpSpoolRead( DWORD kind, const char *target, void *buffer, DWORD size, DWORD *length );

/* the targets that have a request in the spool: at most max names of WEX_GP_TARGET_MAX bytes in targets, their number
 * in *count (a request over max waits for the next call) */
DWORD WINAPI WexGpSpoolTargets( char (*targets)[WEX_GP_TARGET_MAX], DWORD max, DWORD *count );

/* the request is handled: it is removed, and NAME.error is written with the text (at most 200 bytes), or an old
 * NAME.error is removed when the text is NULL */
DWORD WINAPI WexGpSpoolDone( DWORD kind, const char *target, const char *error );

/* the store file of a kind, written as STORE.new and renamed into place; size 0 removes it */
DWORD WINAPI WexGpStoreWrite( DWORD kind, const char *target, const void *data, DWORD size );

#endif
