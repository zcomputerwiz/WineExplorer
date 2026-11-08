/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

#include "cred_dialog.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/*
 * S4 Account-Map Name Validation Rules:
 * - Length: 1 to 63 ASCII characters.
 * - Allowed characters: a-z, A-Z, 0-9, '_', '-', '.'
 * - First character cannot be '-'.
 * - Name cannot be '.' or '..'.
 * - Trailing '$' permitted only as the last character (i == len - 1) for machine accounts (len > 1).
 * - Reject path separators ('/', '\\'), control characters, quotes, whitespace, and markup ('<', '>', '&').
 */
int wex_valid_account_name( const char *name )
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
void wex_secure_wipe( void *ptr, size_t size )
{
    if (!ptr || !size) return;

#if defined(_WIN32)
    SecureZeroMemory( ptr, size );
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
    explicit_bzero( ptr, size );
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (size--) *p++ = 0;
#endif
}

int wex_is_buffer_zeroed( const void *ptr, size_t size )
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
int wex_build_answer_record( uint64_t request_id, uint32_t attempt,
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
int wex_verify_answer_record( const uint8_t *buf, size_t len,
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

    if (!buf || len < WEX_S1_ANSWER_LENGTH) return 0;
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

/*
 * Conversation Tracking & State Machine
 */
void wex_conversation_init( struct wex_conversation *conv,
                            uint64_t request_id, uint32_t attempt,
                            const uint8_t nonce[32],
                            const uint8_t binding_digest[32],
                            const uint8_t request_digest[32],
                            const uint8_t channel_id[16] )
{
    if (!conv) return;
    memset( conv, 0, sizeof(*conv) );
    conv->request_id = request_id;
    conv->attempt = attempt;
    conv->round = 0;
    conv->index = 0;
    conv->state = WEX_CONV_STATE_INIT;
    conv->confirmed = 0;

    if (nonce) memcpy( conv->nonce, nonce, 32 );
    if (binding_digest) memcpy( conv->binding_digest, binding_digest, 32 );
    if (request_digest) memcpy( conv->request_digest, request_digest, 32 );
    if (channel_id) memcpy( conv->channel_id, channel_id, 16 );
}

int wex_conversation_set_account( struct wex_conversation *conv, const char *account )
{
    if (!conv || !account) return WEX_STATUS_INVALID_PARAMETER;
    if (!wex_valid_account_name( account )) return WEX_STATUS_INVALID_PARAMETER;

    wex_secure_wipe( conv->typed_account, sizeof(conv->typed_account) );
    strncpy( conv->typed_account, account, WEX_MAX_ACCOUNT_NAME_LEN );
    conv->typed_account[WEX_MAX_ACCOUNT_NAME_LEN] = '\0';

    if (conv->state == WEX_CONV_STATE_INIT)
        conv->state = WEX_CONV_STATE_PROMPTING;

    return WEX_STATUS_OK;
}

int wex_conversation_apply_answer( struct wex_conversation *conv,
                                   const uint8_t *buf, size_t len,
                                   uint64_t now, uint64_t deadline )
{
    const struct wex_s1_answer_record *rec;

    if (!conv || !buf || len < WEX_S1_ANSWER_LENGTH)
        return WEX_STATUS_INVALID_PARAMETER;
    if (conv->state != WEX_CONV_STATE_PROMPTING &&
        conv->state != WEX_CONV_STATE_AUTHENTICATING)
        return WEX_STATUS_DENIED;
    if (now && deadline && now >= deadline)
    {
        conv->state = WEX_CONV_STATE_TIMED_OUT;
        return WEX_STATUS_TIMED_OUT;
    }

    rec = (const struct wex_s1_answer_record *)buf;
    if (rec->answer_kind == WEX_ANSWER_KIND_CONFIRM)
    {
        if (!wex_verify_answer_record( buf, len,
                                       conv->request_id, conv->attempt,
                                       conv->round, conv->index,
                                       WEX_ANSWER_KIND_CONFIRM,
                                       conv->nonce, conv->binding_digest,
                                       conv->request_digest, conv->channel_id ))
        {
            conv->state = WEX_CONV_STATE_DENIED;
            return WEX_STATUS_DENIED;
        }
        conv->confirmed = 1;
        conv->state = WEX_CONV_STATE_CONFIRMED;
        return WEX_STATUS_OK;
    }
    else if (rec->answer_kind == WEX_ANSWER_KIND_CANCEL)
    {
        if (!wex_verify_answer_record( buf, len,
                                       conv->request_id, conv->attempt,
                                       conv->round, conv->index,
                                       WEX_ANSWER_KIND_CANCEL,
                                       conv->nonce, conv->binding_digest,
                                       conv->request_digest, conv->channel_id ))
        {
            conv->state = WEX_CONV_STATE_DENIED;
            return WEX_STATUS_DENIED;
        }
        conv->confirmed = 0;
        conv->state = WEX_CONV_STATE_CANCELLED;
        return WEX_STATUS_CANCELLED;
    }

    conv->state = WEX_CONV_STATE_DENIED;
    return WEX_STATUS_DENIED;
}

void wex_conversation_cancel( struct wex_conversation *conv )
{
    if (!conv) return;
    if (conv->state != WEX_CONV_STATE_CONFIRMED)
    {
        conv->state = WEX_CONV_STATE_CANCELLED;
        conv->confirmed = 0;
    }
}

void wex_conversation_child_exit( struct wex_conversation *conv, int exit_code )
{
    (void)exit_code;
    if (!conv) return;
    /* Helper exit NEVER approves: only explicit valid signed Confirm does */
    if (conv->state != WEX_CONV_STATE_CONFIRMED || !conv->confirmed)
    {
        conv->state = WEX_CONV_STATE_DENIED;
        conv->confirmed = 0;
    }
}

int wex_conversation_is_approved( const struct wex_conversation *conv )
{
    if (!conv) return 0;
    return (conv->state == WEX_CONV_STATE_CONFIRMED && conv->confirmed == 1);
}

void wex_conversation_wipe( struct wex_conversation *conv )
{
    if (!conv) return;
    wex_secure_wipe( conv->nonce, sizeof(conv->nonce) );
    wex_secure_wipe( conv->binding_digest, sizeof(conv->binding_digest) );
    wex_secure_wipe( conv->request_digest, sizeof(conv->request_digest) );
    wex_secure_wipe( conv->channel_id, sizeof(conv->channel_id) );
    wex_secure_wipe( conv->typed_account, sizeof(conv->typed_account) );
    conv->confirmed = 0;
}

/*
 * Serialized Prompt Queue (H3b)
 */
void wex_prompt_queue_init( struct wex_prompt_queue *q )
{
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
    q->active_entry = NULL;
    q->total_count = 0;
}

void wex_prompt_queue_destroy( struct wex_prompt_queue *q )
{
    struct wex_prompt_queue_entry *cur, *next;

    if (!q) return;
    cur = q->head;
    while (cur)
    {
        next = cur->next;
        free( cur );
        cur = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->active_entry = NULL;
    q->total_count = 0;
}

int wex_prompt_queue_enqueue( struct wex_prompt_queue *q,
                              uint64_t request_id,
                              uint32_t caller_pid,
                              uint64_t caller_logon,
                              uint32_t session_id,
                              const char *desktop,
                              uint64_t deadline,
                              struct wex_prompt_queue_entry **out_entry )
{
    struct wex_prompt_queue_entry *cur, *entry;
    unsigned int caller_count = 0;
    unsigned int session_count = 0;

    if (!q || !request_id || !out_entry)
        return WEX_STATUS_INVALID_PARAMETER;

    if (q->total_count >= WEX_PROMPT_QUEUE_GLOBAL_LIMIT)
        return WEX_STATUS_BUSY;

    /* Count per-caller and per-session queued entries */
    for (cur = q->head; cur != NULL; cur = cur->next)
    {
        if (caller_logon && cur->caller_logon == caller_logon)
            caller_count++;
        else if (!caller_logon && cur->caller_pid == caller_pid)
            caller_count++;

        if (cur->session_id == session_id)
            session_count++;
    }

    if (caller_count >= WEX_PROMPT_QUEUE_PER_CALLER_LIMIT)
        return WEX_STATUS_QUOTA_EXCEEDED;

    if (session_count >= WEX_PROMPT_QUEUE_PER_SESSION_LIMIT)
        return WEX_STATUS_BUSY;

    entry = (struct wex_prompt_queue_entry *)malloc( sizeof(*entry) );
    if (!entry) return WEX_STATUS_BUSY;

    memset( entry, 0, sizeof(*entry) );
    entry->request_id = request_id;
    entry->caller_pid = caller_pid;
    entry->caller_logon = caller_logon;
    entry->session_id = session_id;
    if (desktop)
    {
        strncpy( entry->desktop, desktop, sizeof(entry->desktop) - 1 );
        entry->desktop[sizeof(entry->desktop) - 1] = '\0';
    }
    entry->deadline = deadline;

    /* Link at tail of FIFO */
    entry->prev = q->tail;
    entry->next = NULL;
    if (q->tail) q->tail->next = entry;
    else q->head = entry;
    q->tail = entry;
    q->total_count++;

    /* If no active prompt in this queue, this entry becomes active immediately */
    if (!q->active_entry)
    {
        entry->is_active = 1;
        entry->state = WEX_QUEUE_STATE_ACTIVE;
        q->active_entry = entry;
    }
    else
    {
        entry->is_active = 0;
        entry->state = WEX_QUEUE_STATE_WAITING;
    }

    *out_entry = entry;
    return WEX_STATUS_OK;
}

static void queue_unlink( struct wex_prompt_queue *q, struct wex_prompt_queue_entry *entry )
{
    if (entry->prev) entry->prev->next = entry->next;
    else q->head = entry->next;

    if (entry->next) entry->next->prev = entry->prev;
    else q->tail = entry->prev;

    entry->prev = NULL;
    entry->next = NULL;
    if (q->total_count > 0) q->total_count--;
}

int wex_prompt_queue_cancel( struct wex_prompt_queue *q,
                             struct wex_prompt_queue_entry *entry )
{
    if (!q || !entry) return WEX_STATUS_INVALID_PARAMETER;

    entry->state = WEX_QUEUE_STATE_CANCELLED;

    if (q->active_entry == entry)
    {
        return wex_prompt_queue_release( q, entry );
    }

    /* Unlink waiting entry and free */
    queue_unlink( q, entry );
    free( entry );
    return WEX_STATUS_OK;
}

int wex_prompt_queue_release( struct wex_prompt_queue *q,
                              struct wex_prompt_queue_entry *entry )
{
    struct wex_prompt_queue_entry *next;

    if (!q || !entry) return WEX_STATUS_INVALID_PARAMETER;

    if (q->active_entry == entry)
    {
        q->active_entry = NULL;
    }

    queue_unlink( q, entry );
    free( entry );

    /* Promote next waiting entry in FIFO order */
    for (next = q->head; next != NULL; next = next->next)
    {
        if (next->state == WEX_QUEUE_STATE_WAITING)
        {
            next->is_active = 1;
            next->state = WEX_QUEUE_STATE_ACTIVE;
            q->active_entry = next;
            break;
        }
    }

    return WEX_STATUS_OK;
}

int wex_prompt_queue_check_deadlines( struct wex_prompt_queue *q, uint64_t now )
{
    struct wex_prompt_queue_entry *cur, *next;
    int count = 0;

    if (!q || !now) return 0;

    cur = q->head;
    while (cur)
    {
        next = cur->next;
        if (cur->deadline && now >= cur->deadline)
        {
            cur->state = WEX_QUEUE_STATE_TIMED_OUT;
            wex_prompt_queue_release( q, cur );
            count++;
        }
        cur = next;
    }
    return count;
}
