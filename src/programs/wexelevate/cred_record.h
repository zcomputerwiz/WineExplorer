/*
 * E1-S17: the S1 answer record (kind 2), the S4 account-name rule and buffer wiping. Header-only, so that the
 * broker (cred_dialog.c, the standard-user path) and consent.exe each compile them into their own module without
 * one including the other's source file.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef WEX_CRED_RECORD_H
#define WEX_CRED_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define WEX_S1_ANSWER_KIND              2
#define WEX_S1_ANSWER_VERSION           1
#define WEX_S1_ANSWER_LENGTH            156
#define WEX_S1_REQUIRED_CAPS            0x3ffULL

#define WEX_ANSWER_KIND_TEXT            1
#define WEX_ANSWER_KIND_CONFIRM         2
#define WEX_ANSWER_KIND_CANCEL          3

#define WEX_MAX_ACCOUNT_NAME_LEN        63
#define WEX_MIN_ACCOUNT_NAME_LEN        1

/* Status codes */
#define WEX_STATUS_OK                   0
#define WEX_STATUS_INVALID_PARAMETER    1
#define WEX_STATUS_QUOTA_EXCEEDED       2
#define WEX_STATUS_BUSY                 3
#define WEX_STATUS_CANCELLED            4
#define WEX_STATUS_TIMED_OUT            5
#define WEX_STATUS_DENIED               6
#define WEX_STATUS_NOT_READY            7

/* S1 Answer Record layout (Kind 2) */
#pragma pack(push, 1)
struct wex_s1_answer_record
{
    uint16_t kind;             /* 2 */
    uint16_t version;          /* 1 */
    uint32_t length;           /* 156 (plus payload_len if any) */
    uint64_t required_caps;    /* 0x3ff */
    uint64_t request_id;
    uint32_t attempt;
    uint32_t round;
    uint32_t message_index;
    uint32_t answer_kind;      /* 1=text, 2=confirm, 3=cancel */
    uint8_t  nonce[32];
    uint8_t  binding_digest[32];
    uint8_t  request_digest[32];
    uint8_t  channel_id[16];
    uint32_t payload_len;      /* 0 for broker channel */
};
#pragma pack(pop)

/*
 * S4 Account-Map Name Validation Rules:
 * - Length: 1 to 63 ASCII characters.
 * - Allowed characters: a-z, A-Z, 0-9, '_', '-', '.'
 * - First character cannot be '-'.
 * - Name cannot be '.' or '..'.
 * - Trailing '$' permitted only as the last character (i == len - 1) for machine accounts (len > 1).
 * - Reject path separators, control characters, quotes, whitespace, and markup ('<', '>', '&').
 */
static inline int wex_valid_account_name( const char *name )
{
    size_t i, len;

    if (!name) return 0;
    len = strlen( name );
    if (len < WEX_MIN_ACCOUNT_NAME_LEN || len > WEX_MAX_ACCOUNT_NAME_LEN) return 0;
    if (name[0] == '-' || !strcmp( name, "." ) || !strcmp( name, ".." )) return 0;

    for (i = 0; i < len; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' ||
              (c == '$' && i == len - 1 && len > 1)))
        {
            return 0;
        }
    }
    return 1;
}

/*
 * Secure Memory Wiping Primitive:
 * Guaranteed not to be elided by the compiler.
 */
static inline void wex_secure_wipe( void *ptr, size_t size )
{
    if (!ptr || !size) return;

#if defined(_WIN32)
    SecureZeroMemory( ptr, size );
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
    explicit_bzero( ptr, size );
#else
    {
        volatile unsigned char *p = (volatile unsigned char *)ptr;
        while (size--) *p++ = 0;
    }
#endif
}

static inline int wex_is_buffer_zeroed( const void *ptr, size_t size )
{
    const unsigned char *p = (const unsigned char *)ptr;
    size_t i;

    if (!ptr) return 1;
    for (i = 0; i < size; i++)
    {
        if (p[i] != 0) return 0;
    }
    return 1;
}

/*
 * S1 Answer Record Construction (Kind 2)
 */
static inline int wex_build_answer_record( uint64_t request_id, uint32_t attempt,
                                           uint32_t round, uint32_t index,
                                           uint32_t answer_kind,
                                           const uint8_t nonce[32],
                                           const uint8_t binding_digest[32],
                                           const uint8_t request_digest[32],
                                           const uint8_t channel_id[16],
                                           uint8_t *out_buf, size_t buf_size,
                                           size_t *out_len )
{
    struct wex_s1_answer_record *rec;

    if (!out_buf || buf_size < WEX_S1_ANSWER_LENGTH || !out_len)
        return WEX_STATUS_INVALID_PARAMETER;
    if (!request_id || !attempt || !nonce || !binding_digest || !request_digest || !channel_id)
        return WEX_STATUS_INVALID_PARAMETER;
    if (answer_kind != WEX_ANSWER_KIND_CONFIRM && answer_kind != WEX_ANSWER_KIND_CANCEL &&
        answer_kind != WEX_ANSWER_KIND_TEXT)
        return WEX_STATUS_INVALID_PARAMETER;

    rec = (struct wex_s1_answer_record *)out_buf;
    memset( rec, 0, WEX_S1_ANSWER_LENGTH );

    rec->kind = WEX_S1_ANSWER_KIND;
    rec->version = WEX_S1_ANSWER_VERSION;
    rec->length = WEX_S1_ANSWER_LENGTH;
    rec->required_caps = WEX_S1_REQUIRED_CAPS;
    rec->request_id = request_id;
    rec->attempt = attempt;
    rec->round = round;
    rec->message_index = index;
    rec->answer_kind = answer_kind;

    memcpy( rec->nonce, nonce, 32 );
    memcpy( rec->binding_digest, binding_digest, 32 );
    memcpy( rec->request_digest, request_digest, 32 );
    memcpy( rec->channel_id, channel_id, 16 );
    rec->payload_len = 0;

    *out_len = WEX_S1_ANSWER_LENGTH;
    return WEX_STATUS_OK;
}

/*
 * S1 Answer Record Verification
 */
static inline int wex_verify_answer_record( const uint8_t *buf, size_t len,
                                            uint64_t expected_req_id,
                                            uint32_t expected_attempt,
                                            uint32_t expected_round,
                                            uint32_t expected_index,
                                            uint32_t expected_answer_kind,
                                            const uint8_t expected_nonce[32],
                                            const uint8_t expected_binding_digest[32],
                                            const uint8_t expected_request_digest[32],
                                            const uint8_t expected_channel_id[16] )
{
    const struct wex_s1_answer_record *rec;

    /* R15: an S1 answer is exactly WEX_S1_ANSWER_LENGTH bytes; reject shorter and longer. */
    if (!buf || len != WEX_S1_ANSWER_LENGTH) return 0;
    rec = (const struct wex_s1_answer_record *)buf;

    if (rec->kind != WEX_S1_ANSWER_KIND ||
        rec->version != WEX_S1_ANSWER_VERSION ||
        rec->length != WEX_S1_ANSWER_LENGTH ||
        rec->required_caps != WEX_S1_REQUIRED_CAPS)
        return 0;

    if (rec->request_id != expected_req_id ||
        rec->attempt != expected_attempt ||
        rec->round != expected_round ||
        rec->message_index != expected_index ||
        rec->answer_kind != expected_answer_kind ||
        rec->payload_len != 0)
        return 0;

    if (memcmp( rec->nonce, expected_nonce, 32 ) != 0 ||
        memcmp( rec->binding_digest, expected_binding_digest, 32 ) != 0 ||
        memcmp( rec->request_digest, expected_request_digest, 32 ) != 0 ||
        memcmp( rec->channel_id, expected_channel_id, 16 ) != 0)
        return 0;

    return 1;
}

#endif /* WEX_CRED_RECORD_H */
