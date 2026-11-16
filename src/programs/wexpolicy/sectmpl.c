/*
 * wexpolicy: security templates (GptTmpl.inf), the parts that apply to WineExplorer
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

/*
 * docs/GROUP_POLICY.md, "Step 5: the security template". A GPO's security settings are in
 * Machine\Microsoft\Windows NT\SecEdit\GptTmpl.inf, an INF file (UTF-16LE), read here with setupapi:
 *
 *   [System Access]    LockoutBadCount, LockoutDuration, ResetLockoutCount: the account lockout policy, written
 *                      where NetUserModalsSet level 3 and the elevation verifier keep it
 *                      (HKLM\Software\WineExplorer\Elevation). The password policy and the rest belong to the
 *                      host and the domain: counted as not applicable.
 *   [Registry Values]  MACHINE\path\name=type,data: security options. Written when the key is in one of the two
 *                      policy trees (the consent settings are in ...\CurrentVersion\Policies\System); others are
 *                      counted as not applicable.
 *
 * Every other section (privilege rights, audit, restricted groups, services, file and registry permissions) does
 * not apply to Wine and is not read.
 *
 * Templates are applied in the order given; a later one wins. As on Windows, security settings stay when a GPO
 * no longer sets them ("tattooed"): nothing is undone. Every template is read and checked completely before
 * anything is written.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <setupapi.h>

#include "preg.h"
#include "wexpolicy.h"

#define ELEVATION_KEY  L"Software\\WineExplorer\\Elevation"
#define MAX_TEXT       4096
#define LONGEST_MINUTES 99999

static unsigned int count_set, count_not_applicable;

/* an integer field: decimal, optionally negative (-1 is "forever" in the template) */
static BOOL int_field( INFCONTEXT *ctx, DWORD index, LONG *value )
{
    WCHAR text[32], *end;
    long n;

    if (!SetupGetStringFieldW( ctx, index, text, ARRAY_SIZE(text), NULL ) || !text[0]) return FALSE;
    n = wcstol( text, &end, 10 );
    if (*end) return FALSE;
    *value = n;
    return TRUE;
}

/* [System Access]: the three lockout values; anything else is the host's and the domain's */
static DWORD system_access( HINF inf, BOOL write )
{
    static const WCHAR * const names[] = { L"LockoutBadCount", L"LockoutDuration", L"ResetLockoutCount" };
    INFCONTEXT ctx;
    WCHAR name[64];
    LONG value;
    DWORD i, err = ERROR_SUCCESS;
    HKEY key = NULL;
    BOOL more;

    for (more = SetupFindFirstLineW( inf, L"System Access", NULL, &ctx ); more; more = SetupFindNextLine( &ctx, &ctx ))
    {
        if (!SetupGetStringFieldW( &ctx, 0, name, ARRAY_SIZE(name), NULL )) continue;
        for (i = 0; i < ARRAY_SIZE(names); i++) if (!_wcsicmp( name, names[i] )) break;
        if (i == ARRAY_SIZE(names))
        {
            if (write) count_not_applicable++;
            continue;
        }
        if (!int_field( &ctx, 1, &value )) return ERROR_BAD_FORMAT;
        /* the threshold is 0..999; a time of -1 (until an administrator unlocks) or 0 is the longest */
        if (i == 0 && (value < 0 || value > 999)) return ERROR_BAD_FORMAT;
        if (i > 0 && (value < -1 || value > LONGEST_MINUTES)) return ERROR_BAD_FORMAT;
        if (i > 0 && value <= 0) value = LONGEST_MINUTES;
        if (!write) continue;
        if (!key && (err = RegCreateKeyExW( HKEY_LOCAL_MACHINE, ELEVATION_KEY, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                                            NULL, &key, NULL ))) return err;
        if ((err = RegSetValueExW( key, names[i], 0, REG_DWORD, (const BYTE *)&value, sizeof(DWORD) ))) break;
        count_set++;
    }
    if (key) RegCloseKey( key );
    return err;
}

/* the data of a [Registry Values] line as registry data; returns the size in bytes, 0 on a fault */
static DWORD registry_data( INFCONTEXT *ctx, DWORD type, BYTE *data, DWORD room )
{
    WCHAR *out = (WCHAR *)data;
    DWORD fields = SetupGetFieldCount( ctx ), i, len, used = 0;
    LONG number;

    switch (type)
    {
    case REG_DWORD:
        if (fields != 2 || !int_field( ctx, 2, &number ) || room < sizeof(DWORD)) return 0;
        memcpy( data, &number, sizeof(DWORD) );
        return sizeof(DWORD);
    case REG_SZ:
    case REG_EXPAND_SZ:
        if (fields > 2) return 0;
        if (fields < 2) { out[0] = 0; return sizeof(WCHAR); }
        if (!SetupGetStringFieldW( ctx, 2, out, room / sizeof(WCHAR), &len )) return 0;
        return len * sizeof(WCHAR);
    case REG_MULTI_SZ:
        /* one string per field */
        for (i = 2; i <= fields; i++)
        {
            if (used + 2 >= room / sizeof(WCHAR)) return 0;
            if (!SetupGetStringFieldW( ctx, i, out + used, room / sizeof(WCHAR) - used - 1, &len )) return 0;
            if (len > 1) used += len;
        }
        out[used++] = 0;
        return used * sizeof(WCHAR);
    default:
        return 0;
    }
}

/* [Registry Values]: security options in the policy trees */
static DWORD registry_values( HINF inf, BOOL write )
{
    static BYTE data[MAX_TEXT * sizeof(WCHAR)];
    WCHAR path[MAX_TEXT], *name;
    INFCONTEXT ctx;
    LONG type;
    DWORD size, err = ERROR_SUCCESS;
    HKEY key;
    BOOL more;

    for (more = SetupFindFirstLineW( inf, L"Registry Values", NULL, &ctx ); more; more = SetupFindNextLine( &ctx, &ctx ))
    {
        if (!SetupGetStringFieldW( &ctx, 0, path, ARRAY_SIZE(path), NULL )) return ERROR_BAD_FORMAT;
        if (!int_field( &ctx, 1, &type )) return ERROR_BAD_FORMAT;
        /* REG_BINARY, REG_QWORD and the rest are not used by the options that apply here */
        if ((type != REG_SZ && type != REG_EXPAND_SZ && type != REG_DWORD && type != REG_MULTI_SZ) ||
            _wcsnicmp( path, L"MACHINE\\", 8 ) || !(name = wcsrchr( path, '\\' )) || name == path + 7 ||
            !preg_key_in_policy_tree( (const uint8_t *)(path + 8), name - (path + 8) ))
        {
            if (write) count_not_applicable++;
            continue;
        }
        if (!(size = registry_data( &ctx, type, data, sizeof(data) ))) return ERROR_BAD_FORMAT;
        if (!write) continue;
        *name++ = 0;
        if ((err = RegCreateKeyExW( HKEY_LOCAL_MACHINE, path + 8, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL,
                                    &key, NULL ))) break;
        err = RegSetValueExW( key, name, 0, type, data, size );
        RegCloseKey( key );
        if (err) break;
        count_set++;
    }
    return err;
}

/* the checks that apply_security_templates makes before it writes anything, for one file and without writing: the
 * template a request of the spool carries is installed only when this is ERROR_SUCCESS (store.c) */
DWORD check_security_template( const WCHAR *path )
{
    UINT line = 0;
    HINF inf = SetupOpenInfFileW( path, NULL, INF_STYLE_WIN4, &line );
    DWORD err;

    if (inf == INVALID_HANDLE_VALUE) return GetLastError() ? GetLastError() : ERROR_BAD_FORMAT;
    if (!(err = system_access( inf, FALSE ))) err = registry_values( inf, FALSE );
    SetupCloseInfFile( inf );
    return err;
}

/* wexpolicy security /machine FILE [FILE...] */
DWORD apply_security_templates( int count, WCHAR **paths )
{
    HINF infs[MAX_FILES];
    DWORD err = ERROR_SUCCESS;
    int i;

    for (i = 0; i < count; i++) infs[i] = INVALID_HANDLE_VALUE;
    for (i = 0; i < count && !err; i++)
    {
        UINT line = 0;

        infs[i] = SetupOpenInfFileW( paths[i], NULL, INF_STYLE_WIN4, &line );
        if (infs[i] == INVALID_HANDLE_VALUE)
        {
            err = GetLastError() ? GetLastError() : ERROR_BAD_FORMAT;
            say( "wexpolicy: %ls: error %lu (line %u)\n", paths[i], err, line );
        }
        else if ((err = system_access( infs[i], FALSE )) || (err = registry_values( infs[i], FALSE )))
            say( "wexpolicy: %ls: error %lu\n", paths[i], err );
    }
    for (i = 0; i < count && !err; i++)
        if (!(err = system_access( infs[i], TRUE ))) err = registry_values( infs[i], TRUE );
    for (i = 0; i < count; i++) if (infs[i] != INVALID_HANDLE_VALUE) SetupCloseInfFile( infs[i] );
    if (count_set) bump_serial( HKEY_LOCAL_MACHINE, FALSE );
    say( "wexpolicy: security set=%u not-applicable=%u status=%lu\n", count_set, count_not_applicable, err );
    return err;
}
