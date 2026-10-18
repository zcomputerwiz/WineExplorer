/*
 * OOBE Optional Components Catalog Parser and Types
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OOBE_CATALOG_H
#define OOBE_CATALOG_H

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char verb[32];
    char title[128];
    char publisher[128];
    char description[256];
    char size[32];
    bool reboot;
    bool installed;
    bool disabled;
    char disabled_reason[128];
    bool checked;
    bool accepted;
    char sha256[65];
    char accepted_sha256[65];
} OobeComponentItem;

typedef struct {
    char name[64];
    char real_name[128];
    char password[128];
    bool is_admin;
} UserAccount;

typedef struct {
    UserAccount users[5];
    int user_count;
    bool automatic_updates;
    bool unattended;
    char unattend_path[MAX_PATH];
    OobeComponentItem components[16];
    int component_count;
    int installed_count;
    int skipped_count;
    int declined_count;
    int failed_count;
} OobeContext;

static inline void Utf8ToAnsi(const char *utf8_in, char *ansi_out, size_t out_max)
{
    WCHAR wbuf[1024];
    int wlen, alen;
    if (!ansi_out || out_max == 0) return;
    memset(ansi_out, 0, out_max);
    if (!utf8_in || !utf8_in[0]) return;

    wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_in, -1, wbuf, (int)(sizeof(wbuf)/sizeof(wbuf[0])));
    if (wlen <= 0) {
        /* Win32 conversion failed; fail closed: zero buffer so no corrupted text is presented */
        ansi_out[0] = '\0';
        return;
    }
    alen = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, ansi_out, (int)out_max, NULL, NULL);
    if (alen <= 0) {
        ansi_out[0] = '\0';
        return;
    }
    ansi_out[out_max - 1] = '\0';
}

static inline void ParseCatalogBuffer(const char *buf, OobeContext *ctx)
{
    const char *p;
    if (!buf || !ctx) return;
    p = buf;
    while (*p && ctx->component_count < 16) {
        const char *line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > 0) {
            char line[1024];
            size_t copy_len = (line_len < sizeof(line) - 1) ? line_len : sizeof(line) - 1;
            memcpy(line, p, copy_len);
            line[copy_len] = '\0';
            if (copy_len > 0 && line[copy_len - 1] == '\r') {
                line[copy_len - 1] = '\0';
            }
            if (line[0] != '\0') {
                char *cols[10];
                int col_count = 0;
                char *cursor = line;
                cols[col_count++] = cursor;
                while (*cursor && col_count < 10) {
                    if (*cursor == '\t') {
                        *cursor = '\0';
                        cols[col_count++] = cursor + 1;
                    }
                    cursor++;
                }
                if (col_count >= 7 && cols[0][0] != '\0') {
                    OobeComponentItem *item = &ctx->components[ctx->component_count];
                    memset(item, 0, sizeof(*item));
                    Utf8ToAnsi(cols[0], item->verb, sizeof(item->verb));
                    Utf8ToAnsi(cols[1], item->title, sizeof(item->title));
                    Utf8ToAnsi(cols[2], item->publisher, sizeof(item->publisher));
                    Utf8ToAnsi(cols[3], item->description, sizeof(item->description));
                    Utf8ToAnsi(cols[4], item->size, sizeof(item->size));
                    item->reboot = (strcmp(cols[5], "1") == 0);
                    item->installed = (strcmp(cols[6], "1") == 0);
                    if (col_count >= 8) {
                        item->disabled = (strcmp(cols[7], "1") == 0);
                    }
                    if (col_count >= 9) {
                        Utf8ToAnsi(cols[8], item->disabled_reason, sizeof(item->disabled_reason));
                    }
                    item->checked = false;
                    item->accepted = false;
                    ctx->component_count++;
                }
            }
        }
        if (!line_end) break;
        p = line_end + 1;
    }
}

#endif /* OOBE_CATALOG_H */
