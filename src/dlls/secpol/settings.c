/*
 * secpol: the security settings the editor offers
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

/* See settings.h. Names are those of the Security Settings tree; the explanations are this project's own words. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "wexsectmpl.h"

#define ELEVATION  WEX_ELEVATION_KEY
#define SYSTEM     WEX_SYSTEM_POLICY_KEY
#define LONGEST    WEX_LOCKOUT_LONGEST

#define LOCKOUT    "Account Policies\\Account Lockout Policy"
#define PASSWORD   "Account Policies\\Password Policy"
#define AUDIT      "Local Policies\\Audit Policy"
#define RIGHTS     "Local Policies\\User Rights Assignment"
#define OPTIONS    "Local Policies\\Security Options"

#define HOST_PASSWORD "passwords belong to the host (its authentication modules) and to the domain, not to this system"
#define HOST_AUDIT    "this system keeps no audit policy of its own: the host's logs record these events"
#define HOST_RIGHTS   "a right to act on this system is decided by the host and by the elevation service, not by this table"
#define NOT_READ      "the elevation service of this system does not read this value"

#define GRAYED( node, name, explain, reason ) \
    { node, name, explain, SEC_FLAG, reason, "", "", NULL, NULL, NULL, NULL, 0, 1, 0, NULL, NULL, 0 }

static const struct sec_choice admin_prompt[] =
{
    { 2, "Prompt for consent" },
    { 5, "Prompt for consent for changes to the computer (the default)" },
    { 0, NULL }
};

const struct sec_setting sec_settings[] =
{
    /* ---- Account Lockout Policy: the three values wexpolicy writes where the elevation verifier reads them ---- */
    { LOCKOUT, "Account lockout duration",
      "How long an account stays locked out, in minutes, after too many invalid attempts to confirm an elevation. 0 "
      "keeps it locked until an administrator unlocks it. The value applies only when an account lockout threshold "
      "is set.", SEC_NUMBER, NULL, "System Access", "LockoutDuration", ELEVATION, "LockoutDuration",
      "Account is locked out for:", "minutes", 0, LONGEST, 30,
      "Account is locked out until an administrator unlocks it", NULL, 1 },
    { LOCKOUT, "Account lockout threshold",
      "How many invalid attempts to confirm an elevation lock the account out. 0 never locks an account out.",
      SEC_NUMBER, NULL, "System Access", "LockoutBadCount", ELEVATION, "LockoutBadCount",
      "Account will lock out after:", "invalid logon attempts", 0, 999, 0, "Account will not lock out", NULL, 0 },
    { LOCKOUT, "Reset account lockout counter after",
      "How many minutes pass before the count of invalid attempts is set back to zero.",
      SEC_NUMBER, NULL, "System Access", "ResetLockoutCount", ELEVATION, "ResetLockoutCount",
      "Reset account lockout counter after:", "minutes", 1, LONGEST, 30, NULL, NULL, 0 },

    /* ---- Password Policy ---- */
    GRAYED( PASSWORD, "Enforce password history", "How many earlier passwords an account cannot use again.", HOST_PASSWORD ),
    GRAYED( PASSWORD, "Maximum password age", "How long a password may be used before it must be changed.", HOST_PASSWORD ),
    GRAYED( PASSWORD, "Minimum password age", "How long a password must be used before it may be changed.", HOST_PASSWORD ),
    GRAYED( PASSWORD, "Minimum password length", "The fewest characters a password may have.", HOST_PASSWORD ),
    GRAYED( PASSWORD, "Password must meet complexity requirements", "Whether a password must mix kinds of characters.", HOST_PASSWORD ),
    GRAYED( PASSWORD, "Store passwords using reversible encryption", "Whether passwords are kept in a form that can be read back.", HOST_PASSWORD ),

    /* ---- Audit Policy ---- */
    GRAYED( AUDIT, "Audit account logon events", "Records the check of an account's credentials.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit account management", "Records the creation and change of accounts and groups.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit directory service access", "Records access to objects of a directory service.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit logon events", "Records every logon and logoff.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit object access", "Records access to files, folders and registry keys.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit policy change", "Records changes to the audit, rights and trust policies.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit privilege use", "Records the use of a user right.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit process tracking", "Records the start and end of programs.", HOST_AUDIT ),
    GRAYED( AUDIT, "Audit system events", "Records the start and stop of the system.", HOST_AUDIT ),

    /* ---- User Rights Assignment ---- */
    GRAYED( RIGHTS, "Access this computer from the network", "Who may connect to this computer over the network.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Act as part of the operating system", "Who may act as the system itself.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Allow log on locally", "Who may log on at this computer.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Back up files and directories", "Who may read any file in order to back it up.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Change the system time", "Who may set the clock.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Debug programs", "Who may attach a debugger to any program.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Deny log on locally", "Who may not log on at this computer.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Load and unload device drivers", "Who may load drivers.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Log on as a service", "Which accounts may run a service.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Manage auditing and security log", "Who may set the audit policy and clear the security log.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Shut down the system", "Who may shut down this computer.", HOST_RIGHTS ),
    GRAYED( RIGHTS, "Take ownership of files or other objects", "Who may take ownership of an object.", HOST_RIGHTS ),

    /* ---- Security Options: the registry values of the policy trees that the elevation service reads ---- */
    { OPTIONS, "User Account Control: Behavior of the elevation prompt for administrators",
      "How an administrator is asked before a program runs with full rights. A prompt for consent always asks; "
      "the default asks for changes to the computer. Other behaviors of Windows (elevating without a prompt, asking "
      "for credentials) are not supported here, and the elevation service refuses a request when the value is one of "
      "them.", SEC_CHOICE, NULL, "Registry Values", "MACHINE\\" SYSTEM "\\ConsentPromptBehaviorAdmin", SYSTEM,
      "ConsentPromptBehaviorAdmin", NULL, NULL, 2, 5, 5, NULL, admin_prompt, 0 },
    { OPTIONS, "User Account Control: Switch to the secure desktop when prompting for elevation",
      "Enabled (the default): the prompt appears on the secure desktop, where other programs cannot reach it. "
      "Disabled: it appears on the user's own desktop.", SEC_FLAG, NULL, "Registry Values",
      "MACHINE\\" SYSTEM "\\PromptOnSecureDesktop", SYSTEM, "PromptOnSecureDesktop", NULL, NULL, 0, 1, 1, NULL, NULL, 0 },
    GRAYED( OPTIONS, "User Account Control: Behavior of the elevation prompt for standard users",
            "How a standard user is asked to elevate.", NOT_READ ),
    GRAYED( OPTIONS, "User Account Control: Detect application installations and prompt for elevation",
            "Whether a program that looks like an installer is asked to elevate.", NOT_READ ),
    GRAYED( OPTIONS, "User Account Control: Run all administrators in Admin Approval Mode",
            "Whether administrators run with a limited token until they confirm.", NOT_READ ),
};

const unsigned int sec_setting_count = sizeof(sec_settings) / sizeof(sec_settings[0]);

/* a field of a line as setup reads it: the quotes dropped, the blanks around it cut; the rest of the line after the
 * comma that ends it (outside quotes) in *next, or NULL */
static void field_of( const char *value, char *out, size_t room, const char **next )
{
    size_t n = 0;
    int quoted = 0;

    *next = NULL;
    while (*value == ' ' || *value == '\t') value++;
    for (; *value; value++)
    {
        if (*value == '"') { quoted = !quoted; continue; }
        if (*value == ',' && !quoted) { *next = value + 1; break; }
        if (n + 1 < room) out[n++] = *value;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = 0;
}

static int number_of( const char *text, long *n )
{
    char *end;

    if (!*text) return 0;
    *n = strtol( text, &end, 10 );
    return !*end;
}

/* "5", "-1" or "4,5" (a registry value: its type, a comma, the data), as wexpolicy applies it (sectmpl.c): what the
 * template sets is what is in force, so the value is read as the registry will keep it */
uint32_t sec_from_template( const struct sec_setting *s, const char *value, int *ok )
{
    char field[64];
    const char *next;
    enum wex_lockout which;
    uint32_t registry;
    long n;

    *ok = 0;
    field_of( value, field, sizeof(field), &next );
    if (s->kind != SEC_NUMBER)
    {
        /* REG_DWORD only, the one type of the two options (wexpolicy leaves another type out) */
        if (!number_of( field, &n ) || n != 4 || !next) return 0;
        field_of( next, field, sizeof(field), &next );
        if (next || !number_of( field, &n ) || n < 0 || n > 0xfffffffeL) return 0;
        *ok = 1;
        return (uint32_t)n;
    }
    if (next || !number_of( field, &n )) return 0;
    /* the lockout values: the template's range, and a time of -1 or 0 kept as the longest (wexsectmpl.h) */
    if ((which = wex_lockout_of( s->key )) == WEX_LOCKOUT_COUNT)
    {
        if (n < 0 || n > 0xfffffffeL) return 0;
        *ok = 1;
        return (uint32_t)n;
    }
    if (!wex_lockout_from_template( which, n, &registry )) return 0;
    *ok = 1;
    return sec_from_registry( s, registry );
}

uint32_t sec_from_registry( const struct sec_setting *s, uint32_t dword )
{
    return s->forever && dword >= LONGEST ? 0 : dword;
}

void sec_to_template( const struct sec_setting *s, uint32_t number, char *out, size_t room )
{
    if (s->kind == SEC_NUMBER)
    {
        if (s->forever && !number) snprintf( out, room, "-1" );
        else snprintf( out, room, "%lu", (unsigned long)number );
    }
    else snprintf( out, room, "4,%lu", (unsigned long)number );
}

uint32_t sec_to_registry( const struct sec_setting *s, uint32_t number )
{
    return s->forever && !number ? LONGEST : number;
}

int sec_number_valid( const struct sec_setting *s, uint32_t number )
{
    const struct sec_choice *c;

    if (s->kind == SEC_FLAG) return number <= 1;
    if (s->kind == SEC_CHOICE)
    {
        for (c = s->choices; c && c->text; c++) if (c->value == number) return 1;
        return 0;
    }
    return number >= s->min && number <= s->max;
}
