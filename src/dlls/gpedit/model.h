/*
 * gpedit: the settings of a Registry.pol, in memory
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
 * docs/GROUP_POLICY.md, "Local policy". The model holds every entry of a Registry.pol as it was read, so that editing
 * a policy never drops an entry that the table does not know (another tool's, a deletion marker); it changes only
 * the entries of the policies of policies.h. Plain C, tested natively (scripts/test_gpedit_model.sh).
 */

#ifndef GPEDIT_MODEL_H
#define GPEDIT_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "policies.h"

struct model;

enum setting_state { SETTING_NOT_CONFIGURED, SETTING_ENABLED, SETTING_DISABLED };

struct model *model_new(void);
void model_free( struct model *model );

/* the bytes of a Registry.pol (none at all: an empty model); 0, or a negative PREG_ERR_* value for a file that is not
 * a policy file, in which case the model is empty */
int model_load( struct model *model, const uint8_t *data, size_t size );

/* the file for the model, malloc'd: no entries give size 0 (the caller then asks to remove the store file) */
int model_save( const struct model *model, uint8_t **data, size_t *size );

/* A flag policy is Enabled when its value is the enabled number, Disabled when it is the disabled number, and Not
 * Configured when there is no entry; any other number of a flag is Enabled (the number is returned). A number
 * policy is Enabled with its number, or Not Configured. */
void model_get( const struct model *model, const struct policy *policy, enum setting_state *state, uint32_t *number );

/* sets a policy: 0, or -1 when memory is short. For a number policy Enabled writes the number; for a flag, Enabled
 * and Disabled write the policy's numbers; Not Configured removes the entry. */
int model_set( struct model *model, const struct policy *policy, enum setting_state state, uint32_t number );

/* how many entries there are, and how many of them no policy of the table describes (kept as they are) */
size_t model_entries( const struct model *model );
size_t model_foreign_entries( const struct model *model );

#endif
