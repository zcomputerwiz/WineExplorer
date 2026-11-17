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

/*
 * docs/GROUP_POLICY.md, "The local security settings editor". A built-in table of the settings of Windows' Security
 * Settings tree. The ones that apply here are what wexpolicy's security extension writes (sectmpl.c: the three account
 * lockout values of [System Access] and the registry values of the policy trees in [Registry Values]); the others are
 * listed grayed out with the reason. Plain C, tested natively with template.c (scripts/test_secpol_model.sh).
 */

#ifndef SECPOL_SETTINGS_H
#define SECPOL_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

enum sec_kind
{
    SEC_NUMBER,     /* a number of a unit, from min to max (zero_text: what 0 means) */
    SEC_FLAG,       /* Enabled or Disabled: the registry value 1 or 0 */
    SEC_CHOICE      /* one of a list of registry values */
};

struct sec_choice
{
    uint32_t value;
    const char *text;       /* NULL ends the list */
};

struct sec_setting
{
    const char *node;       /* the folder of the tree: "Account Policies\\Account Lockout Policy" */
    const char *name;
    const char *explain;
    enum sec_kind kind;
    const char *reason;     /* NULL when the setting applies here, else why it does nothing (shown grayed out) */
    const char *section;    /* the template section: "System Access" or "Registry Values" */
    const char *key;        /* the line's name in it */
    const char *regkey;     /* where the system keeps the value now, below HKLM (NULL: nowhere) */
    const char *regvalue;
    const char *label;      /* SEC_NUMBER: the words in front of the number and after it */
    const char *unit;
    uint32_t min, max, def; /* the range; the value when neither the template nor the registry has one */
    const char *zero_text;  /* SEC_NUMBER: what the number 0 means (NULL: nothing special) */
    const struct sec_choice *choices;
    int forever;            /* SEC_NUMBER: 0 stands for "until an administrator unlocks" (-1 in the template, 99999 in the registry) */
};

extern const struct sec_setting sec_settings[];
extern const unsigned int sec_setting_count;

/* the number a template's value or a registry DWORD stands for in the editor ("5", "-1", "4,2"); 0 for a value that
 * cannot be read (ok is then cleared) */
uint32_t sec_from_template( const struct sec_setting *s, const char *value, int *ok );
uint32_t sec_from_registry( const struct sec_setting *s, uint32_t dword );

/* the template's value of an editor number: "N" for a lockout value (-1 for "forever"), "4,N" for a registry value */
void sec_to_template( const struct sec_setting *s, uint32_t number, char *out, size_t room );

/* the registry value that wexpolicy writes for it (the number, or 99999 for "forever") */
uint32_t sec_to_registry( const struct sec_setting *s, uint32_t number );

/* whether number is in the range (and, for a choice, in the list) */
int sec_number_valid( const struct sec_setting *s, uint32_t number );

#endif
