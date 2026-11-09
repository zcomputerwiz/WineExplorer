/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * WO-14: One canonical S1 encoder (groundwork step 1).
 *
 * Pure byte encoding of the S1 caller-binding record (kind 1) and the frozen
 * request record, shared by the S5 broker (e1s.h) and the S18 standard-user
 * branch (e1s18_broker.c). No hashing, no Win32 calls, and no allocation the
 * caller cannot see: the caller supplies the output buffer and performs the
 * SHA-256 and the SID validation. The field order is the one
 * docs/E1_S1_CONTRACTS.md "Encoding and negotiation" specifies; any different
 * encoding or changed field produces a different digest.
 */
#ifndef WEXELEVATE_E1S_ENCODE_H
#define WEXELEVATE_E1S_ENCODE_H

#include <stdint.h>
#include <string.h>

/* Maximum record size (1 MiB), per the S1 contract. */
#define E1S_ENCODE_MAX_RECORD (1024 * 1024)

/* Maximum Windows string length in code units (matches WEXELEVATE_MAX_PATH_CHARS). */
#ifndef E1S_ENCODE_MAX_STRING_CHARS
#define E1S_ENCODE_MAX_STRING_CHARS 32767
#endif

struct e1s_bytes
{
    uint8_t *data;
    uint32_t length;
    uint32_t capacity;
    int failed;
};

static void e1s_put( struct e1s_bytes *b, const void *data, uint32_t len )
{
    if (b->failed || len > b->capacity - b->length) { b->failed = 1; return; }
    if (len) memcpy( b->data + b->length, data, len );
    b->length += len;
}

static void e1s_uint( struct e1s_bytes *b, uint64_t n, uint32_t size )
{
    uint8_t v[8];
    uint32_t i;
    for (i = 0; i < size; ++i) { v[i] = (uint8_t)(n & 0xff); n >>= 8; }
    e1s_put( b, v, size );
}

static void e1s_blob( struct e1s_bytes *b, const void *p, uint32_t len )
{
    e1s_uint( b, len, 4 );
    e1s_put( b, p, len );
}

/* Explicit UTF-16LE, independent of native structure padding. NUL/surrogate
 * validation. The string is NUL-terminated; the length is counted here so no
 * Win32 call is needed. */
static void e1s_string( struct e1s_bytes *b, const uint16_t *s )
{
    uint32_t i, n = 0;
    if (!s) { b->failed = 1; return; }
    while (s[n]) n++;
    if (n > E1S_ENCODE_MAX_STRING_CHARS) { b->failed = 1; return; }
    e1s_uint( b, (uint64_t)n * 2, 4 );
    for (i = 0; i < n; ++i)
    {
        uint16_t ch = s[i];
        if (ch >= 0xd800 && ch <= 0xdbff)
        {
            if (i + 1 >= n || s[i + 1] < 0xdc00 || s[i + 1] > 0xdfff) { b->failed = 1; return; }
            e1s_uint( b, ch, 2 );
            ch = s[++i];
        }
        else if (ch >= 0xdc00 && ch <= 0xdfff) { b->failed = 1; return; }
        e1s_uint( b, ch, 2 );
    }
}

/*
 * Caller-binding record (kind 1). Pure encoding: no validation, no hashing,
 * no allocation. Field order per docs/E1_S1_CONTRACTS.md record 1. The caller
 * validates the SID and the generations and performs the SHA-256.
 */
static void e1s_encode_binding( struct e1s_bytes *b,
                                uint64_t server_gen, uint64_t root_gen, uint64_t broker_gen,
                                const uint8_t prefix[16], uint64_t host_uid,
                                const uint8_t host_process[16],
                                const uint8_t opener[16], const uint8_t writer[16],
                                uint64_t token_id, uint64_t modified_id, uint64_t auth_id,
                                uint32_t session,
                                const uint8_t *sid, uint32_t sid_len,
                                const uint8_t desktop[16] )
{
    e1s_uint( b, 1, 2 );              /* kind */
    e1s_uint( b, 1, 2 );              /* version */
    e1s_uint( b, 164 + sid_len, 4 );  /* length, including the 16-byte header */
    e1s_uint( b, 7, 8 );              /* required_caps: H2_OPENER | MESSAGE_PROVENANCE | BINDING_V1 */
    e1s_uint( b, server_gen, 8 );
    e1s_uint( b, root_gen, 8 );
    e1s_uint( b, broker_gen, 8 );
    e1s_put( b, prefix, 16 );
    e1s_uint( b, host_uid, 8 );
    e1s_put( b, host_process, 16 );
    e1s_put( b, opener, 16 );
    e1s_put( b, writer, 16 );
    e1s_uint( b, token_id, 8 );
    e1s_uint( b, modified_id, 8 );
    e1s_uint( b, auth_id, 8 );
    e1s_uint( b, session, 4 );
    e1s_uint( b, 0, 4 );              /* open_impersonated=0, write_impersonated=0, reserved=0 */
    e1s_blob( b, sid, sid_len );
    e1s_put( b, desktop, 16 );
}

/*
 * Frozen request record. Pure encoding: no validation, no hashing, no
 * allocation. Field order per docs/E1_S1_CONTRACTS.md "Encoding and
 * negotiation" (request digest): no common header, starts at request_id.
 */
static void e1s_encode_request( struct e1s_bytes *b,
                                uint64_t request_id, const uint8_t binding_digest[32],
                                uint64_t volume_serial, uint64_t file_index,
                                uint64_t file_size, uint64_t last_write,
                                const uint8_t image_hash[32],
                                const uint16_t *image, const uint16_t *cmd, const uint16_t *cwd,
                                uint32_t env_policy, uint32_t show, uint32_t flags,
                                uint64_t deadline )
{
    e1s_uint( b, request_id, 8 );
    e1s_put( b, binding_digest, 32 );
    e1s_uint( b, volume_serial, 8 );
    e1s_uint( b, file_index, 8 );
    e1s_uint( b, file_size, 8 );
    e1s_uint( b, last_write, 8 );
    e1s_put( b, image_hash, 32 );
    e1s_string( b, image );
    e1s_string( b, cmd );
    e1s_string( b, cwd );
    e1s_uint( b, env_policy, 4 );
    e1s_uint( b, show, 4 );
    e1s_uint( b, flags, 4 );
    e1s_uint( b, deadline, 8 );
}

#endif /* WEXELEVATE_E1S_ENCODE_H */
