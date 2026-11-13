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
/* R9 (docs/E1_S18E_R9_SPEC.md): the grant held by V, and the A<->B0 channel. */
#define E1R8_TYPE_GRANT             32
#define E1R8_TYPE_GRANTED           33
#define E1R8_TYPE_REQUEST           40
#define E1R8_TYPE_DECISION          41
#define E1R8_TYPE_ENDORSED_LOCAL    42
#define E1R8_TYPE_TO_D              43
#define E1R8_TYPE_FROM_D            44
#define E1R8_TYPE_RECHECK           45
#define E1R8_TYPE_RECHECKED         46
#define E1R8_TYPE_LAUNCH            47
#define E1R8_TYPE_LAUNCHED          48
#define E1R8_TYPE_CANCEL_R9         49
#define E1R8_TYPE_END               50
#define E1R8_TYPE_NEXT_ATTEMPT      51

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
/* R9 enum values, per docs/E1_S18E_R9_SPEC.md section 4. */
#define E1R8_REQUEST_KIND_RUNAS     1
#define E1R8_REQUEST_KIND_MANIFEST  2
#define E1R8_DECISION_PROMPT        1
#define E1R8_DECISION_DENY          2
#define E1R8_DECISION_REASON_POLICY     1
#define E1R8_DECISION_REASON_ATTEMPTS   2
#define E1R8_DECISION_REASON_BUSY       3
#define E1R8_ENDORSED_LOCAL_ENDORSED  1
#define E1R8_ENDORSED_LOCAL_REFUSED   2
#define E1R8_RECHECK_STEP_ATTACH      1
#define E1R8_RECHECK_STEP_READY       2
#define E1R8_RECHECK_STEP_RESULT      3
#define E1R8_RECHECK_STEP_CONFIRM     4
#define E1R8_RECHECKED_OK             1
#define E1R8_RECHECKED_REFUSED        2
#define E1R8_LAUNCHED_COMMITTED       1
#define E1R8_LAUNCHED_FAILED_BEFORE   2
#define E1R8_LAUNCHED_FAILED_AFTER    3
#define E1R8_CANCEL_R9_USER           1
#define E1R8_CANCEL_R9_D_GONE         2
#define E1R8_CANCEL_R9_DEADLINE       3
#define E1R8_CANCEL_R9_PEER_GONE      4
#define E1R8_CANCEL_R9_PROTOCOL       5
#define E1R8_CANCEL_R9_RECHECK        6
#define E1R8_END_COMPLETED            1
#define E1R8_END_CANCELLED            2
#define E1R8_END_DENIED               3

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
 * R9 message body output structures (docs/E1_S18E_R9_SPEC.md section 4).
 * String fields point into the decoded buffer; the caller must not free
 * them.
 */
struct e1r8_request
{
    uint32_t kind;
    uint64_t deadline;
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    uint64_t root_gen;
    uint64_t server_gen;
    uint64_t broker_gen;
    uint64_t verifier_gen;
    uint64_t map_gen;
    uint64_t policy_gen;
    uint8_t desktop_route[16];
    uint32_t secure_desktop;
    const uint8_t *requester;
    uint32_t requester_len;
    const uint8_t *image;
    uint32_t image_len;
    const uint8_t *publisher;
    uint32_t publisher_len;
};

struct e1r8_decision
{
    uint32_t status;
    struct e1r8_tuple tuple;  /* prompt only */
    uint32_t reason;          /* deny only */
};

struct e1r8_endorsed_local
{
    uint8_t dialog_id[16];
    uint32_t status;
};

struct e1r8_to_d
{
    uint16_t type;
    const uint8_t *frame;
    uint32_t frame_len;
};

struct e1r8_from_d
{
    uint16_t type;
    const uint8_t *frame;
    uint32_t frame_len;
};

struct e1r8_recheck
{
    uint32_t step;
};

struct e1r8_rechecked
{
    uint32_t step;
    uint32_t status;
};

struct e1r8_launch
{
    const uint8_t *account;
    uint32_t account_len;
    uint8_t evidence_ref[16];
};

struct e1r8_launched
{
    uint32_t status;
    uint32_t win32;
};

struct e1r8_cancel_r9
{
    uint32_t reason;
};

struct e1r8_end
{
    uint32_t outcome;
};

struct e1r8_next_attempt
{
    struct e1r8_tuple tuple;
};

struct e1r8_grant
{
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
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
 * R9 helpers (docs/E1_S18E_R9_SPEC.md).
 */

/* True when a 16-byte field is all zero. */
static inline int e1r8_is_zero16( const uint8_t *p )
{
    int i;
    for (i = 0; i < 16; ++i)
        if (p[i] != 0) return 0;
    return 1;
}

/*
 * Verify a pre-endorsement tuple against the expected sealed tuple
 * (dialog_id is not part of the pre-endorsement tuple).
 */
static inline int e1r8_check_pre_endorsement( const uint8_t *buf, size_t len,
                                      const struct e1r8_tuple *expected )
{
    struct e1r8_tuple t;
    int err = e1r8_decode_pre_endorsement_tuple( buf, len, &t );
    if (err) return err;
    if (t.request_id != expected->request_id) return E1R8_ERR_TUPLE;
    if (t.attempt != expected->attempt) return E1R8_ERR_TUPLE;
    if (memcmp( t.nonce, expected->nonce, 32 ) != 0) return E1R8_ERR_TUPLE;
    if (memcmp( t.channel_id, expected->channel_id, 16 ) != 0) return E1R8_ERR_TUPLE;
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
    return 0;
}

/*
 * The embedded frame of a TO_D (type 43): one of the R8 B->D types, within
 * that type's R8 length bounds.
 */
static inline int e1r8_valid_to_d_frame( uint16_t type, uint32_t len )
{
    if (type == E1R8_TYPE_INIT) return (len >= 256 && len <= 2067);
    if (type == E1R8_TYPE_SELECT_OK) return (len == E1R8_HEADER_SIZE);
    if (type == E1R8_TYPE_ATTACH_OK) return (len == E1R8_HEADER_SIZE);
    if (type == E1R8_TYPE_READY) return (len == E1R8_HEADER_SIZE);
    if (type == E1R8_TYPE_RESULT) return (len >= 116 && len <= 179);
    if (type == E1R8_TYPE_DONE) return (len == 80);
    /* The lockout notice (WO-39), sent in place of ATTACH_OK: header only. */
    if (type == E1R8_TYPE_LOCKED) return (len == E1R8_HEADER_SIZE);
    return 0;
}

/*
 * The embedded frame of a FROM_D (type 44): one of the R8 D->B types,
 * within that type's R8 length bounds.
 */
static inline int e1r8_valid_from_d_frame( uint16_t type, uint32_t len )
{
    if (type == E1R8_TYPE_ACK) return (len == 156);
    if (type == E1R8_TYPE_SELECT) return (len >= 81 && len <= 143);
    if (type == E1R8_TYPE_ANSWER) return (len == 264);
    return 0;
}

/*
 * Check the embedded frame's header (magic, version, type, length,
 * reserved) and its type/length bounds. Only the header is checked, not
 * the embedded body.
 */
static inline int e1r8_check_embedded( const uint8_t *frame, uint32_t frame_len,
                                  uint16_t *out_type, int (*valid)( uint16_t, uint32_t ) )
{
    uint16_t type;
    uint32_t len;
    if (frame_len < E1R8_HEADER_SIZE) return E1R8_ERR_SHORT;
    if (memcmp( frame, "E1R8", 4 ) != 0) return E1R8_ERR_MAGIC;
    if (e1r8_get_u16( frame + 4 ) != 1) return E1R8_ERR_VERSION;
    type = e1r8_get_u16( frame + 6 );
    len = e1r8_get_u32( frame + 8 );
    if (len != frame_len) return E1R8_ERR_LENGTH;
    if (e1r8_get_u32( frame + 24 ) != 0) return E1R8_ERR_RESERVED;
    if (!valid( type, len )) return E1R8_ERR_TYPE;
    *out_type = type;
    return 0;
}

/*
 * Type 32: GRANT (B0->V on B0's channel). Body: account_key:16,
 * evidence_ref:16, both nonzero. Total 108.
 */
static inline int e1r8_encode_grant( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                              const uint8_t account_key[16],
                              const uint8_t evidence_ref[16] )
{
    int err;
    if (e1r8_is_zero16( account_key ) || e1r8_is_zero16( evidence_ref ))
        return E1R8_ERR_ID;
    if (len != 108) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_GRANT, 108, t );
    if (err) return err;
    memcpy( buf + 76, account_key, 16 );
    memcpy( buf + 92, evidence_ref, 16 );
    return 0;
}

static inline int e1r8_decode_grant( const uint8_t *buf, size_t len,
                              const struct e1r8_tuple *expected,
                              struct e1r8_grant *out )
{
    int err;
    if (len != 108) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_GRANT, expected, NULL );
    if (err) return err;
    memcpy( out->account_key, buf + 76, 16 );
    memcpy( out->evidence_ref, buf + 92, 16 );
    if (e1r8_is_zero16( out->account_key ) || e1r8_is_zero16( out->evidence_ref ))
        return E1R8_ERR_ID;
    return 0;
}

/*
 * Type 33: GRANTED (V->B0). Body: none. Total 76.
 */
static inline int e1r8_encode_granted( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_put_header( buf, len, E1R8_TYPE_GRANTED, E1R8_HEADER_SIZE, t );
}

static inline int e1r8_decode_granted( const uint8_t *buf, size_t len,
                                const struct e1r8_tuple *expected )
{
    if (len != E1R8_HEADER_SIZE) return E1R8_ERR_LONG;
    return e1r8_get_header( buf, len, E1R8_TYPE_GRANTED, expected, NULL );
}

/*
 * Type 40: REQUEST (A->B0). Body: kind:u32 (1 runas, 2 manifest),
 * deadline:u64, binding_digest:32, request_digest:32, root_gen:u64,
 * server_gen:u64, broker_gen:u64, verifier_gen:u64, map_gen:u64,
 * policy_gen:u64, desktop_route:16, secure_desktop:u32 (0 or 1),
 * requester:str, image:str, publisher:str (display strings). Total
 * 232..1792. The header's attempt is 0 and its channel_id is zero; the
 * nonce is the request's.
 */
static inline int e1r8_encode_request( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                uint32_t kind, uint32_t secure_desktop,
                                const uint8_t *requester, uint32_t req_len,
                                const uint8_t *image, uint32_t img_len,
                                const uint8_t *publisher, uint32_t pub_len )
{
    struct e1r8_tuple h;
    uint32_t total;
    size_t off;
    int err;

    if (kind != E1R8_REQUEST_KIND_RUNAS && kind != E1R8_REQUEST_KIND_MANIFEST)
        return E1R8_ERR_ENUM;
    if (secure_desktop != 0 && secure_desktop != 1) return E1R8_ERR_ENUM;
    if (!e1r8_valid_display_string( requester, req_len )) return E1R8_ERR_STRING;
    if (!e1r8_valid_display_string( image, img_len )) return E1R8_ERR_STRING;
    if (!e1r8_valid_display_string( publisher, pub_len )) return E1R8_ERR_STRING;

    total = E1R8_HEADER_SIZE + 4 + 8 + 32 + 32 + 6 * 8 + 16 + 4 +
            (4 + req_len) + (4 + img_len) + (4 + pub_len);
    if (len != total) return E1R8_ERR_LONG;

    /* The header's attempt is 0 and its channel_id is zero; the nonce is the request's. */
    h = *t;
    h.attempt = 0;
    memset( h.channel_id, 0, 16 );
    err = e1r8_put_header( buf, len, E1R8_TYPE_REQUEST, total, &h );
    if (err) return err;
    off = E1R8_HEADER_SIZE;
    e1r8_put_u32( buf + off, kind ); off += 4;
    e1r8_put_u64( buf + off, t->deadline ); off += 8;
    memcpy( buf + off, t->binding_digest, 32 ); off += 32;
    memcpy( buf + off, t->request_digest, 32 ); off += 32;
    e1r8_put_u64( buf + off, t->root_gen ); off += 8;
    e1r8_put_u64( buf + off, t->server_gen ); off += 8;
    e1r8_put_u64( buf + off, t->broker_gen ); off += 8;
    e1r8_put_u64( buf + off, t->verifier_gen ); off += 8;
    e1r8_put_u64( buf + off, t->map_gen ); off += 8;
    e1r8_put_u64( buf + off, t->policy_gen ); off += 8;
    memcpy( buf + off, t->desktop_route, 16 ); off += 16;
    e1r8_put_u32( buf + off, secure_desktop ); off += 4;
    e1r8_put_string( buf + off, requester, req_len ); off += 4 + req_len;
    e1r8_put_string( buf + off, image, img_len ); off += 4 + img_len;
    e1r8_put_string( buf + off, publisher, pub_len ); off += 4 + pub_len;
    return 0;
}

static inline int e1r8_decode_request( const uint8_t *buf, size_t len,
                                const struct e1r8_tuple *expected,
                                struct e1r8_request *out )
{
    struct e1r8_tuple h;
    size_t off;
    const uint8_t *s;
    uint32_t slen;
    int err;

    if (len < 232 || len > 1792) return E1R8_ERR_LONG;
    /* The header's attempt is 0 and its channel_id is zero; the nonce is the request's. */
    h = *expected;
    h.attempt = 0;
    memset( h.channel_id, 0, 16 );
    err = e1r8_get_header( buf, len, E1R8_TYPE_REQUEST, &h, NULL );
    if (err) return err;
    off = E1R8_HEADER_SIZE;
    out->kind = e1r8_get_u32( buf + off ); off += 4;
    if (out->kind != E1R8_REQUEST_KIND_RUNAS && out->kind != E1R8_REQUEST_KIND_MANIFEST)
        return E1R8_ERR_ENUM;
    out->deadline = e1r8_get_u64( buf + off ); off += 8;
    memcpy( out->binding_digest, buf + off, 32 ); off += 32;
    memcpy( out->request_digest, buf + off, 32 ); off += 32;
    out->root_gen = e1r8_get_u64( buf + off ); off += 8;
    out->server_gen = e1r8_get_u64( buf + off ); off += 8;
    out->broker_gen = e1r8_get_u64( buf + off ); off += 8;
    out->verifier_gen = e1r8_get_u64( buf + off ); off += 8;
    out->map_gen = e1r8_get_u64( buf + off ); off += 8;
    out->policy_gen = e1r8_get_u64( buf + off ); off += 8;
    memcpy( out->desktop_route, buf + off, 16 ); off += 16;
    out->secure_desktop = e1r8_get_u32( buf + off ); off += 4;
    if (out->secure_desktop != 0 && out->secure_desktop != 1) return E1R8_ERR_ENUM;
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
 * Type 41: DECISION (B0->A). Body: status:u32 (1 prompt, 2 deny), then
 * for 1 the pre-endorsement tuple (204), for 2 reason:u32 (1 policy, 2
 * attempts, 3 busy). Total 284 prompt, 84 deny. For prompt the header's
 * request_id, attempt, nonce and channel_id equal the tuple's; for deny
 * the header's attempt is 0 and its nonce and channel_id are zero.
 */
static inline int e1r8_encode_decision( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                 uint32_t status, uint32_t reason )
{
    struct e1r8_tuple h;
    int err;

    if (status == E1R8_DECISION_PROMPT)
    {
        if (len != 284) return E1R8_ERR_LONG;
        err = e1r8_put_header( buf, len, E1R8_TYPE_DECISION, 284, t );
        if (err) return err;
        e1r8_put_u32( buf + 76, status );
        err = e1r8_encode_pre_endorsement_tuple( buf + 80, E1R8_PRE_ENDORSEMENT_SIZE, t );
        if (err) return err;
        return 0;
    }
    else if (status == E1R8_DECISION_DENY)
    {
        if (reason != E1R8_DECISION_REASON_POLICY &&
            reason != E1R8_DECISION_REASON_ATTEMPTS &&
            reason != E1R8_DECISION_REASON_BUSY)
            return E1R8_ERR_ENUM;
        if (len != 84) return E1R8_ERR_LONG;
        h = *t;
        h.attempt = 0;
        memset( h.channel_id, 0, 16 );
        err = e1r8_put_header( buf, len, E1R8_TYPE_DECISION, 84, &h );
        if (err) return err;
        e1r8_put_u32( buf + 76, status );
        e1r8_put_u32( buf + 80, reason );
        return 0;
    }
    return E1R8_ERR_ENUM;
}

static inline int e1r8_decode_decision( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected,
                                 struct e1r8_decision *out )
{
    struct e1r8_tuple h;
    uint32_t status;
    int err;

    if (len < 84 || len > 284) return E1R8_ERR_LONG;
    status = e1r8_get_u32( buf + 76 );
    if (status == E1R8_DECISION_PROMPT)
    {
        if (len != 284) return E1R8_ERR_LONG;
        err = e1r8_get_header( buf, len, E1R8_TYPE_DECISION, expected, NULL );
        if (err) return err;
        err = e1r8_check_pre_endorsement( buf + 80, E1R8_PRE_ENDORSEMENT_SIZE, expected );
        if (err) return err;
        out->status = status;
        out->tuple = *expected;
        return 0;
    }
    else if (status == E1R8_DECISION_DENY)
    {
        if (len != 84) return E1R8_ERR_LONG;
        h = *expected;
        h.attempt = 0;
        memset( h.channel_id, 0, 16 );
        err = e1r8_get_header( buf, len, E1R8_TYPE_DECISION, &h, NULL );
        if (err) return err;
        out->status = status;
        out->reason = e1r8_get_u32( buf + 80 );
        if (out->reason != E1R8_DECISION_REASON_POLICY &&
            out->reason != E1R8_DECISION_REASON_ATTEMPTS &&
            out->reason != E1R8_DECISION_REASON_BUSY)
            return E1R8_ERR_ENUM;
        return 0;
    }
    return E1R8_ERR_ENUM;
}

/*
 * Type 42: ENDORSED_LOCAL (A->B0). Body: dialog_id:16, status:u32
 * (1 endorsed, 2 refused). Total 96.
 */
static inline int e1r8_encode_endorsed_local( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                      const uint8_t dialog_id[16], uint32_t status )
{
    int err;
    if (status != E1R8_ENDORSED_LOCAL_ENDORSED && status != E1R8_ENDORSED_LOCAL_REFUSED)
        return E1R8_ERR_ENUM;
    if (len != 96) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_ENDORSED_LOCAL, 96, t );
    if (err) return err;
    memcpy( buf + 76, dialog_id, 16 );
    e1r8_put_u32( buf + 92, status );
    return 0;
}

static inline int e1r8_decode_endorsed_local( const uint8_t *buf, size_t len,
                                      const struct e1r8_tuple *expected,
                                      struct e1r8_endorsed_local *out )
{
    int err;
    if (len != 96) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_ENDORSED_LOCAL, expected, NULL );
    if (err) return err;
    memcpy( out->dialog_id, buf + 76, 16 );
    out->status = e1r8_get_u32( buf + 92 );
    if (out->status != E1R8_ENDORSED_LOCAL_ENDORSED && out->status != E1R8_ENDORSED_LOCAL_REFUSED)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 43: TO_D (B0->A). Body: one complete R8 B->D frame (types 1, 3,
 * 9, 5, 6, 8). Only the embedded header is checked, not the embedded
 * body. Total 152..2143.
 */
static inline int e1r8_encode_to_d( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                             const uint8_t *frame, uint32_t frame_len )
{
    uint16_t type;
    int err;
    err = e1r8_check_embedded( frame, frame_len, &type, e1r8_valid_to_d_frame );
    if (err) return err;
    if (len != (size_t)E1R8_HEADER_SIZE + frame_len) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_TO_D, E1R8_HEADER_SIZE + frame_len, t );
    if (err) return err;
    memcpy( buf + E1R8_HEADER_SIZE, frame, frame_len );
    return 0;
}

static inline int e1r8_decode_to_d( const uint8_t *buf, size_t len,
                             const struct e1r8_tuple *expected,
                             struct e1r8_to_d *out )
{
    int err;
    if (len < 152 || len > 2143) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_TO_D, expected, NULL );
    if (err) return err;
    err = e1r8_check_embedded( buf + E1R8_HEADER_SIZE, (uint32_t)(len - E1R8_HEADER_SIZE),
                               &out->type, e1r8_valid_to_d_frame );
    if (err) return err;
    out->frame = buf + E1R8_HEADER_SIZE;
    out->frame_len = (uint32_t)(len - E1R8_HEADER_SIZE);
    return 0;
}

/*
 * Type 44: FROM_D (A->B0). Body: one complete R8 D->B frame (types 2,
 * 4, 7) as read from D. Only the embedded header is checked, not the
 * embedded body. Total 157..340.
 */
static inline int e1r8_encode_from_d( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               const uint8_t *frame, uint32_t frame_len )
{
    uint16_t type;
    int err;
    err = e1r8_check_embedded( frame, frame_len, &type, e1r8_valid_from_d_frame );
    if (err) return err;
    if (len != (size_t)E1R8_HEADER_SIZE + frame_len) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_FROM_D, E1R8_HEADER_SIZE + frame_len, t );
    if (err) return err;
    memcpy( buf + E1R8_HEADER_SIZE, frame, frame_len );
    return 0;
}

static inline int e1r8_decode_from_d( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_from_d *out )
{
    int err;
    if (len < 157 || len > 340) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_FROM_D, expected, NULL );
    if (err) return err;
    err = e1r8_check_embedded( buf + E1R8_HEADER_SIZE, (uint32_t)(len - E1R8_HEADER_SIZE),
                               &out->type, e1r8_valid_from_d_frame );
    if (err) return err;
    out->frame = buf + E1R8_HEADER_SIZE;
    out->frame_len = (uint32_t)(len - E1R8_HEADER_SIZE);
    return 0;
}

/*
 * Type 45: RECHECK (B0->A). Body: step:u32 (1 attach, 2 ready, 3 result,
 * 4 confirm). Total 80.
 */
static inline int e1r8_encode_recheck( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               uint32_t step )
{
    int err;
    if (step < E1R8_RECHECK_STEP_ATTACH || step > E1R8_RECHECK_STEP_CONFIRM)
        return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_RECHECK, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, step );
    return 0;
}

static inline int e1r8_decode_recheck( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_recheck *out )
{
    int err;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_RECHECK, expected, NULL );
    if (err) return err;
    out->step = e1r8_get_u32( buf + 76 );
    if (out->step < E1R8_RECHECK_STEP_ATTACH || out->step > E1R8_RECHECK_STEP_CONFIRM)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 46: RECHECKED (A->B0). Body: step:u32 (1..4), status:u32
 * (1 ok, 2 refused). Total 84.
 */
static inline int e1r8_encode_rechecked( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                 uint32_t step, uint32_t status )
{
    int err;
    if (step < E1R8_RECHECK_STEP_ATTACH || step > E1R8_RECHECK_STEP_CONFIRM)
        return E1R8_ERR_ENUM;
    if (status != E1R8_RECHECKED_OK && status != E1R8_RECHECKED_REFUSED)
        return E1R8_ERR_ENUM;
    if (len != 84) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_RECHECKED, 84, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, step );
    e1r8_put_u32( buf + 80, status );
    return 0;
}

static inline int e1r8_decode_rechecked( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected,
                                 struct e1r8_rechecked *out )
{
    int err;
    if (len != 84) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_RECHECKED, expected, NULL );
    if (err) return err;
    out->step = e1r8_get_u32( buf + 76 );
    out->status = e1r8_get_u32( buf + 80 );
    if (out->step < E1R8_RECHECK_STEP_ATTACH || out->step > E1R8_RECHECK_STEP_CONFIRM)
        return E1R8_ERR_ENUM;
    if (out->status != E1R8_RECHECKED_OK && out->status != E1R8_RECHECKED_REFUSED)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 47: LAUNCH (B0->A). Body: account:str (canonical), evidence_ref:16.
 * Total 97..159.
 */
static inline int e1r8_encode_launch( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                               const uint8_t *account, uint32_t acct_len,
                               const uint8_t evidence_ref[16] )
{
    uint32_t total;
    int err;
    if (!e1r8_valid_account_name( account, acct_len )) return E1R8_ERR_STRING;
    total = E1R8_HEADER_SIZE + (4 + acct_len) + 16;
    if (len != total) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_LAUNCH, total, t );
    if (err) return err;
    e1r8_put_string( buf + 76, account, acct_len );
    memcpy( buf + 76 + 4 + acct_len, evidence_ref, 16 );
    return 0;
}

static inline int e1r8_decode_launch( const uint8_t *buf, size_t len,
                               const struct e1r8_tuple *expected,
                               struct e1r8_launch *out )
{
    size_t off = 76;
    const uint8_t *s;
    uint32_t slen;
    int err;
    if (len < 97 || len > 159) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_LAUNCH, expected, NULL );
    if (err) return err;
    err = e1r8_get_string( buf, len, &off, &s, &slen );
    if (err) return err;
    if (!e1r8_valid_account_name( s, slen )) return E1R8_ERR_STRING;
    out->account = s;
    out->account_len = slen;
    if (off + 16 > len) return E1R8_ERR_SHORT;
    memcpy( out->evidence_ref, buf + off, 16 );
    off += 16;
    if (off != len) return E1R8_ERR_LONG;
    return 0;
}

/*
 * Type 48: LAUNCHED (A->B0). Body: status:u32 (1 committed, 2 failed
 * before commit, 3 failed after commit), win32:u32. Total 84.
 */
static inline int e1r8_encode_launched( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                uint32_t status, uint32_t win32 )
{
    int err;
    if (status != E1R8_LAUNCHED_COMMITTED &&
        status != E1R8_LAUNCHED_FAILED_BEFORE &&
        status != E1R8_LAUNCHED_FAILED_AFTER)
        return E1R8_ERR_ENUM;
    if (len != 84) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_LAUNCHED, 84, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, status );
    e1r8_put_u32( buf + 80, win32 );
    return 0;
}

static inline int e1r8_decode_launched( const uint8_t *buf, size_t len,
                                const struct e1r8_tuple *expected,
                                struct e1r8_launched *out )
{
    int err;
    if (len != 84) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_LAUNCHED, expected, NULL );
    if (err) return err;
    out->status = e1r8_get_u32( buf + 76 );
    out->win32 = e1r8_get_u32( buf + 80 );
    if (out->status != E1R8_LAUNCHED_COMMITTED &&
        out->status != E1R8_LAUNCHED_FAILED_BEFORE &&
        out->status != E1R8_LAUNCHED_FAILED_AFTER)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 49: CANCEL (A<->B0). Body: reason:u32 (1 user, 2 D gone, 3
 * deadline, 4 peer gone, 5 protocol, 6 recheck). Total 80. Distinct from
 * R8's type 21 CANCEL.
 */
static inline int e1r8_encode_cancel_r9( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                                 uint32_t reason )
{
    int err;
    if (reason < E1R8_CANCEL_R9_USER || reason > E1R8_CANCEL_R9_RECHECK)
        return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_CANCEL_R9, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, reason );
    return 0;
}

static inline int e1r8_decode_cancel_r9( const uint8_t *buf, size_t len,
                                 const struct e1r8_tuple *expected,
                                 struct e1r8_cancel_r9 *out )
{
    int err;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_CANCEL_R9, expected, NULL );
    if (err) return err;
    out->reason = e1r8_get_u32( buf + 76 );
    if (out->reason < E1R8_CANCEL_R9_USER || out->reason > E1R8_CANCEL_R9_RECHECK)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 50: END (B0->A). Body: outcome:u32 (1 completed, 2 cancelled,
 * 3 denied). Total 80.
 */
static inline int e1r8_encode_end( uint8_t *buf, size_t len, const struct e1r8_tuple *t,
                             uint32_t outcome )
{
    int err;
    if (outcome != E1R8_END_COMPLETED && outcome != E1R8_END_CANCELLED &&
        outcome != E1R8_END_DENIED)
        return E1R8_ERR_ENUM;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_END, 80, t );
    if (err) return err;
    e1r8_put_u32( buf + 76, outcome );
    return 0;
}

static inline int e1r8_decode_end( const uint8_t *buf, size_t len,
                             const struct e1r8_tuple *expected,
                             struct e1r8_end *out )
{
    int err;
    if (len != 80) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_END, expected, NULL );
    if (err) return err;
    out->outcome = e1r8_get_u32( buf + 76 );
    if (out->outcome != E1R8_END_COMPLETED && out->outcome != E1R8_END_CANCELLED &&
        out->outcome != E1R8_END_DENIED)
        return E1R8_ERR_ENUM;
    return 0;
}

/*
 * Type 51: NEXT_ATTEMPT (B0->A). Body: the next attempt's
 * pre-endorsement tuple (204). Total 280. The header's request_id,
 * attempt, nonce and channel_id equal the tuple's; the attempt is 2 or 3.
 */
static inline int e1r8_encode_next_attempt( uint8_t *buf, size_t len, const struct e1r8_tuple *t )
{
    int err;
    if (t->attempt != 2 && t->attempt != 3) return E1R8_ERR_ENUM;
    if (len != 280) return E1R8_ERR_LONG;
    err = e1r8_put_header( buf, len, E1R8_TYPE_NEXT_ATTEMPT, 280, t );
    if (err) return err;
    err = e1r8_encode_pre_endorsement_tuple( buf + 76, E1R8_PRE_ENDORSEMENT_SIZE, t );
    if (err) return err;
    return 0;
}

static inline int e1r8_decode_next_attempt( const uint8_t *buf, size_t len,
                                     const struct e1r8_tuple *expected,
                                     struct e1r8_next_attempt *out )
{
    struct e1r8_header h;
    int err;
    if (len != 280) return E1R8_ERR_LONG;
    err = e1r8_get_header( buf, len, E1R8_TYPE_NEXT_ATTEMPT, expected, &h );
    if (err) return err;
    if (h.attempt != 2 && h.attempt != 3) return E1R8_ERR_ENUM;
    err = e1r8_check_pre_endorsement( buf + 76, E1R8_PRE_ENDORSEMENT_SIZE, expected );
    if (err) return err;
    out->tuple = *expected;
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
