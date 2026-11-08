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
#define WEX_S1_ANSWER_KIND              2
#define WEX_S1_ANSWER_VERSION           1
#define WEX_S1_ANSWER_LENGTH            156
#define WEX_S1_REQUIRED_CAPS            0x3ffULL

#define WEX_ANSWER_KIND_TEXT            1
#define WEX_ANSWER_KIND_CONFIRM         2
#define WEX_ANSWER_KIND_CANCEL          3

#define WEX_MAX_ACCOUNT_NAME_LEN        63
#define WEX_MIN_ACCOUNT_NAME_LEN        1

#define WEX_PROMPT_QUEUE_PER_CALLER_LIMIT   2
#define WEX_PROMPT_QUEUE_PER_SESSION_LIMIT  8
#define WEX_PROMPT_QUEUE_GLOBAL_LIMIT       32

/* Status codes */
#define WEX_STATUS_OK                   0
#define WEX_STATUS_INVALID_PARAMETER    1
#define WEX_STATUS_QUOTA_EXCEEDED       2
#define WEX_STATUS_BUSY                 3
#define WEX_STATUS_CANCELLED            4
#define WEX_STATUS_TIMED_OUT            5
#define WEX_STATUS_DENIED               6
#define WEX_STATUS_NOT_READY            7

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

/* Account Name Validation (S4 account map rules) */
int wex_valid_account_name( const char *name );

/* Secure Buffer Wiping */
void wex_secure_wipe( void *ptr, size_t size );
int wex_is_buffer_zeroed( const void *ptr, size_t size );

/* S1 Answer Record Construction & Verification */
int wex_build_answer_record( uint64_t request_id, uint32_t attempt,
                             uint32_t round, uint32_t index,
                             uint32_t answer_kind,
                             const uint8_t nonce[32],
                             const uint8_t binding_digest[32],
                             const uint8_t request_digest[32],
                             const uint8_t channel_id[16],
                             uint8_t *out_buf, size_t buf_size,
                             size_t *out_len );

int wex_verify_answer_record( const uint8_t *buf, size_t len,
                              uint64_t expected_req_id,
                              uint32_t expected_attempt,
                              uint32_t expected_round,
                              uint32_t expected_index,
                              uint32_t expected_answer_kind,
                              const uint8_t expected_nonce[32],
                              const uint8_t expected_binding_digest[32],
                              const uint8_t expected_request_digest[32],
                              const uint8_t expected_channel_id[16] );

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
    struct wex_prompt_queue_entry *prev;
    struct wex_prompt_queue_entry *next;
};

struct wex_prompt_queue
{
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

int wex_prompt_queue_cancel( struct wex_prompt_queue *q,
                             struct wex_prompt_queue_entry *entry );

int wex_prompt_queue_release( struct wex_prompt_queue *q,
                              struct wex_prompt_queue_entry *entry );

int wex_prompt_queue_check_deadlines( struct wex_prompt_queue *q, uint64_t now );

#ifdef __cplusplus
}
#endif

#endif /* WEX_CRED_DIALOG_H */
