/*
 * The security template's vocabulary as WineExplorer reads and writes it (GptTmpl.inf, secedit's INF files)
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
 * docs/GROUP_POLICY.md, "Step 5: the security template". Three programs read the same file and must agree on it:
 * wexpolicy security applies it (sectmpl.c, through setupapi), Local Security Settings edits the local one
 * (dlls/secpol) and secedit exports and configures one. What they share is here: the account lockout values of
 * [System Access] and where the registry keeps them, the System policy key of the security options (the two that
 * only the template sets are in wexpreg.h), and the text of an empty template. Plain C (char strings), so that the
 * editor's model and its native test use it too; a WCHAR name is written L"" WEX_..._NAME.
 */

#ifndef WEXSECTMPL_H
#define WEXSECTMPL_H

#include <stdint.h>

/* where NetUserModalsSet level 3, the elevation verifier and wexpolicy keep the account lockout policy (HKLM, 64-bit
 * view), in minutes */
#define WEX_ELEVATION_KEY        "Software\\WineExplorer\\Elevation"
/* the key of the security options (the consent settings), as written in [Registry Values] after "MACHINE\" */
#define WEX_SYSTEM_POLICY_KEY    "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System"

#define WEX_LOCKOUT_BADCOUNT_NAME  "LockoutBadCount"
#define WEX_LOCKOUT_DURATION_NAME  "LockoutDuration"
#define WEX_LOCKOUT_RESET_NAME     "ResetLockoutCount"

enum wex_lockout { WEX_LOCKOUT_BADCOUNT, WEX_LOCKOUT_DURATION, WEX_LOCKOUT_RESET, WEX_LOCKOUT_COUNT };

/* The ranges, as Windows' template takes them: the threshold 0..999 (0: never locked out); the two times in minutes
 * up to 99999, where -1 (and 0 for the duration) is "until an administrator unlocks it". The registry has no "for
 * ever": it keeps the longest time, 99999, which every reader takes as for ever (the elevation verifier, netapi32,
 * secedit's export as -1, the editor as "until an administrator unlocks it"). Without a value: threshold 0, 30
 * minutes, 30 minutes. */
#define WEX_LOCKOUT_THRESHOLD_MAX  999
#define WEX_LOCKOUT_LONGEST        99999

static const struct wex_lockout_value
{
    const char *name;
    long min, max;        /* the template's range */
    uint32_t def;         /* what is in force when the registry has no value */
} wex_lockout_values[WEX_LOCKOUT_COUNT] =
{
    { WEX_LOCKOUT_BADCOUNT_NAME, 0, WEX_LOCKOUT_THRESHOLD_MAX, 0 },
    { WEX_LOCKOUT_DURATION_NAME, -1, WEX_LOCKOUT_LONGEST, 30 },
    { WEX_LOCKOUT_RESET_NAME, -1, WEX_LOCKOUT_LONGEST, 30 },
};

/* a [System Access] number of one of the three as the registry keeps it; 0 when the template's number is out of
 * range (the whole template is then refused, as setup would refuse a bad line) */
static inline int wex_lockout_from_template( enum wex_lockout which, long value, uint32_t *registry )
{
    if (which >= WEX_LOCKOUT_COUNT || value < wex_lockout_values[which].min || value > wex_lockout_values[which].max)
        return 0;
    /* a time of -1 or 0 is the longest */
    if (which != WEX_LOCKOUT_BADCOUNT && value <= 0) value = WEX_LOCKOUT_LONGEST;
    *registry = (uint32_t)value;
    return 1;
}

/* the template's number for a registry value (secedit /export): the longest time is written -1, "for ever" */
static inline long wex_lockout_to_template( enum wex_lockout which, uint32_t registry )
{
    if (which != WEX_LOCKOUT_BADCOUNT && (registry >= WEX_LOCKOUT_LONGEST || !registry)) return -1;
    return (long)registry;
}

/* which of the three a [System Access] name is (ASCII, without case); WEX_LOCKOUT_COUNT when none */
static inline enum wex_lockout wex_lockout_of( const char *name )
{
    int i, j;

    for (i = 0; i < WEX_LOCKOUT_COUNT; i++)
    {
        const char *want = wex_lockout_values[i].name;

        for (j = 0; name[j] && want[j]; j++)
        {
            char a = name[j], b = want[j];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (!name[j] && !want[j]) return (enum wex_lockout)i;
    }
    return WEX_LOCKOUT_COUNT;
}

/* A template with nothing in it. Setup reads a file without [Version] (its signature) as no template at all, and
 * [Unicode] says the file is UTF-16, as Windows writes it. */
#define WEX_SECTMPL_UNICODE  "[Unicode]\r\nUnicode=yes\r\n"
#define WEX_SECTMPL_VERSION  "[Version]\r\nsignature=\"$CHICAGO$\"\r\nRevision=1\r\n"
#define WEX_SECTMPL_EMPTY    WEX_SECTMPL_UNICODE WEX_SECTMPL_VERSION

#endif /* WEXSECTMPL_H */
