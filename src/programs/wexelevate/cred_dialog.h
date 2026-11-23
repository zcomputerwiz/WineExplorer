/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

#ifndef WEX_CRED_DIALOG_H
#define WEX_CRED_DIALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * E1-S17: Credential Dialog Constants, Protocol Definitions & Prompt Queue (H3b).
 *
 * Disabled by default; not reachable from production broker path until S18.
 */

#define WEX_CRED_DIALOG_VERSION         1

#define WEX_PROMPT_QUEUE_PER_CALLER_LIMIT   2
#define WEX_PROMPT_QUEUE_PER_SESSION_LIMIT  8
#define WEX_PROMPT_QUEUE_GLOBAL_LIMIT       32

/* Conversation State Machine */
enum wex_conv_state
{
    WEX_CONV_STATE_INIT = 0,
    WEX_CONV_STATE_PROMPTING,
    WEX_CONV_STATE_AUTHENTICATING,
    WEX_CONV_STATE_CONFIRMED,
    WEX_CONV_STATE_CANCELLED,
    WEX_CONV_STATE_TIMED_OUT,
    WEX_CONV_STATE_DENIED
};

/* Queue Entry States */
enum wex_queue_entry_state
{
    WEX_QUEUE_STATE_WAITING = 0,
    WEX_QUEUE_STATE_ACTIVE,
    WEX_QUEUE_STATE_CONFIRMED,
    WEX_QUEUE_STATE_CANCELLED,
    WEX_QUEUE_STATE_TIMED_OUT,
    WEX_QUEUE_STATE_DONE
};

/* The answer record, the account-name rule and wiping: cred_record.h (header-only, shared with consent). */
#include "cred_record.h"

/* Conversation Tracking */
struct wex_conversation
{
    uint64_t request_id;
    uint32_t attempt;
    uint32_t round;
    uint32_t index;
    uint8_t  nonce[32];
    uint8_t  binding_digest[32];
    uint8_t  request_digest[32];
    uint8_t  channel_id[16];
    enum wex_conv_state state;
    int      confirmed;
    char     typed_account[WEX_MAX_ACCOUNT_NAME_LEN + 1];
};

void wex_conversation_init( struct wex_conversation *conv,
                            uint64_t request_id, uint32_t attempt,
                            const uint8_t nonce[32],
                            const uint8_t binding_digest[32],
                            const uint8_t request_digest[32],
                            const uint8_t channel_id[16] );

int wex_conversation_set_account( struct wex_conversation *conv, const char *account );
int wex_conversation_apply_answer( struct wex_conversation *conv,
                                   const uint8_t *buf, size_t len,
                                   uint64_t now, uint64_t deadline );
void wex_conversation_cancel( struct wex_conversation *conv );
void wex_conversation_child_exit( struct wex_conversation *conv, int exit_code );
int wex_conversation_is_approved( const struct wex_conversation *conv );
void wex_conversation_wipe( struct wex_conversation *conv );

/*
 * Platform Synchronization Primitives for Prompt Queue
 */
#if defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION wex_queue_lock_t;
typedef HANDLE wex_queue_event_t;
#else
#include <pthread.h>
typedef pthread_mutex_t wex_queue_lock_t;
typedef pthread_cond_t wex_queue_event_t;
#endif

/* Serialized Prompt Queue (H3b) */
struct wex_prompt_queue_entry
{
    uint64_t request_id;
    uint32_t caller_pid;
    uint64_t caller_logon;
    uint32_t session_id;
    char     desktop[64];
    uint64_t deadline;
    enum wex_queue_entry_state state;
    int      is_active;
    int      waiter_count;
    int      unlinked;
#if defined(_WIN32)
    HANDLE event;
#else
    pthread_cond_t cond;
    int      signaled;
#endif
    struct wex_prompt_queue_entry *prev;
    struct wex_prompt_queue_entry *next;
};

struct wex_prompt_queue
{
    wex_queue_lock_t lock;
    int initialized;
    struct wex_prompt_queue_entry *head;
    struct wex_prompt_queue_entry *tail;
    struct wex_prompt_queue_entry *active_entry;
    unsigned int total_count;
};

void wex_prompt_queue_init( struct wex_prompt_queue *q );
void wex_prompt_queue_destroy( struct wex_prompt_queue *q );

int wex_prompt_queue_enqueue( struct wex_prompt_queue *q,
                              uint64_t request_id,
                              uint32_t caller_pid,
                              uint64_t caller_logon,
                              uint32_t session_id,
                              const char *desktop,
                              uint64_t deadline,
                              struct wex_prompt_queue_entry **out_entry );

int wex_prompt_queue_wait_active( struct wex_prompt_queue *q,
                                  struct wex_prompt_queue_entry *entry,
                                  uint64_t timeout_ms );

int wex_prompt_queue_cancel( struct wex_prompt_queue *q,
                             struct wex_prompt_queue_entry *entry );

int wex_prompt_queue_cancel_all( struct wex_prompt_queue *q );

int wex_prompt_queue_release( struct wex_prompt_queue *q,
                              struct wex_prompt_queue_entry *entry );

int wex_prompt_queue_check_deadlines( struct wex_prompt_queue *q, uint64_t now );

#ifdef __cplusplus
}
#endif

#endif /* WEX_CRED_DIALOG_H */
