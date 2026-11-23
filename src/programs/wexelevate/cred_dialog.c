/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

/* A lab unit (broker_internal.h): empty in a shipping Wine build. */
#if defined(WEXELEVATE_E1S18) || !defined(__WINE_PE_BUILD)

#include "cred_dialog.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

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

/* Bounded copy that never reads past the terminator (Wine headers forbid strncpy). */
static void wex_copy_bounded( char *dst, size_t dstsize, const char *src )
{
    size_t n = 0;
    while (n + 1 < dstsize && src[n]) n++;
    memcpy( dst, src, n );
    dst[n] = 0;
}

int wex_conversation_set_account( struct wex_conversation *conv, const char *account )
{
    if (!conv || !account) return WEX_STATUS_INVALID_PARAMETER;
    if (!wex_valid_account_name( account )) return WEX_STATUS_INVALID_PARAMETER;

    wex_secure_wipe( conv->typed_account, sizeof(conv->typed_account) );
    wex_copy_bounded( conv->typed_account, WEX_MAX_ACCOUNT_NAME_LEN + 1, account );
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

    /* R15: an S1 answer is exactly WEX_S1_ANSWER_LENGTH bytes; reject shorter and longer. */
    if (!conv || !buf || len != WEX_S1_ANSWER_LENGTH)
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

static void queue_lock( struct wex_prompt_queue *q )
{
#if defined(_WIN32)
    EnterCriticalSection( &q->lock );
#else
    pthread_mutex_lock( &q->lock );
#endif
}

static void queue_unlock( struct wex_prompt_queue *q )
{
#if defined(_WIN32)
    LeaveCriticalSection( &q->lock );
#else
    pthread_mutex_unlock( &q->lock );
#endif
}

static void entry_signal( struct wex_prompt_queue_entry *entry )
{
#if defined(_WIN32)
    if (entry->event) SetEvent( entry->event );
#else
    entry->signaled = 1;
    pthread_cond_broadcast( &entry->cond );
#endif
}

static void entry_destroy_sync( struct wex_prompt_queue_entry *entry )
{
#if defined(_WIN32)
    if (entry->event)
    {
        CloseHandle( entry->event );
        entry->event = NULL;
    }
#else
    pthread_cond_destroy( &entry->cond );
#endif
}

static void queue_unlink( struct wex_prompt_queue *q, struct wex_prompt_queue_entry *entry )
{
    if (entry->unlinked) return;
    if (entry->prev) entry->prev->next = entry->next;
    else q->head = entry->next;

    if (entry->next) entry->next->prev = entry->prev;
    else q->tail = entry->prev;

    entry->prev = NULL;
    entry->next = NULL;
    entry->unlinked = 1;
    if (q->total_count > 0) q->total_count--;
}

static void queue_promote_next( struct wex_prompt_queue *q, const struct wex_prompt_queue_entry *rel )
{
    struct wex_prompt_queue_entry *next;

    for (next = q->head; next != NULL; next = next->next)
    {
        if (next->state == WEX_QUEUE_STATE_WAITING &&
            next->session_id == rel->session_id &&
            !strcmp( next->desktop, rel->desktop ))
        {
            next->is_active = 1;
            next->state = WEX_QUEUE_STATE_ACTIVE;
            entry_signal( next );
            break;
        }
    }

    q->active_entry = NULL;
    for (next = q->head; next != NULL; next = next->next)
    {
        if (next->is_active)
        {
            q->active_entry = next;
            break;
        }
    }
}

void wex_prompt_queue_init( struct wex_prompt_queue *q )
{
    if (!q) return;
    memset( q, 0, sizeof(*q) );
#if defined(_WIN32)
    InitializeCriticalSection( &q->lock );
#else
    pthread_mutex_init( &q->lock, NULL );
#endif
    q->head = NULL;
    q->tail = NULL;
    q->active_entry = NULL;
    q->total_count = 0;
    q->initialized = 1;
}

void wex_prompt_queue_destroy( struct wex_prompt_queue *q )
{
    struct wex_prompt_queue_entry *cur, *next;

    if (!q || !q->initialized) return;
    queue_lock( q );
    cur = q->head;
    while (cur)
    {
        next = cur->next;
        entry_destroy_sync( cur );
        free( cur );
        cur = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->active_entry = NULL;
    q->total_count = 0;
    queue_unlock( q );

#if defined(_WIN32)
    DeleteCriticalSection( &q->lock );
#else
    pthread_mutex_destroy( &q->lock );
#endif
    q->initialized = 0;
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
    int route_active = 0;

    if (!q || !request_id || !out_entry)
        return WEX_STATUS_INVALID_PARAMETER;

    queue_lock( q );

    if (q->total_count >= WEX_PROMPT_QUEUE_GLOBAL_LIMIT)
    {
        queue_unlock( q );
        return WEX_STATUS_BUSY;
    }

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
    {
        queue_unlock( q );
        return WEX_STATUS_QUOTA_EXCEEDED;
    }

    if (session_count >= WEX_PROMPT_QUEUE_PER_SESSION_LIMIT)
    {
        queue_unlock( q );
        return WEX_STATUS_BUSY;
    }

    entry = (struct wex_prompt_queue_entry *)malloc( sizeof(*entry) );
    if (!entry)
    {
        queue_unlock( q );
        return WEX_STATUS_BUSY;
    }

    memset( entry, 0, sizeof(*entry) );
#if defined(_WIN32)
    entry->event = CreateEventW( NULL, FALSE, FALSE, NULL );
    if (!entry->event)
    {
        free( entry );
        queue_unlock( q );
        return WEX_STATUS_BUSY;
    }
#else
    pthread_cond_init( &entry->cond, NULL );
#endif
    entry->request_id = request_id;
    entry->caller_pid = caller_pid;
    entry->caller_logon = caller_logon;
    entry->session_id = session_id;
    if (desktop)
    {
        wex_copy_bounded( entry->desktop, sizeof(entry->desktop), desktop );
        entry->desktop[sizeof(entry->desktop) - 1] = '\0';
    }
    entry->deadline = deadline;

    /* Check if an entry for this (session_id, desktop) route is already active */
    for (cur = q->head; cur != NULL; cur = cur->next)
    {
        if (cur->is_active && cur->session_id == session_id && !strcmp( cur->desktop, entry->desktop ))
        {
            route_active = 1;
            break;
        }
    }

    /* Link at tail of FIFO */
    entry->prev = q->tail;
    entry->next = NULL;
    if (q->tail) q->tail->next = entry;
    else q->head = entry;
    q->tail = entry;
    q->total_count++;

    if (!route_active)
    {
        entry->is_active = 1;
        entry->state = WEX_QUEUE_STATE_ACTIVE;
        if (!q->active_entry) q->active_entry = entry;
    }
    else
    {
        entry->is_active = 0;
        entry->state = WEX_QUEUE_STATE_WAITING;
    }

    *out_entry = entry;
    queue_unlock( q );
    return WEX_STATUS_OK;
}

int wex_prompt_queue_wait_active( struct wex_prompt_queue *q,
                                  struct wex_prompt_queue_entry *entry,
                                  uint64_t timeout_ms )
{
    if (!q || !entry) return WEX_STATUS_INVALID_PARAMETER;

    queue_lock( q );
    if (entry->is_active && entry->state == WEX_QUEUE_STATE_ACTIVE)
    {
        queue_unlock( q );
        return WEX_STATUS_OK;
    }

    entry->waiter_count++;

#if defined(_WIN32)
    while (!entry->is_active && entry->state == WEX_QUEUE_STATE_WAITING)
    {
        HANDLE ev = entry->event;
        queue_unlock( q );
        DWORD wr = WaitForSingleObject( ev, (DWORD)timeout_ms );
        queue_lock( q );
        if (wr == WAIT_TIMEOUT)
        {
            if (!entry->is_active && entry->state == WEX_QUEUE_STATE_WAITING)
            {
                entry->state = WEX_QUEUE_STATE_TIMED_OUT;
                queue_unlink( q, entry );
                entry->waiter_count--;
                queue_unlock( q );
                return WEX_STATUS_TIMED_OUT;
            }
        }
        break;
    }
#else
    {
        struct timespec ts;
        clock_gettime( CLOCK_REALTIME, &ts );
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000ULL;
        if (ts.tv_nsec >= 1000000000L)
        {
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
        }

        while (!entry->is_active && entry->state == WEX_QUEUE_STATE_WAITING && !entry->signaled)
        {
            int rc = pthread_cond_timedwait( &entry->cond, &q->lock, &ts );
            if (rc == ETIMEDOUT)
            {
                if (!entry->is_active && entry->state == WEX_QUEUE_STATE_WAITING)
                {
                    entry->state = WEX_QUEUE_STATE_TIMED_OUT;
                    queue_unlink( q, entry );
                    entry->waiter_count--;
                    queue_unlock( q );
                    return WEX_STATUS_TIMED_OUT;
                }
                break;
            }
        }
    }
#endif

    entry->waiter_count--;
    if (entry->is_active && entry->state == WEX_QUEUE_STATE_ACTIVE)
    {
        queue_unlock( q );
        return WEX_STATUS_OK;
    }

    if (entry->state == WEX_QUEUE_STATE_CANCELLED)
    {
        queue_unlock( q );
        return WEX_STATUS_CANCELLED;
    }
    if (entry->state == WEX_QUEUE_STATE_TIMED_OUT)
    {
        queue_unlock( q );
        return WEX_STATUS_TIMED_OUT;
    }

    queue_unlock( q );
    return WEX_STATUS_DENIED;
}

int wex_prompt_queue_cancel( struct wex_prompt_queue *q,
                             struct wex_prompt_queue_entry *entry )
{
    int was_active;

    if (!q || !entry) return WEX_STATUS_INVALID_PARAMETER;

    queue_lock( q );
    was_active = entry->is_active;
    entry->state = WEX_QUEUE_STATE_CANCELLED;
    entry->is_active = 0;

    queue_unlink( q, entry );

    if (was_active)
    {
        queue_promote_next( q, entry );
    }

    /* The owning request thread still holds the entry; only its release frees it (R2). */
    entry_signal( entry );
    queue_unlock( q );
    return WEX_STATUS_OK;
}

int wex_prompt_queue_cancel_all( struct wex_prompt_queue *q )
{
    struct wex_prompt_queue_entry *cur, *next;

    if (!q) return WEX_STATUS_INVALID_PARAMETER;

    queue_lock( q );
    cur = q->head;
    while (cur)
    {
        next = cur->next;
        cur->state = WEX_QUEUE_STATE_CANCELLED;
        cur->is_active = 0;
        entry_signal( cur );
        cur = next;
    }
    q->active_entry = NULL;
    queue_unlock( q );
    return WEX_STATUS_OK;
}

int wex_prompt_queue_release( struct wex_prompt_queue *q,
                              struct wex_prompt_queue_entry *entry )
{
    int was_active;

    if (!q || !entry) return WEX_STATUS_INVALID_PARAMETER;

    queue_lock( q );
    was_active = entry->is_active;
    entry->is_active = 0;

    queue_unlink( q, entry );

    if (was_active)
    {
        queue_promote_next( q, entry );
    }

    entry_destroy_sync( entry );
    free( entry );
    queue_unlock( q );
    return WEX_STATUS_OK;
}

int wex_prompt_queue_check_deadlines( struct wex_prompt_queue *q, uint64_t now )
{
    struct wex_prompt_queue_entry *cur, *next;
    int count = 0;

    if (!q || !now) return 0;

    queue_lock( q );
    cur = q->head;
    while (cur)
    {
        next = cur->next;
        if (cur->deadline && now >= cur->deadline)
        {
            int was_active = cur->is_active;
            cur->state = WEX_QUEUE_STATE_TIMED_OUT;
            cur->is_active = 0;
            queue_unlink( q, cur );
            if (was_active)
            {
                queue_promote_next( q, cur );
            }
            entry_signal( cur ); /* the owner's release frees it (R2) */
            count++;
        }
        cur = next;
    }
    queue_unlock( q );
    return count;
}

#else
typedef int wexelevate_lab_unit_empty;   /* ISO C: no empty translation unit */
#endif /* WEXELEVATE_E1S18 || !__WINE_PE_BUILD */
