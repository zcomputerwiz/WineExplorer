/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * WO-18: E1R8 C codec (R8 step 2, part 1).
 *
 * Header-only, portable C (stdint.h and string.h only; no Win32, no
 * allocation) byte codec for the E1-S18e R8 attempt-tuple messages, the
 * S7 version 2 PROMPT and the secret password answer, per the frozen
 * contract docs/E1_S18E_R8_SPEC.md (v5.1) sections 1, 2, 4, 5, 6, 7 and 11.
 *
 * Little-endian, no padding, exact length, all lengths checked before any
 * write. Explicit little-endian put/get helpers are used throughout; a
 * struct is never memcpy'd onto the wire. Every decoder takes (buf, len)
 * and the expected sealed tuple, returns 0 on success or a negative error,
 * and checks the exact total length for its type, the magic, version 1,
 * the type, reserved 0, the enum values, the string rules, the refused
 * RESULT zero/empty rule and the absence of trailing bytes.
 */
#ifndef WEXELEVATE_E1R8_CODEC_H
#define WEXELEVATE_E1R8_CODEC_H

#include <stdint.h>
#include <string.h>

/*
 * Sizes (bytes), per spec section 4.
 */
#define E1R8_HEADER_SIZE            76
#define E1R8_TAIL_SIZE              160
#define E1R8_PRE_ENDORSEMENT_SIZE   204
#define E1R8_SEALED_SIZE            220
#define E1R8_S1_RECORD_SIZE         156
#define E1R8_S7_PROMPT_SIZE         178

/*
 * Message type constants, per spec sections 5 and 6.
 */
#define E1R8_TYPE_INIT              1
#define E1R8_TYPE_ACK               2
#define E1R8_TYPE_SELECT_OK         3
#define E1R8_TYPE_SELECT            4
#define E1R8_TYPE_READY             5
#define E1R8_TYPE_RESULT            6
#define E1R8_TYPE_ANSWER            7
#define E1R8_TYPE_DONE              8
#define E1R8_TYPE_ATTACH_OK         9
#define E1R8_TYPE_ENDORSED          16
#define E1R8_TYPE_START             17
#define E1R8_TYPE_WORKER_READY      18
#define E1R8_TYPE_DELEGATED         19
#define E1R8_TYPE_RESULT_V          20
#define E1R8_TYPE_CANCEL            21
#define E1R8_TYPE_W_ACK             22
#define E1R8_TYPE_ATTACH_ACK        23
#define E1R8_TYPE_PROMPT_RELEASE    24
#define E1R8_TYPE_RELEASE           25
#define E1R8_TYPE_RELEASED          26
#define E1R8_TYPE_W_SETUP           27
/* Lab frame, header only: the verifier's answer to START, passed on to the dialog in place of ATTACH_OK,
 * when the account lockout policy has closed the account. No worker exists and the request ends. */
#define E1R8_TYPE_LOCKED            31

/*
 * Enum values, per spec sections 5 and 6.
 */
#define E1R8_DESKTOP_MODE_SECURE    1
#define E1R8_DESKTOP_MODE_INSECURE  2
#define E1R8_RESULT_AUTHENTICATED   1
#define E1R8_RESULT_REFUSED         2
#define E1R8_DONE_COMPLETED         1
#define E1R8_DONE_CANCELLED         2
#define E1R8_CANCEL_REASON          1
#define E1R8_RELEASE_COMPLETED      1
#define E1R8_RELEASE_CANCELLED      2

/*
 * S1 answer kinds (kind-2 record), per docs/E1_S1_CONTRACTS.md record 2.
 */
#define E1R8_S1_KIND                2
#define E1R8_S1_VERSION             1
#define E1R8_S1_ANSWER_TEXT         1
#define E1R8_S1_ANSWER_CONFIRM      2
#define E1R8_S1_ANSWER_CANCEL       3

/*
 * Error codes. 0 is success; every failure is a distinct negative value.
 */
#define E1R8_OK                     0
#define E1R8_ERR_SHORT              (-1)
#define E1R8_ERR_LONG               (-2)
#define E1R8_ERR_MAGIC              (-3)
#define E1R8_ERR_VERSION            (-4)
#define E1R8_ERR_TYPE               (-5)
#define E1R8_ERR_RESERVED           (-6)
#define E1R8_ERR_ID                 (-7)
#define E1R8_ERR_LENGTH             (-8)
#define E1R8_ERR_ENUM               (-9)
#define E1R8_ERR_STRING             (-10)
#define E1R8_ERR_S1                 (-11)
#define E1R8_ERR_TUPLE              (-12)
#define E1R8_ERR_UTF8               (-13)
#define E1R8_ERR_REFUSED            (-14)

/*
 * Decoded common header values (spec section 4). The magic is always
 * "E1R8" and is not stored.
 */
struct e1r8_header
{
    uint16_t version;
    uint16_t type;
    uint32_t length;
    uint64_t request_id;
    uint32_t attempt;
    uint32_t reserved;
    uint8_t nonce[32];
    uint8_t channel_id[16];
};

/*
 * Sealed tuple fields (spec section 2). The pre-endorsement tuple is the
 * same fields without dialog_id.
 */
struct e1r8_tuple
{
    uint64_t request_id;
    uint32_t attempt;
    uint8_t nonce[32];
    uint8_t channel_id[16];
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    uint64_t deadline;
    uint64_t root_gen;
    uint64_t server_gen;
    uint64_t broker_gen;
    uint64_t verifier_gen;
    uint64_t map_gen;
    uint64_t policy_gen;
    uint32_t caller_session;
    uint8_t desktop_route[16];
    uint32_t desktop_mode;
    uint8_t dialog_id[16];
};

/*
 * Message body output structures. String fields point into the decoded
 * buffer; the caller must not free them.
 */
struct e1r8_init
{
    struct e1r8_tuple tuple;
    const uint8_t *verifier_owner;
    uint32_t verifier_owner_len;
    const uint8_t *requester;
    uint32_t requester_len;
    const uint8_t *image;
    uint32_t image_len;
    const uint8_t *publisher;
    uint32_t publisher_len;
};

struct e1r8_ack
{
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    uint8_t dialog_id[16];
};

struct e1r8_select
{
    const uint8_t *account;
    uint32_t account_len;
};

struct e1r8_result
{
    uint32_t status;
    const uint8_t *account;
    uint32_t account_len;
    uint8_t challenge[32];
};

struct e1r8_answer
{
    uint8_t challenge[32];
    uint16_t kind;
    uint16_t version;
    uint32_t length;
    uint64_t required_caps;
    uint64_t request_id;
    uint32_t attempt;
    uint32_t round;
    uint32_t message_index;
    uint32_t answer_kind;
    uint8_t nonce[32];
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    uint8_t channel_id[16];
    uint32_t payload_len;
    const uint8_t *payload;
};

struct e1r8_done
{
    uint32_t status;
};

struct e1r8_endorsed
{
    struct e1r8_tuple tuple;
    uint32_t pid;
    uint32_t uid;
};

struct e1r8_start
{
    const uint8_t *account;
    uint32_t account_len;
    uint8_t account_key[16];
};

struct e1r8_result_v
{
    uint32_t status;
    uint8_t account_key[16];
    const uint8_t *account;
    uint32_t account_len;
    uint8_t evidence_ref[16];
};

struct e1r8_cancel
{
    uint32_t reason;
};

struct e1r8_release
{
    uint32_t outcome;
};

struct e1r8_w_setup
{
    struct e1r8_tuple tuple;
    uint32_t pid;
    uint32_t uid;
};

/*
 * Explicit little-endian put/get helpers.
 */
static inline void e1r8_put_u16( uint8_t *p, uint16_t v )
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

static inline void e1r8_put_u32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static inline void e1r8_put_u64( uint8_t *p, uint64_t v )
{
    int i;
    for (i = 0; i < 8; ++i)
        p[i] = (uint8_t)((v >> (8 * i)) & 0xff);
}

static inline uint16_t e1r8_get_u16( const uint8_t *p )
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t e1r8_get_u32( const uint8_t *p )
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t e1r8_get_u64( const uint8_t *p )
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

/*
 * A wire string is a u32 byte length followed by exactly that many bytes,
 * no NUL terminator (spec section 4).
 */
static inline void e1r8_put_string( uint8_t *p, const uint8_t *s, uint32_t len )
{
    e1r8_put_u32( p, len );
    if (len) memcpy( p + 4, s, len );
}

static inline int e1r8_get_string( const uint8_t *buf, size_t len, size_t *off,
                            const uint8_t **out, uint32_t *out_len )
{
    uint32_t slen;
    /* Written so that nothing can wrap when size_t is 32 bits (i686 PE). */
    if (*off > len || len - *off < 4) return E1R8_ERR_SHORT;
    slen = e1r8_get_u32( buf + *off );
    if (slen > len - *off - 4) return E1R8_ERR_SHORT;
    *out = buf + *off + 4;
    *out_len = slen;
    *off += 4 + slen;
    return 0;
}

/*
 * Account name: ASCII, 1..63 bytes, S4 grammar (valid_name,
 * wine/server/accounts.c), matching wex_valid_account_name.
 */
static inline int e1r8_valid_account_name( const uint8_t *s, size_t len )
{
    size_t i;
    if (!s || len < 1 || len > 63) return 0;
    if (s[0] == '-' || (len == 1 && s[0] == '.') ||
        (len == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (i = 0; i < len; ++i)
    {
        uint8_t c = s[i];
        if (c > 127) return 0;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' ||
              (c == '$' && i == len - 1 && len > 1)))
            return 0;
    }
    return 1;
}

/*
 * Display string: UTF-16LE, even length, 0..520 bytes, valid surrogate
 * pairs, no NUL or control characters (spec section 4).
 */
static inline int e1r8_valid_display_string( const uint8_t *s, size_t len )
{
    size_t i = 0;
    if (len % 2 != 0) return 0;
    if (len > 520) return 0;
    if (!s && len) return 0;
    while (i < len)
    {
        uint16_t ch = (uint16_t)s[i] | ((uint16_t)s[i + 1] << 8);
        if (ch >= 0xd800 && ch <= 0xdbff)
        {
            uint16_t lo;
            if (i + 4 > len) return 0;
            lo = (uint16_t)s[i + 2] | ((uint16_t)s[i + 3] << 8);
            if (lo < 0xdc00 || lo > 0xdfff) return 0;
            i += 4;
        }
        else if (ch >= 0xdc00 && ch <= 0xdfff)
        {
            return 0;
        }
        else
        {
            if (ch <= 0x1f || (ch >= 0x7f && ch <= 0x9f)) return 0;
            i += 2;
        }
    }
    return 1;
}

/*
 * Verifier owner: ASCII D-Bus unique name ":<digits>.<digits>", 4..255
 * bytes (spec section 4).
 */
static inline int e1r8_valid_verifier_owner( const uint8_t *s, size_t len )
{
    size_t i, dot = 0;
    int found_dot = 0;
    if (!s || len < 4 || len > 255) return 0;
    if (s[0] != ':') return 0;
    for (i = 1; i < len; ++i)
    {
        if (s[i] == '.')
        {
            if (found_dot) return 0;
            found_dot = 1;
            dot = i;
        }
        else if (s[i] < '0' || s[i] > '9')
        {
            return 0;
        }
    }
    if (!found_dot) return 0;
    if (dot == 1) return 0;
    if (dot + 1 >= len) return 0;
    return 1;
}

/*
 * Strict RFC 3629 UTF-8 (spec section 4): no truncated or missing
 * continuation, no overlong, no surrogates, nothing above U+10FFFF, no
 * C0, C1, F5..FF, no NUL. Length 1..4096 bytes.
 */
static inline int e1r8_valid_utf8( const uint8_t *s, size_t len )
{
    size_t i = 0;
    if (!s || len < 1 || len > 4096) return 0;
    while (i < len)
    {
        uint8_t b = s[i];
        if (b == 0) return 0;
        if (b < 0x80)
        {
            i += 1;
        }
        else if (b >= 0xc2 && b <= 0xdf)
        {
            uint8_t c;
            if (i + 1 >= len) return 0;
            c = s[i + 1];
            if (c < 0x80 || c > 0xbf) return 0;
            i += 2;
        }
        else if (b >= 0xe0 && b <= 0xef)
        {
            uint8_t c, d;
            if (i + 2 >= len) return 0;
            c = s[i + 1];
            d = s[i + 2];
            if (c < 0x80 || c > 0xbf) return 0;
            if (d < 0x80 || d > 0xbf) return 0;
            if (b == 0xe0 && c < 0xa0) return 0;
            if (b == 0xed && c > 0x9f) return 0;
            i += 3;
        }
        else if (b >= 0xf0 && b <= 0xf4)
        {
            uint8_t c, d, e;
            if (i + 3 >= len) return 0;
            c = s[i + 1];
            d = s[i + 2];
            e = s[i + 3];
            if (c < 0x80 || c > 0xbf) return 0;
            if (d < 0x80 || d > 0xbf) return 0;
            if (e < 0x80 || e > 0xbf) return 0;
            if (b == 0xf0 && c < 0x90) return 0;
            if (b == 0xf4 && c > 0x8f) return 0;
            i += 4;
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

/*
 * Common header (76 bytes), spec section 4 offsets. The request_id,
 * attempt, nonce and channel_id come from the sealed tuple.
 */
static inline int e1r8_put_header( uint8_t *buf, size_t len, uint16_t type,
                            uint32_t total_len, const struct e1r8_tuple *t )
{
    if (len < E1R8_HEADER_SIZE) return E1R8_ERR_SHORT;
    buf[0] = 'E';
    buf[1] = '1';
    buf[2] = 'R';
    buf[3] = '8';
    e1r8_put_u16( buf + 4, 1 );
    e1r8_put_u16( buf + 6, type );
    e1r8_put_u32( buf + 8, total_len );
    e1r8_put_u64( buf + 12, t->request_id );
    e1r8_put_u32( buf + 20, t->attempt );
    e1r8_put_u32( buf + 24, 0 );
    memcpy( buf + 28, t->nonce, 32 );
    memcpy( buf + 60, t->channel_id, 16 );
    return 0;
}

static inline int e1r8_get_header( const uint8_t *buf, size_t len, uint16_t type,
                            const struct e1r8_tuple *expected,
                            struct e1r8_header *out )
{
    if (len < E1R8_HEADER_SIZE) return E1R8_ERR_SHORT;
    if (memcmp( buf, "E1R8", 4 ) != 0) return E1R8_ERR_MAGIC;
    if (e1r8_get_u16( buf + 4 ) != 1) return E1R8_ERR_VERSION;
    if (e1r8_get_u16( buf + 6 ) != type) return E1R8_ERR_TYPE;
    if (e1r8_get_u32( buf + 8 ) != (uint32_t)len) return E1R8_ERR_LENGTH;
    if (e1r8_get_u64( buf + 12 ) != expected->request_id) return E1R8_ERR_ID;
    if (e1r8_get_u32( buf + 20 ) != expected->attempt) return E1R8_ERR_ID;
    if (e1r8_get_u32( buf + 24 ) != 0) return E1R8_ERR_RESERVED;
    if (memcmp( buf + 28, expected->nonce, 32 ) != 0) return E1R8_ERR_ID;
    if (memcmp( buf + 60, expected->channel_id, 16 ) != 0) return E1R8_ERR_ID;
    if (out)
    {
        out->version = 1;
        out->type = type;
        out->length = (uint32_t)len;
        out->request_id = expected->request_id;
        out->attempt = expected->attempt;
        out->reserved = 0;
        memcpy( out->nonce, expected->nonce, 32 );
        memcpy( out->channel_id, expected->channel_id, 16 );
    }
    return 0;
}

/*
 * Tuple tail (160 bytes), spec section 4 offsets: binding_digest 0,
 * request_digest 32, deadline 64, root_gen 72, server_gen 80, broker_gen
 * 88, verifier_gen 96, map_gen 104, policy_gen 112, caller_session 120,
 * desktop_route 124, desktop_mode 140, dialog_id 144; end 160.
 */
static inline int e1r8_encode_tuple_tail( uint8_t *buf, size_t len,
                                   const struct e1r8_tuple *t )
{
    if (len != E1R8_TAIL_SIZE) return E1R8_ERR_LONG;
    memcpy( buf + 0, t->binding_digest, 32 );
    memcpy( buf + 32, t->request_digest, 32 );
    e1r8_put_u64( buf + 64, t->deadline );
    e1r8_put_u64( buf + 72, t->root_gen );
    e1r8_put_u64( buf + 80, t->server_gen );
    e1r8_put_u64( buf + 88, t->broker_gen );
    e1r8_put_u64( buf + 96, t->verifier_gen );
    e1r8_put_u64( buf + 104, t->map_gen );
    e1r8_put_u64( buf + 112, t->policy_gen );
    e1r8_put_u32( buf + 120, t->caller_session );
    memcpy( buf + 124, t->desktop_route, 16 );
    e1r8_put_u32( buf + 140, t->desktop_mode );
    memcpy( buf + 144, t->dialog_id, 16 );
    return 0;
}

static inline int e1r8_decode_tuple_tail( const uint8_t *buf, size_t len,
                                   struct e1r8_tuple *t )
{
    if (len != E1R8_TAIL_SIZE) return E1R8_ERR_LONG;
    memcpy( t->binding_digest, buf + 0, 32 );
    memcpy( t->request_digest, buf + 32, 32 );
    t->deadline = e1r8_get_u64( buf + 64 );
    t->root_gen = e1r8_get_u64( buf + 72 );
    t->server_gen = e1r8_get_u64( buf + 80 );
    t->broker_gen = e1r8_get_u64( buf + 88 );
    t->verifier_gen = e1r8_get_u64( buf + 96 );
    t->map_gen = e1r8_get_u64( buf + 104 );
    t->policy_gen = e1r8_get_u64( buf + 112 );
    t->caller_session = e1r8_get_u32( buf + 120 );
    memcpy( t->desktop_route, buf + 124, 16 );
    t->desktop_mode = e1r8_get_u32( buf + 140 );
    memcpy( t->dialog_id, buf + 144, 16 );
    if (t->desktop_mode != E1R8_DESKTOP_MODE_SECURE &&
        t->desktop_mode != E1R8_DESKTOP_MODE_INSECURE)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Pre-endorsement tuple (204 bytes): request_id 0, attempt 8, nonce 12,
 * channel_id 44, then the tail fields at 60 + tail offset, without
 * dialog_id (spec section 4).
 */
static inline int e1r8_encode_pre_endorsement_tuple( uint8_t *buf, size_t len,
                                              const struct e1r8_tuple *t )
{
    if (len != E1R8_PRE_ENDORSEMENT_SIZE) return E1R8_ERR_LONG;
    e1r8_put_u64( buf + 0, t->request_id );
    e1r8_put_u32( buf + 8, t->attempt );
    memcpy( buf + 12, t->nonce, 32 );
    memcpy( buf + 44, t->channel_id, 16 );
    memcpy( buf + 60, t->binding_digest, 32 );
    memcpy( buf + 92, t->request_digest, 32 );
    e1r8_put_u64( buf + 124, t->deadline );
    e1r8_put_u64( buf + 132, t->root_gen );
    e1r8_put_u64( buf + 140, t->server_gen );
    e1r8_put_u64( buf + 148, t->broker_gen );
    e1r8_put_u64( buf + 156, t->verifier_gen );
    e1r8_put_u64( buf + 164, t->map_gen );
    e1r8_put_u64( buf + 172, t->policy_gen );
    e1r8_put_u32( buf + 180, t->caller_session );
    memcpy( buf + 184, t->desktop_route, 16 );
    e1r8_put_u32( buf + 200, t->desktop_mode );
    return 0;
}

static inline int e1r8_decode_pre_endorsement_tuple( const uint8_t *buf, size_t len,
                                              struct e1r8_tuple *t )
{
    if (len != E1R8_PRE_ENDORSEMENT_SIZE) return E1R8_ERR_LONG;
    t->request_id = e1r8_get_u64( buf + 0 );
    t->attempt = e1r8_get_u32( buf + 8 );
    memcpy( t->nonce, buf + 12, 32 );
    memcpy( t->channel_id, buf + 44, 16 );
    memcpy( t->binding_digest, buf + 60, 32 );
    memcpy( t->request_digest, buf + 92, 32 );
    t->deadline = e1r8_get_u64( buf + 124 );
    t->root_gen = e1r8_get_u64( buf + 132 );
    t->server_gen = e1r8_get_u64( buf + 140 );
    t->broker_gen = e1r8_get_u64( buf + 148 );
    t->verifier_gen = e1r8_get_u64( buf + 156 );
    t->map_gen = e1r8_get_u64( buf + 164 );
    t->policy_gen = e1r8_get_u64( buf + 172 );
    t->caller_session = e1r8_get_u32( buf + 180 );
    memcpy( t->desktop_route, buf + 184, 16 );
    t->desktop_mode = e1r8_get_u32( buf + 200 );
    if (t->desktop_mode != E1R8_DESKTOP_MODE_SECURE &&
        t->desktop_mode != E1R8_DESKTOP_MODE_INSECURE)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Sealed tuple (220 bytes): the pre-endorsement tuple plus dialog_id
 * (spec section 4).
 */
static inline int e1r8_encode_sealed_tuple( uint8_t *buf, size_t len,
                                     const struct e1r8_tuple *t )
{
    if (len != E1R8_SEALED_SIZE) return E1R8_ERR_LONG;
    e1r8_put_u64( buf + 0, t->request_id );
    e1r8_put_u32( buf + 8, t->attempt );
    memcpy( buf + 12, t->nonce, 32 );
    memcpy( buf + 44, t->channel_id, 16 );
    return e1r8_encode_tuple_tail( buf + 60, E1R8_TAIL_SIZE, t );
}

static inline int e1r8_decode_sealed_tuple( const uint8_t *buf, size_t len,
                                     struct e1r8_tuple *t )
{
    if (len != E1R8_SEALED_SIZE) return E1R8_ERR_LONG;
    t->request_id = e1r8_get_u64( buf + 0 );
    t->attempt = e1r8_get_u32( buf + 8 );
    memcpy( t->nonce, buf + 12, 32 );
    memcpy( t->channel_id, buf + 44, 16 );
    return e1r8_decode_tuple_tail( buf + 60, E1R8_TAIL_SIZE, t );
}

/*
 * Verify a tuple tail against the expected sealed tuple.
 */
static inline int e1r8_check_tuple_tail( const uint8_t *buf, size_t len,
                                  const struct e1r8_tuple *expected )
{
    struct e1r8_tuple t;
    int err = e1r8_decode_tuple_tail( buf, len, &t );
    if (err) return err;
    if (memcmp( t.binding_digest, expected->binding_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( t.request_digest, expected->request_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (t.deadline != expected->deadline) return E1R8_ERR_TUPLE;
    if (t.root_gen != expected->root_gen) return E1R8_ERR_TUPLE;
    if (t.server_gen != expected->server_gen) return E1R8_ERR_TUPLE;
    if (t.broker_gen != expected->broker_gen) return E1R8_ERR_TUPLE;
    if (t.verifier_gen != expected->verifier_gen) return E1R8_ERR_TUPLE;
    if (t.map_gen != expected->map_gen) return E1R8_ERR_TUPLE;
    if (t.policy_gen != expected->policy_gen) return E1R8_ERR_TUPLE;
    if (t.caller_session != expected->caller_session) return E1R8_ERR_TUPLE;
    if (memcmp( t.desktop_route, expected->desktop_route, 16 ) != 0) return E1R8_ERR_TUPLE;
    if (t.desktop_mode != expected->desktop_mode) return E1R8_ERR_TUPLE;
    if (memcmp( t.dialog_id, expected->dialog_id, 16 ) != 0) return E1R8_ERR_TUPLE;
    return 0;
}

/*
 * Type 1: INIT (B->D). Body: tuple tail (160), verifier_owner:str,
 * requester:str, image:str, publisher:str. Total 256..2067.
 */
static inline int e1r8_encode_init( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                             const uint8_t *verifier_owner, uint32_t vo_len,
                             const uint8_t *requester, uint32_t req_len,
                             const uint8_t *image, uint32_t img_len,
                             const uint8_t *publisher, uint32_t pub_len )
{
    uint32_t total;
    size_t off;
    int err;

    if (!e1r8_valid_verifier_owner( verifier_owner, vo_len )) return E1R8_ERR_STRING;
    if (!e1r8_valid_display_string( requester, req_len )) return E1R8_ERR_STRING;
    if (!e1r8_valid_display_string( image, img_len )) return E1R8_ERR_STRING;
    if (!e1r8_valid_display_string( publisher, pub_len )) return E1R8_ERR_STRING;

    total = E1R8_HEADER_SIZE + E1R8_TAIL_SIZE +
            (4 + vo_len) + (4 + req_len) + (4 + img_len) + (4 + pub_len);
    if (len != total) return E1R8_ERR_LONG;

    err = e1r8_put_header( buf, len, E1R8_TYPE_INIT, total, t );
    if (err) return err;
    off = E1R8_HEADER_SIZE;
    err = e1r8_encode_tuple_tail( buf + off, E1R8_TAIL_SIZE, t );
    if (err) return err;
    off += E1R8_TAIL_SIZE;
    e1r8_put_string( buf + off, verifier_owner, vo_len ); off += 4 + vo_len;
    e1r8_put_string( buf + off, requester, req_len ); off += 4 + req_len;
    e1r8_put_string( buf + off, image, img_len ); off += 4 + img_len;
    e1r8_put_string( buf + off, publisher, pub_len ); off += 4 + pub_len;
    return 0;
}

static inline int e1r8_decode_init( const uint8_t *buf, size_t len,
                             const struct e1r8_tuple *expected,
                             struct e1r8_init *out )
{
    size_t off;
    int err;
    const uint8_t *s;
    uint32_t slen;

    if (len < 256 || len > 2067) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_INIT, expected, NULL );
    if (err) return err;
    off = E1R8_HEADER_SIZE;
    err = e1r8_check_tuple_tail( buf + off, E1R8_TAIL_SIZE, expected );
    if (err) return err;
    out->tuple = *expected;
    off += E1R8_TAIL_SIZE;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_verifier_owner( s, slen )) return E1R8_ERR_STRING;
    out->verifier_owner = s;
    out->verifier_owner_len = slen;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_display_string( s, slen )) return E1R8_ERR_STRING;
    out->requester = s;
    out->requester_len = slen;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_display_string( s, slen )) return E1R8_ERR_STRING;
    out->image = s;
    out->image_len = slen;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_display_string( s, slen )) return E1R8_ERR_STRING;
    out->publisher = s;
    out->publisher_len = slen;
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 2: ACK (D->B). Body: binding_digest:32, request_digest:32,
 * dialog_id:16. Total 156.
 */
static inline int e1r8_encode_ack( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    int err;
    if (len != 156) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_ACK, 156, t );
    if (err) return err;
    memcpy( buf + 76, t->binding_digest, 32 );
    memcpy( buf + 108, t->request_digest, 32 );
    memcpy( buf + 140, t->dialog_id, 16 );
    return 0;
}

static inline int e1r8_decode_ack( const uint8_t *buf, size_t len,
                            const struct e1r8_tuple *expected,
                            struct e1r8_ack *out )
{
    int err;
    if (len != 156) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_ACK, expected, NULL );
    if (err) return err;
    memcpy( out->binding_digest, buf + 76, 32 );
    memcpy( out->request_digest, buf + 108, 32 );
    memcpy( out->dialog_id, buf + 140, 16 );
    if (memcmp( out->binding_digest, expected->binding_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( out->request_digest, expected->request_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( out->dialog_id, expected->dialog_id, 16 ) != 0) return E1R8_ERR_TUPLE;
    return 0;
}

/*
 * Type 3: SELECT_OK (B->D). Body: none. Total 76.
 */
static inline int e1r8_encode_select_ok( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_SELECT_OK, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_select_ok( const uint8_t *buf, size_t len,
                                  const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_SELECT_OK, expected, NULL );
}

/*
 * Type 4: SELECT (D->B). Body: account:str. Total 81..143.
 */
static inline int e1r8_encode_select( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               const uint8_t *account, uint32_t acct_len )
{
    uint32_t total;
    int err;
    if (!e1r8_valid_account_name( account, acct_len )) return E1R8_ERR_STRING;
    total = E1R8_HEADER_SIZE + (4 + acct_len);
    if (len != total) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_SELECT, total, t );
    if (err) return err;
    e1r8_put_string( buf + E1R8_HEADER_SIZE, account, acct_len );
    return 0;
}

static inline int e1r8_decode_select( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_select *out )
{
    size_t off = E1R8_HEADER_SIZE;
    const uint8_t *s;
    uint32_t slen;
    int err;
    if (len < 81 || len > 143) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_SELECT, expected, NULL );
    if (err) return err;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_account_name( s, slen )) return E1R8_ERR_STRING;
    out->account = s;
    out->account_len = slen;
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 5: READY (B->D). Body: none. Total 76.
 */
static inline int e1r8_encode_ready( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_READY, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_ready( const uint8_t *buf, size_t len,
                              const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_READY, expected, NULL );
}

/*
 * Type 6: RESULT (B->D). Body: status:u32 (1 authenticated, 2 refused),
 * account:str (empty if refused), challenge:32 (zero if refused). Total
 * 116 refused, 117..179 authenticated.
 */
static inline int e1r8_encode_result( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               uint32_t status, const uint8_t *account,
                               uint32_t acct_len, const uint8_t challenge[32] )
{
    uint32_t total;
    int err, i;
    if (status != E1R8_RESULT_AUTHENTICATED && status != E1R8_RESULT_REFUSED)
        return E1R8_ERR_ENUM;
    if (status == E1R8_RESULT_REFUSED)
    {
        if (acct_len != 0) return E1R8_ERR_REFUSED;
        for (i = 0; i < 32; ++i)
            if (challenge[i] != 0) return E1R8_ERR_REFUSED;
        total = E1R8_HEADER_SIZE + 4 + 4 + 32;
    }
    else
    {
        if (!e1r8_valid_account_name( account, acct_len )) return E1R8_ERR_STRING;
        total = E1R8_HEADER_SIZE + 4 + (4 + acct_len) + 32;
    }
    if (len != total) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_RESULT, total, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, status );
    e1r8_put_string( buf + 80, account, acct_len );
    memcpy( buf + 80 + 4 + acct_len, challenge, 32 );
    return 0;
}

static inline int e1r8_decode_result( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_result *out )
{
    size_t off = 76;
    const uint8_t *s;
    uint32_t slen;
    int err, i;
    if (len < 116 || len > 179) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_RESULT, expected, NULL );
    if (err) return err;
    out->status = e1r8_get_u32( buf + 76 );
    off = 80;
    if (out->status == E1R8_RESULT_REFUSED)
    {
        err = e1r8_get_string( buf, len, &off, &s, &slen );
        if (err) return err;
        if (slen != 0) return E1R8_ERR_REFUSED;
        out->account = s;
        out->account_len = 0;
        if (off + 32 > len) return E1R8_ERR_SHORT;
        memcpy( out->challenge, buf + off, 32 );
        off += 32;
        for (i = 0; i < 32; ++i)
            if (out->challenge[i] != 0) return E1R8_ERR_REFUSED;
    }
    else if (out->status == E1R8_RESULT_AUTHENTICATED)
    {
        err = e1r8_get_string( buf, len, &off, &s, &slen );
        if (err) return err;
        if (!e1r8_valid_account_name( s, slen )) return E1R8_ERR_STRING;
        out->account = s;
        out->account_len = slen;
        if (off + 32 > len) return E1R8_ERR_SHORT;
        memcpy( out->challenge, buf + off, 32 );
        off += 32;
    }
    else
    {
        return E1R8_ERR_ENUM;
    }
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 7: ANSWER (D->B). Body: challenge:32, one unchanged 156-byte S1
 * kind-2 record. Total 264 for Confirm/Cancel.
 */
static inline int e1r8_encode_answer( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               const uint8_t challenge[32], uint32_t answer_kind,
                               uint32_t round, uint32_t index,
                               uint64_t required_caps )
{
    int err, i;
    if (len != 264) return E1R8_ERR_LONG;
    if (answer_kind == E1R8_S1_ANSWER_CONFIRM)
    {
        if (round != 2 || index != 0) return E1R8_ERR_S1;
    }
    else if (answer_kind == E1R8_S1_ANSWER_CANCEL)
    {
        if (round != 0 || index != 0) return E1R8_ERR_S1;
        for (i = 0; i < 32; ++i)
            if (challenge[i] != 0) return E1R8_ERR_S1;
    }
    else
    {
        return E1R8_ERR_S1;
    }
    err = e1r8_put_header( buf, len, E1R8_TYPE_ANSWER, 264, t );
    if (err) return err;
    memcpy( buf + 76, challenge, 32 );
    e1r8_put_u16( buf + 108, E1R8_S1_KIND );
    e1r8_put_u16( buf + 110, E1R8_S1_VERSION );
    e1r8_put_u32( buf + 112, E1R8_S1_RECORD_SIZE );
    e1r8_put_u64( buf + 116, required_caps );
    e1r8_put_u64( buf + 124, t->request_id );
    e1r8_put_u32( buf + 132, t->attempt );
    e1r8_put_u32( buf + 136, round );
    e1r8_put_u32( buf + 140, index );
    e1r8_put_u32( buf + 144, answer_kind );
    memcpy( buf + 148, t->nonce, 32 );
    memcpy( buf + 180, t->binding_digest, 32 );
    memcpy( buf + 212, t->request_digest, 32 );
    memcpy( buf + 244, t->channel_id, 16 );
    e1r8_put_u32( buf + 260, 0 );
    return 0;
}

static inline int e1r8_decode_answer( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               uint64_t caps_mask, struct e1r8_answer *out )
{
    size_t off = 76;
    int i;
    int err;
    if (len != 264) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_ANSWER, expected, NULL );
    if (err) return err;
    memcpy( out->challenge, buf + off, 32 );
    off += 32;
    if (off + E1R8_S1_RECORD_SIZE > len) return E1R8_ERR_SHORT;
    out->kind = e1r8_get_u16( buf + off );
    out->version = e1r8_get_u16( buf + off + 2 );
    out->length = e1r8_get_u32( buf + off + 4 );
    out->required_caps = e1r8_get_u64( buf + off + 8 );
    out->request_id = e1r8_get_u64( buf + off + 16 );
    out->attempt = e1r8_get_u32( buf + off + 24 );
    out->round = e1r8_get_u32( buf + off + 28 );
    out->message_index = e1r8_get_u32( buf + off + 32 );
    out->answer_kind = e1r8_get_u32( buf + off + 36 );
    memcpy( out->nonce, buf + off + 40, 32 );
    memcpy( out->binding_digest, buf + off + 72, 32 );
    memcpy( out->request_digest, buf + off + 104, 32 );
    memcpy( out->channel_id, buf + off + 136, 16 );
    out->payload_len = e1r8_get_u32( buf + off + 152 );
    out->payload = NULL;
    off += E1R8_S1_RECORD_SIZE;

    if (out->kind != E1R8_S1_KIND) return E1R8_ERR_S1;
    if (out->version != E1R8_S1_VERSION) return E1R8_ERR_S1;
    if (out->length != E1R8_S1_RECORD_SIZE) return E1R8_ERR_S1;
    if (out->required_caps & ~caps_mask) return E1R8_ERR_S1;
    if (out->request_id != expected->request_id) return E1R8_ERR_S1;
    if (out->attempt != expected->attempt) return E1R8_ERR_S1;
    if (memcmp( out->nonce, expected->nonce, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->binding_digest, expected->binding_digest, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->request_digest, expected->request_digest, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->channel_id, expected->channel_id, 16 ) != 0) return E1R8_ERR_S1;

    if (out->answer_kind == E1R8_S1_ANSWER_CONFIRM)
    {
        if (out->round != 2) return E1R8_ERR_S1;
        if (out->message_index != 0) return E1R8_ERR_S1;
        if (out->payload_len != 0) return E1R8_ERR_S1;
    }
    else if (out->answer_kind == E1R8_S1_ANSWER_CANCEL)
    {
        if (out->round != 0) return E1R8_ERR_S1;
        if (out->message_index != 0) return E1R8_ERR_S1;
        if (out->payload_len != 0) return E1R8_ERR_S1;
        for (i = 0; i < 32; ++i)
            if (out->challenge[i] != 0) return E1R8_ERR_S1;
    }
    else
    {
        return E1R8_ERR_S1;
    }
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 8: DONE (B->D). Body: status:u32 (1 completed, 2 cancelled).
 * Total 80.
 */
static inline int e1r8_encode_done( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                             uint32_t status )
{
    int err;
    if (status != E1R8_DONE_COMPLETED && status != E1R8_DONE_CANCELLED)
        return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_DONE, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, status );
    return 0;
}

static inline int e1r8_decode_done( const uint8_t *buf, size_t len,
                             const struct e1r8_tuple *expected,
                             struct e1r8_done *out )
{
    int err = e1r8_get_header( buf, len, E1R8_TYPE_DONE, expected, NULL );
    if (err) return err;
    out->status = e1r8_get_u32( buf + 76 );
    if (out->status != E1R8_DONE_COMPLETED && out->status != E1R8_DONE_CANCELLED)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 9: ATTACH_OK (B->D). Body: none. Total 76.
 */
static inline int e1r8_encode_attach_ok( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_ATTACH_OK, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_attach_ok( const uint8_t *buf, size_t len,
                                  const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_ATTACH_OK, expected, NULL );
}

/*
 * Type 16: ENDORSED (S->V control socket). Body: tuple tail, D's pin
 * pid:u32, uid:u32; D's pidfd as SCM_RIGHTS (not in the body). Total 244.
 */
static inline int e1r8_encode_endorsed( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                 uint32_t pid, uint32_t uid )
{
    int err;
    if (len != 244) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_ENDORSED, 244, t );
    if (err) return err;
    err = e1r8_encode_tuple_tail( buf + 76, E1R8_TAIL_SIZE, t );
    if (err) return err;
    e1r8_put_u32( buf + 236, pid );
    e1r8_put_u32( buf + 240, uid );
    return 0;
}

static inline int e1r8_decode_endorsed( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected,
                                 struct e1r8_endorsed *out )
{
    int err;
    if (len != 244) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_ENDORSED, expected, NULL );
    if (err) return err;
    err = e1r8_check_tuple_tail( buf + 76, E1R8_TAIL_SIZE, expected );
    if (err) return err;
    out->tuple = *expected;
    out->pid = e1r8_get_u32( buf + 236 );
    out->uid = e1r8_get_u32( buf + 240 );
    return 0;
}

/*
 * Type 17: START (B->V S6 data). Body: account:str, account_key:16 (zero
 * for an unknown name: decoy). Total 97..159.
 */
static inline int e1r8_encode_start( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                              const uint8_t *account, uint32_t acct_len,
                              const uint8_t account_key[16] )
{
    uint32_t total;
    int err;
    if (!e1r8_valid_account_name( account, acct_len )) return E1R8_ERR_STRING;
    total = E1R8_HEADER_SIZE + (4 + acct_len) + 16;
    if (len != total) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_START, total, t );
    if (err) return err;
    e1r8_put_string( buf + 76, account, acct_len );
    memcpy( buf + 76 + 4 + acct_len, account_key, 16 );
    return 0;
}

static inline int e1r8_decode_start( const uint8_t *buf, size_t len,
                              const struct e1r8_tuple *expected,
                              struct e1r8_start *out )
{
    size_t off = 76;
    const uint8_t *s;
    uint32_t slen;
    int err;
    if (len < 97 || len > 159) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_START, expected, NULL );
    if (err) return err;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_account_name( s, slen )) return E1R8_ERR_STRING;
    out->account = s;
    out->account_len = slen;
    if (off + 16 > len) return E1R8_ERR_SHORT;
    memcpy( out->account_key, buf + off, 16 );
    off += 16;
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 18: WORKER_READY (V->B). Body: none. Total 76.
 */
static inline int e1r8_encode_worker_ready( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_WORKER_READY, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_worker_ready( const uint8_t *buf, size_t len,
                                     const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_WORKER_READY, expected, NULL );
}

/*
 * Type 19: DELEGATED (V->B). Body: none. Total 76.
 */
static inline int e1r8_encode_delegated( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_DELEGATED, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_delegated( const uint8_t *buf, size_t len,
                                  const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_DELEGATED, expected, NULL );
}

/*
 * Type 20: RESULT (V->B). Body: status:u32 (1 authenticated, 2 refused),
 * account_key:16, canonical account:str, evidence_ref:16; refused: all
 * zero/empty. Total 116 refused, 117..179 authenticated.
 */
static inline int e1r8_encode_result_v( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                 uint32_t status, const uint8_t account_key[16],
                                 const uint8_t *account, uint32_t acct_len,
                                 const uint8_t evidence_ref[16] )
{
    uint32_t total;
    int err, i;
    if (status != E1R8_RESULT_AUTHENTICATED && status != E1R8_RESULT_REFUSED)
        return E1R8_ERR_ENUM;
    if (status == E1R8_RESULT_REFUSED)
    {
        if (acct_len != 0) return E1R8_ERR_REFUSED;
        for (i = 0; i < 16; ++i)
            if (account_key[i] != 0 || evidence_ref[i] != 0) return E1R8_ERR_REFUSED;
        total = E1R8_HEADER_SIZE + 4 + 16 + 4 + 16;
    }
    else
    {
        if (!e1r8_valid_account_name( account, acct_len )) return E1R8_ERR_STRING;
        total = E1R8_HEADER_SIZE + 4 + 16 + (4 + acct_len) + 16;
    }
    if (len != total) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_RESULT_V, total, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, status );
    memcpy( buf + 80, account_key, 16 );
    e1r8_put_string( buf + 96, account, acct_len );
    memcpy( buf + 96 + 4 + acct_len, evidence_ref, 16 );
    return 0;
}

static inline int e1r8_decode_result_v( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected,
                                 struct e1r8_result_v *out )
{
    size_t off = 76;
    const uint8_t *s;
    uint32_t slen;
    int err, i;
    if (len < 116 || len > 179) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_RESULT_V, expected, NULL );
    if (err) return err;
    out->status = e1r8_get_u32( buf + 76 );
    memcpy( out->account_key, buf + 80, 16 );
    off = 96;
    if (out->status == E1R8_RESULT_REFUSED)
    {
        err = e1r8_get_string( buf, len, &off, &s, &slen );
        if (err) return err;
        if (slen != 0) return E1R8_ERR_REFUSED;
        out->account = s;
        out->account_len = 0;
        if (off + 16 > len) return E1R8_ERR_SHORT;
        memcpy( out->evidence_ref, buf + off, 16 );
        off += 16;
        for (i = 0; i < 16; ++i)
            if (out->account_key[i] != 0 || out->evidence_ref[i] != 0)
                return E1R8_ERR_REFUSED;
    }
    else if (out->status == E1R8_RESULT_AUTHENTICATED)
    {
        err = e1r8_get_string( buf, len, &off, &s, &slen );
        if (err) return err;
        if (!e1r8_valid_account_name( s, slen )) return E1R8_ERR_STRING;
        out->account = s;
        out->account_len = slen;
        if (off + 16 > len) return E1R8_ERR_SHORT;
        memcpy( out->evidence_ref, buf + off, 16 );
        off += 16;
    }
    else
    {
        return E1R8_ERR_ENUM;
    }
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 21: CANCEL (B<->V). Body: reason:u32 = 1. Total 80.
 */
static inline int e1r8_encode_cancel( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               uint32_t reason )
{
    int err;
    if (reason != E1R8_CANCEL_REASON) return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_CANCEL, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, reason );
    return 0;
}

static inline int e1r8_decode_cancel( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_cancel *out )
{
    int err = e1r8_get_header( buf, len, E1R8_TYPE_CANCEL, expected, NULL );
    if (err) return err;
    out->reason = e1r8_get_u32( buf + 76 );
    if (out->reason != E1R8_CANCEL_REASON) return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 22: W_ACK (W->V supervisor). Body: none. Total 76.
 */
static inline int e1r8_encode_w_ack( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_W_ACK, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_w_ack( const uint8_t *buf, size_t len,
                              const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_W_ACK, expected, NULL );
}

/*
 * Type 23: ATTACH_ACK (D->W secret socket). Body: none. Total 76.
 */
static inline int e1r8_encode_attach_ack( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_ATTACH_ACK, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_attach_ack( const uint8_t *buf, size_t len,
                                   const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_ATTACH_ACK, expected, NULL );
}

/*
 * Type 24: PROMPT_RELEASE (V->W supervisor). Body: none. Total 76.
 */
static inline int e1r8_encode_prompt_release( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_PROMPT_RELEASE, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_prompt_release( const uint8_t *buf, size_t len,
                                       const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_PROMPT_RELEASE, expected, NULL );
}

/*
 * Type 25: RELEASE (B->V S6 data). Body: outcome:u32 (1 completed, 2
 * cancelled). Total 80.
 */
static inline int e1r8_encode_release( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                uint32_t outcome )
{
    int err;
    if (outcome != E1R8_RELEASE_COMPLETED && outcome != E1R8_RELEASE_CANCELLED)
        return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_RELEASE, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, outcome );
    return 0;
}

static inline int e1r8_decode_release( const uint8_t *buf, size_t len,
                                const struct e1r8_tuple *expected,
                                struct e1r8_release *out )
{
    int err = e1r8_get_header( buf, len, E1R8_TYPE_RELEASE, expected, NULL );
    if (err) return err;
    out->outcome = e1r8_get_u32( buf + 76 );
    if (out->outcome != E1R8_RELEASE_COMPLETED && out->outcome != E1R8_RELEASE_CANCELLED)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 26: RELEASED (V->B). Body: none. Total 76.
 */
static inline int e1r8_encode_released( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_RELEASED, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_released( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_RELEASED, expected, NULL );
}

/*
 * Type 27: W_SETUP (supervisor->W at spawn). Body: tuple tail; D's
 * pid:u32, uid:u32; D's pidfd as SCM_RIGHTS (not in the body). Total 244.
 */
static inline int e1r8_encode_w_setup( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                uint32_t pid, uint32_t uid )
{
    int err;
    if (len != 244) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_W_SETUP, 244, t );
    if (err) return err;
    err = e1r8_encode_tuple_tail( buf + 76, E1R8_TAIL_SIZE, t );
    if (err) return err;
    e1r8_put_u32( buf + 236, pid );
    e1r8_put_u32( buf + 240, uid );
    return 0;
}

static inline int e1r8_decode_w_setup( const uint8_t *buf, size_t len,
                                const struct e1r8_tuple *expected,
                                struct e1r8_w_setup *out )
{
    int err;
    if (len != 244) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_W_SETUP, expected, NULL );
    if (err) return err;
    err = e1r8_check_tuple_tail( buf + 76, E1R8_TAIL_SIZE, expected );
    if (err) return err;
    out->tuple = *expected;
    out->pid = e1r8_get_u32( buf + 236 );
    out->uid = e1r8_get_u32( buf + 240 );
    return 0;
}

/*
 * D (INIT) and W (W_SETUP) learn the sealed tuple from the message itself:
 * they know only the request id, attempt and channel id beforehand. Build
 * the tuple from the header's nonce and the tail, then run the normal
 * decoder against it, so every header, tail and body check still applies.
 */
static inline int e1r8_learn_tuple( const uint8_t *buf, size_t len, uint64_t request_id,
                             uint32_t attempt, const uint8_t channel_id[16],
                             struct e1r8_tuple *t )
{
    if (len < E1R8_HEADER_SIZE + E1R8_TAIL_SIZE) return E1R8_ERR_SHORT;
    memset( t, 0, sizeof(*t) );
    t->request_id = e1r8_get_u64( buf + 12 );
    t->attempt = e1r8_get_u32( buf + 20 );
    memcpy( t->nonce, buf + 28, 32 );
    memcpy( t->channel_id, buf + 60, 16 );
    if (t->request_id != request_id || t->attempt != attempt ||
        memcmp( t->channel_id, channel_id, 16 ) != 0) return E1R8_ERR_ID;
    return e1r8_decode_tuple_tail( buf + E1R8_HEADER_SIZE, E1R8_TAIL_SIZE, t );
}

static inline int e1r8_decode_init_learn( const uint8_t *buf, size_t len, uint64_t request_id,
                                   uint32_t attempt, const uint8_t channel_id[16],
                                   struct e1r8_init *out )
{
    struct e1r8_tuple t;
    int err;
    if (len < 256 || len > 2067) return E1R8_ERR_LONG;
    if ((err = e1r8_learn_tuple( buf, len, request_id, attempt, channel_id, &t ))) return err;
    return e1r8_decode_init( buf, len, &t, out );
}

static inline int e1r8_decode_w_setup_learn( const uint8_t *buf, size_t len, uint64_t request_id,
                                      uint32_t attempt, const uint8_t channel_id[16],
                                      struct e1r8_w_setup *out )
{
    struct e1r8_tuple t;
    int err;
    if (len != 244) return E1R8_ERR_LONG;
    if ((err = e1r8_learn_tuple( buf, len, request_id, attempt, channel_id, &t ))) return err;
    return e1r8_decode_w_setup( buf, len, &t, out );
}

/*
 * S7 version 2 PROMPT (spec section 7 step 4): the exact 178-byte prompt.
 * 72-byte S7 header + channel_id:16, binding_digest:32, request_digest:32,
 * dialog_id:16 from the sealed tuple + ASCII "Password: " (10 bytes, no
 * NUL).
 */
static inline int e1r8_encode_s7_prompt( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_S7_PROMPT_SIZE) return E1R8_ERR_LONG;
    memcpy( buf + 0, "E1S7PAM\0", 8 );
    e1r8_put_u16( buf + 8, 2 );
    e1r8_put_u16( buf + 10, 2 );
    e1r8_put_u32( buf + 12, E1R8_S7_PROMPT_SIZE );
    e1r8_put_u64( buf + 16, t->request_id );
    e1r8_put_u32( buf + 24, t->attempt );
    e1r8_put_u32( buf + 28, 1 );
    e1r8_put_u32( buf + 32, 0 );
    e1r8_put_u32( buf + 36, 1 );
    memcpy( buf + 40, t->nonce, 32 );
    memcpy( buf + 72, t->channel_id, 16 );
    memcpy( buf + 88, t->binding_digest, 32 );
    memcpy( buf + 120, t->request_digest, 32 );
    memcpy( buf + 152, t->dialog_id, 16 );
    memcpy( buf + 168, "Password: ", 10 );
    return 0;
}

static inline int e1r8_decode_s7_prompt( const uint8_t *buf, size_t len,
                                  const struct e1r8_tuple *expected )
{
    if (len != E1R8_S7_PROMPT_SIZE) return E1R8_ERR_LONG;
    if (memcmp( buf + 0, "E1S7PAM\0", 8 ) != 0) return E1R8_ERR_MAGIC;
    if (e1r8_get_u16( buf + 8 ) != 2) return E1R8_ERR_VERSION;
    if (e1r8_get_u16( buf + 10 ) != 2) return E1R8_ERR_TYPE;
    if (e1r8_get_u32( buf + 12 ) != E1R8_S7_PROMPT_SIZE) return E1R8_ERR_LENGTH;
    if (e1r8_get_u64( buf + 16 ) != expected->request_id) return E1R8_ERR_ID;
    if (e1r8_get_u32( buf + 24 ) != expected->attempt) return E1R8_ERR_ID;
    if (e1r8_get_u32( buf + 28 ) != 1) return E1R8_ERR_ENUM;
    if (e1r8_get_u32( buf + 32 ) != 0) return E1R8_ERR_ENUM;
    if (e1r8_get_u32( buf + 36 ) != 1) return E1R8_ERR_ENUM;
    if (memcmp( buf + 40, expected->nonce, 32 ) != 0) return E1R8_ERR_ID;
    if (memcmp( buf + 72, expected->channel_id, 16 ) != 0) return E1R8_ERR_ID;
    if (memcmp( buf + 88, expected->binding_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( buf + 120, expected->request_digest, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( buf + 152, expected->dialog_id, 16 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( buf + 168, "Password: ", 10 ) != 0) return E1R8_ERR_MAGIC;
    return 0;
}

/*
 * Secret password answer (spec section 7 step 5): an S1 kind-2 record
 * with answer_kind 1, round 1, index 0 and a payload of 1..4096 bytes
 * (total 157..4252) that passes a strict RFC 3629 UTF-8 check.
 */
static inline int e1r8_encode_secret_answer( uint8_t *buf, size_t len,
                                      const struct e1r8_tuple *t,
                                      uint64_t required_caps,
                                      const uint8_t *password, uint32_t pw_len )
{
    uint32_t total;
    if (pw_len < 1 || pw_len > 4096) return E1R8_ERR_UTF8;
    if (!e1r8_valid_utf8( password, pw_len )) return E1R8_ERR_UTF8;
    total = E1R8_S1_RECORD_SIZE + pw_len;
    if (len != total) return E1R8_ERR_LONG;
    e1r8_put_u16( buf + 0, E1R8_S1_KIND );
    e1r8_put_u16( buf + 2, E1R8_S1_VERSION );
    e1r8_put_u32( buf + 4, total );
    e1r8_put_u64( buf + 8, required_caps );
    e1r8_put_u64( buf + 16, t->request_id );
    e1r8_put_u32( buf + 24, t->attempt );
    e1r8_put_u32( buf + 28, 1 );
    e1r8_put_u32( buf + 32, 0 );
    e1r8_put_u32( buf + 36, E1R8_S1_ANSWER_TEXT );
    memcpy( buf + 40, t->nonce, 32 );
    memcpy( buf + 72, t->binding_digest, 32 );
    memcpy( buf + 104, t->request_digest, 32 );
    memcpy( buf + 136, t->channel_id, 16 );
    e1r8_put_u32( buf + 152, pw_len );
    memcpy( buf + 156, password, pw_len );
    return 0;
}

static inline int e1r8_decode_secret_answer( const uint8_t *buf, size_t len,
                                      const struct e1r8_tuple *expected,
                                      uint64_t caps_mask, struct e1r8_answer *out )
{
    uint32_t pw_len;
    if (len < 157 || len > 4252) return E1R8_ERR_LONG;
    out->challenge[0] = 0;
    out->kind = e1r8_get_u16( buf + 0 );
    out->version = e1r8_get_u16( buf + 2 );
    out->length = e1r8_get_u32( buf + 4 );
    out->required_caps = e1r8_get_u64( buf + 8 );
    out->request_id = e1r8_get_u64( buf + 16 );
    out->attempt = e1r8_get_u32( buf + 24 );
    out->round = e1r8_get_u32( buf + 28 );
    out->message_index = e1r8_get_u32( buf + 32 );
    out->answer_kind = e1r8_get_u32( buf + 36 );
    memcpy( out->nonce, buf + 40, 32 );
    memcpy( out->binding_digest, buf + 72, 32 );
    memcpy( out->request_digest, buf + 104, 32 );
    memcpy( out->channel_id, buf + 136, 16 );
    pw_len = e1r8_get_u32( buf + 152 );
    out->payload_len = pw_len;
    out->payload = buf + 156;

    if (out->kind != E1R8_S1_KIND) return E1R8_ERR_S1;
    if (out->version != E1R8_S1_VERSION) return E1R8_ERR_S1;
    if (out->length != E1R8_S1_RECORD_SIZE + pw_len) return E1R8_ERR_S1;
    if (out->required_caps & ~caps_mask) return E1R8_ERR_S1;
    if (out->request_id != expected->request_id) return E1R8_ERR_S1;
    if (out->attempt != expected->attempt) return E1R8_ERR_S1;
    if (out->round != 1) return E1R8_ERR_S1;
    if (out->message_index != 0) return E1R8_ERR_S1;
    if (out->answer_kind != E1R8_S1_ANSWER_TEXT) return E1R8_ERR_S1;
    if (memcmp( out->nonce, expected->nonce, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->binding_digest, expected->binding_digest, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->request_digest, expected->request_digest, 32 ) != 0) return E1R8_ERR_S1;
    if (memcmp( out->channel_id, expected->channel_id, 16 ) != 0) return E1R8_ERR_S1;
    if (pw_len < 1 || pw_len > 4096) return E1R8_ERR_UTF8;
    if (E1R8_S1_RECORD_SIZE + pw_len != len) return E1R8_ERR_LONG;
    if (!e1r8_valid_utf8( out->payload, pw_len )) return E1R8_ERR_UTF8;
    return 0;
}

#endif /* WEXELEVATE_E1R8_CODEC_H */
