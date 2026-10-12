/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef WEX_COPYDIRS_H
#define WEX_COPYDIRS_H
#include <stdbool.h>
#include <stdio.h>

/* Create even empty directories and retain numeric ownership/permissions.
 * source_root is empty when copying from the running live filesystem. */
bool copy_layout_directories(FILE *layout, const char *source_root, const char *target_root);
#endif
