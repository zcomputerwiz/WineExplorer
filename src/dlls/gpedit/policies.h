/*
 * gpedit: the policies the editor offers
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
 * docs/GROUP_POLICY.md, "Local policy". A built-in table: each policy is a registry value in one of the two policy
 * trees, with what Enabled and Disabled write. Names and explanations are this project's own words, the registry
 * values are the facts that Windows components read. Which settings this tree's components honour was surveyed on
 * 2026-11-16 (the "status" of each row); the others are listed grayed out, with the reason.
 */

#ifndef GPEDIT_POLICIES_H
#define GPEDIT_POLICIES_H

#include <stdint.h>

enum policy_scope { POLICY_COMPUTER, POLICY_USER };

enum policy_status
{
    POLICY_APPLIES,           /* a component of this tree acts on the value */
    POLICY_STORED,            /* the value is stored and read by programs that call SHRestricted; no shell part acts on it */
    POLICY_NOT_APPLICABLE     /* nothing here reads it: shown grayed out, and not written */
};

enum policy_kind
{
    POLICY_FLAG,              /* a DWORD: Enabled writes enabled, Disabled writes disabled */
    POLICY_NUMBER             /* a DWORD that Enabled sets to a chosen number (bit masks, levels) */
};

struct policy
{
    const char *id;           /* stable name: scope, key and value */
    enum policy_scope scope;
    enum policy_kind kind;
    enum policy_status status;
    const char *category;     /* "Windows Components\\Windows Explorer" */
    const char *name;
    const char *explain;
    const char *key;          /* below HKLM (computer) or HKCU (user) */
    const char *value;
    uint32_t enabled;         /* POLICY_FLAG: what Enabled writes */
    uint32_t disabled;        /* POLICY_FLAG: what Disabled writes */
    const char *reason;       /* POLICY_NOT_APPLICABLE: why it does nothing here */
    const char *allowed;      /* POLICY_NUMBER: the numbers that are accepted, "2,5", or NULL for any */
};

extern const struct policy policies[];
extern const unsigned int policy_count;

/* the policy of a registry value (key and value name compared without case), or NULL */
const struct policy *policy_find( enum policy_scope scope, const char *key, const char *value );

#endif
