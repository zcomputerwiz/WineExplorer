/*
 * Disabled E1-S5 broker records. Internal logical records, NOT a public/wire ABI.
 * Only the disposable test broker includes this until the remaining E1-S gates exist.
 * Copyright 2026 WineExplorer contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef WEXELEVATE_E1S_H
#define WEXELEVATE_E1S_H
#ifdef WEXELEVATE_TEST_APPROVAL

/* WO-14: the pure byte encoding (e1s_bytes, e1s_put, e1s_uint, e1s_blob,
 * e1s_string, and the binding/request field order) lives in e1s_encode.h,
 * shared with the S18 standard-user branch. BCrypt and IsValidSid stay here. */
#include "e1s_encode.h"

#define E1S_PROBE_MAGIC 0x35533145u
#define E1S_MAX_RECORD (1024 * 1024)
#define E1S_CAPS 0x3ffu
#define E1S_EVIDENCE_LENGTH 220
#define E1S_ANSWER_LENGTH 156 /* plus payload, which the broker never receives */

enum e1s_probe_case
{
    E1S_ACCEPT = 1, E1S_REQUEST_ID, E1S_NONCE, E1S_DIGEST, E1S_REPLAY,
    E1S_EXPIRED, E1S_TOKEN_REPLACED, E1S_ACCOUNT, E1S_CANCEL,
    E1S_BINDING, E1S_SEQUENCE, E1S_GENERATION, E1S_LOGON,
    E1S_EXPIRY_CONSUME, E1S_EXPIRY_COMMIT, E1S_TOKEN_COMMIT, E1S_VERSION,
    E1S_REDEMPTION, E1S_MUTATED_REQUEST,
    E1S_TRANSPORT_ACCEPT, E1S_TRANSPORT_STALE, E1S_TRANSPORT_REPLAY,
    E1S_TRANSPORT_LATE, E1S_TRANSPORT_OWNER, E1S_TRANSPORT_DISCONNECT,
    E1S_TRANSPORT_CANCEL,
    E1S_TRANSPORT_CHILD_IMPOSTOR,
    E1S_CASE_COUNT
};

#ifndef WEXELEVATE_E1S_PROBE_CLIENT
enum e1s_state
{
    E1S_QUEUED, E1S_PROMPTING, E1S_AUTHENTICATING, E1S_IDENTITY_READY,
    E1S_CONSUMED, E1S_LAUNCH_PREPARED, E1S_COMMITTED, E1S_CANCELLED
};

struct e1s_generations
{
    UINT64 root, server, broker, verifier, map, policy;
};

/* These fields must eventually come from authenticated, pinned authorities.
 * H2/S3 supplies the token fields/SID below, but not these endpoint identities.
 * An ordinary request must never fill this structure. S5 has only lab fixtures. */
struct e1s_context
{
    struct e1s_generations gen;
    BYTE prefix[16], host_process[16], opener[16], writer[16], desktop[16], channel[16];
    UINT64 host_uid;
};

struct e1s_binding
{
    UINT64 token_id, modified_id, auth_id, start_time;
    DWORD session, sid_len;
    BYTE sid[SECURITY_MAX_SID_SIZE];
};

struct e1s_frozen
{
    UINT64 id, deadline;
    BYTE binding_digest[32], request_digest[32], environment_digest[32];
    BYTE *bytes; /* owned, exact canonical request bytes, including copied strings */
    DWORD length;
};

struct e1s_attempt
{
    const struct e1s_frozen *frozen;
    struct e1s_context context;
    enum e1s_state state;
    DWORD number, round, index;
    BYTE nonce[32], account[16], evidence_ref[16];
    BOOL consent, redeemed;
    UINT64 evidence_completed, evidence_expiry;
    UINT64 admin_auth_id; /* never overwrite the caller's AuthenticationId */
};

struct e1s_answer
{
    WORD kind, version;
    DWORD length;
    UINT64 caps, id;
    DWORD attempt, round, index, answer_kind;
    BYTE nonce[32], binding_digest[32], request_digest[32], channel[16];
    DWORD payload_len; /* broker channel permits only empty consent/cancel */
};

struct e1s_evidence
{
    WORD kind, version;
    DWORD length;
    UINT64 caps, id;
    BYTE request_digest[32], binding_digest[32], nonce[32];
    DWORD attempt;
    BYTE account[16];
    UINT64 uid, completed, expiry;
    struct e1s_generations gen;
    DWORD method, service;
};

static BOOL e1s_nonzero( const BYTE *p, DWORD len )
{
    DWORD i;
    BYTE v = 0;
    for (i = 0; i < len; ++i) v |= p[i];
    return v != 0;
}

static BOOL e1s_sha256( const char *tag, DWORD tag_len, const BYTE *data, DWORD len, BYTE out[32] )
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BOOL ok = FALSE;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) &&
        NT_SUCCESS(BCryptCreateHash( alg, &hash, NULL, 0, NULL, 0, 0 )) &&
        NT_SUCCESS(BCryptHashData( hash, (BYTE *)tag, tag_len, 0 )) &&
        NT_SUCCESS(BCryptHashData( hash, (BYTE *)data, len, 0 )) &&
        NT_SUCCESS(BCryptFinishHash( hash, out, 32, 0 ))) ok = TRUE;
    if (hash) BCryptDestroyHash( hash );
    if (alg) BCryptCloseAlgorithmProvider( alg, 0 );
    return ok;
}

static BOOL e1s_generations_valid( const struct e1s_generations *g )
{
    return g->root && g->server && g->broker && g->verifier && g->map && g->policy;
}

static BOOL e1s_same_generations( const struct e1s_generations *a, const struct e1s_generations *b )
{
    return a->root == b->root && a->server == b->server && a->broker == b->broker &&
           a->verifier == b->verifier && a->map == b->map && a->policy == b->policy;
}

/* The caller objects must also compare equal in the live H2/S3 query. */
static BOOL e1s_same_binding( const struct e1s_binding *a, const struct e1s_binding *b )
{
    return a->auth_id && a->token_id == b->token_id && a->modified_id == b->modified_id &&
           a->auth_id == b->auth_id && a->session == b->session && a->start_time == b->start_time &&
           a->sid_len == b->sid_len && a->sid_len <= sizeof(a->sid) && !memcmp( a->sid, b->sid, a->sid_len );
}

static BOOL e1s_binding_digest( const struct e1s_context *c, const struct e1s_binding *s, BYTE out[32] )
{
    BYTE data[256];
    struct e1s_bytes b = { data, 0, sizeof(data), FALSE };
    if (!e1s_generations_valid( &c->gen ) || !e1s_nonzero( c->prefix, 16 ) ||
        !e1s_nonzero( c->host_process, 16 ) || !e1s_nonzero( c->opener, 16 ) ||
        memcmp( c->opener, c->writer, 16 ) || !e1s_nonzero( c->desktop, 16 ) ||
        !e1s_nonzero( c->channel, 16 ) || !s->token_id || !s->modified_id || !s->auth_id ||
        s->sid_len < 8 || s->sid_len > sizeof(s->sid) || !IsValidSid( (PSID)s->sid ) ||
        GetLengthSid( (PSID)s->sid ) != s->sid_len) return FALSE;
    e1s_encode_binding( &b,
        c->gen.server, c->gen.root, c->gen.broker,
        c->prefix, c->host_uid,
        c->host_process, c->opener, c->writer,
        s->token_id, s->modified_id, s->auth_id,
        s->session,
        s->sid, s->sid_len,
        c->desktop );
    return !b.failed && e1s_sha256( "E1S1-BIND", sizeof("E1S1-BIND"), data, b.length, out );
}

static BOOL e1s_absolute( const WCHAR *s )
{
    return s && lstrlenW( s ) >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
           s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}

static BOOL e1s_freeze( struct e1s_frozen *f, UINT64 id, UINT64 now, UINT64 deadline,
                        const struct e1s_context *c, const struct e1s_binding *s,
                        const BY_HANDLE_FILE_INFORMATION *info, const BYTE image_hash[32],
                        const WCHAR *image, const WCHAR *cmd, const WCHAR *cwd, DWORD show, DWORD flags )
{
    struct e1s_bytes b;
    BYTE env_policy[4] = { 1, 0, 0, 0 };
    memset( f, 0, sizeof(*f) );
    if (!id || !now || now >= deadline || !e1s_absolute( image ) || !e1s_absolute( cwd ) ||
        (flags & ~WEXELEVATE_FLAGS_MASK) || (flags & WEXELEVATE_ENV_INHERIT) ||
        !e1s_binding_digest( c, s, f->binding_digest )) return FALSE;
    b.data = malloc( E1S_MAX_RECORD ); b.length = 0; b.capacity = E1S_MAX_RECORD; b.failed = !b.data;
    e1s_encode_request( &b,
        id, f->binding_digest,
        info->dwVolumeSerialNumber,
        ((UINT64)info->nFileIndexHigh << 32) | info->nFileIndexLow,
        ((UINT64)info->nFileSizeHigh << 32) | info->nFileSizeLow,
        ((UINT64)info->ftLastWriteTime.dwHighDateTime << 32) | info->ftLastWriteTime.dwLowDateTime,
        image_hash,
        image, cmd, cwd,
        1, show, flags, deadline );
    if (b.failed || !e1s_sha256( "E1S1-REQ", sizeof("E1S1-REQ"), b.data, b.length, f->request_digest ) ||
        !e1s_sha256( "E1S1-ENV", sizeof("E1S1-ENV"), env_policy, sizeof(env_policy), f->environment_digest ))
    { free( b.data ); return FALSE; }
    f->id = id; f->deadline = deadline; f->bytes = b.data; f->length = b.length;
    return TRUE;
}

/* No time source in these pure functions: callers must supply the endorsed host
 * monotonic time. Test fixtures use an explicit synthetic clock, never GetTickCount. */
static BOOL e1s_begin( struct e1s_attempt *a, const struct e1s_frozen *f, const struct e1s_context *c,
                       DWORD number, const BYTE nonce[32], const BYTE account[16] )
{
    if (!f->bytes || !number || !e1s_nonzero( nonce, 32 ) || !e1s_nonzero( account, 16 )) return FALSE;
    memset( a, 0, sizeof(*a) );
    a->frozen = f; a->context = *c; a->number = number; a->state = E1S_QUEUED;
    memcpy( a->nonce, nonce, 32 ); memcpy( a->account, account, 16 );
    return TRUE;
}

static BOOL e1s_cancel( struct e1s_attempt *a )
{
    if (a->state == E1S_COMMITTED) return FALSE;
    a->state = E1S_CANCELLED;
    return TRUE;
}

static BOOL e1s_answer_matches( const struct e1s_attempt *a, const struct e1s_answer *r, UINT64 now )
{
    return a->state == E1S_AUTHENTICATING && now && now < a->frozen->deadline &&
           r->kind == 2 && r->version == 1 && r->length == E1S_ANSWER_LENGTH && r->caps == E1S_CAPS &&
           r->id == a->frozen->id && r->attempt == a->number && r->round == a->round && r->index == a->index &&
           (r->answer_kind == 2 || r->answer_kind == 3) && !r->payload_len && !a->consent &&
           !memcmp( r->nonce, a->nonce, 32 ) && !memcmp( r->binding_digest, a->frozen->binding_digest, 32 ) &&
           !memcmp( r->request_digest, a->frozen->request_digest, 32 ) && !memcmp( r->channel, a->context.channel, 16 );
}

static BOOL e1s_evidence_matches( const struct e1s_attempt *a, const struct e1s_evidence *e, UINT64 now,
                                 const struct e1s_generations *current, UINT64 mapped_uid )
{
    return (a->state == E1S_AUTHENTICATING || a->state == E1S_IDENTITY_READY) && !a->redeemed &&
           now && e->completed && e->completed <= now && e->completed < e->expiry &&
           now < e->expiry && e->expiry <= a->frozen->deadline &&
           e->kind == 3 && e->version == 1 && e->length == E1S_EVIDENCE_LENGTH && e->caps == E1S_CAPS &&
           e->id == a->frozen->id && e->attempt == a->number && e->method == 1 && e->service == 1 &&
           e->uid == mapped_uid && e1s_generations_valid( current ) &&
           (a->state != E1S_IDENTITY_READY ||
            (e->completed == a->evidence_completed && e->expiry == a->evidence_expiry)) &&
           e1s_same_generations( &e->gen, current ) && e1s_same_generations( &a->context.gen, current ) &&
           !memcmp( e->account, a->account, 16 ) && !memcmp( e->nonce, a->nonce, 32 ) &&
           !memcmp( e->request_digest, a->frozen->request_digest, 32 ) &&
           !memcmp( e->binding_digest, a->frozen->binding_digest, 32 );
}

/* Broker-only compare/transition. The caller holds its request lock, rechecks
 * live binding/cancellation first, and supplies no state chosen by the dialog.
 * Flags here are internal results, never deserialized authorization booleans. */
static BOOL e1s_broker_step( struct e1s_attempt *a, enum e1s_state next )
{
    if (a->state >= E1S_COMMITTED || next != a->state + 1 ||
        (next == E1S_IDENTITY_READY && (!a->consent || !e1s_nonzero( a->evidence_ref, 16 ))) ||
        (next == E1S_CONSUMED && !a->redeemed) ||
        (next == E1S_LAUNCH_PREPARED && !a->admin_auth_id)) return FALSE;
    a->state = next;
    return TRUE;
}

static BOOL e1s_admin_logon( UINT64 caller, UINT64 admin, DWORD caller_session, DWORD admin_session )
{
    return caller && admin && caller != admin && caller_session == admin_session;
}

#endif /* !WEXELEVATE_E1S_PROBE_CLIENT */
#endif /* WEXELEVATE_TEST_APPROVAL */
#endif
