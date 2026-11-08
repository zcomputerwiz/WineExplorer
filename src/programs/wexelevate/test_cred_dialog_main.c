/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

#include "cred_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed_count = 0;

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf( stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #expr ); \
        failed_count++; \
        return 0; \
    } \
} while (0)

static int test_account_name_validation( void )
{
    /* Valid names */
    ASSERT( wex_valid_account_name( "alice" ) );
    ASSERT( wex_valid_account_name( "bob" ) );
    ASSERT( wex_valid_account_name( "admin1" ) );
    ASSERT( wex_valid_account_name( "user_name" ) );
    ASSERT( wex_valid_account_name( "user-name" ) );
    ASSERT( wex_valid_account_name( "user.name" ) );
    ASSERT( wex_valid_account_name( "machine$" ) );
    ASSERT( wex_valid_account_name( "a" ) );
    ASSERT( wex_valid_account_name( "a12345678901234567890123456789012345678901234567890123456789012" ) ); /* 63 chars */

    /* Invalid names */
    ASSERT( !wex_valid_account_name( NULL ) );
    ASSERT( !wex_valid_account_name( "" ) ); /* empty */
    ASSERT( !wex_valid_account_name( "a123456789012345678901234567890123456789012345678901234567890123" ) ); /* 64 chars */
    ASSERT( !wex_valid_account_name( "-alice" ) ); /* leading dash */
    ASSERT( !wex_valid_account_name( "." ) ); /* dot */
    ASSERT( !wex_valid_account_name( ".." ) ); /* dotdot */
    ASSERT( !wex_valid_account_name( "alice/bob" ) ); /* path separator */
    ASSERT( !wex_valid_account_name( "alice\\bob" ) ); /* backslash */
    ASSERT( !wex_valid_account_name( "alice;rm" ) ); /* command injection */
    ASSERT( !wex_valid_account_name( "<script>" ) ); /* markup injection */
    ASSERT( !wex_valid_account_name( "alice&bob" ) ); /* markup */
    ASSERT( !wex_valid_account_name( "alice\"bob" ) ); /* quote */
    ASSERT( !wex_valid_account_name( "alice'bob" ) ); /* quote */
    ASSERT( !wex_valid_account_name( "$alice" ) ); /* dollar at start */
    ASSERT( !wex_valid_account_name( "al$ice" ) ); /* dollar in middle */
    ASSERT( !wex_valid_account_name( "$" ) ); /* dollar alone */
    ASSERT( !wex_valid_account_name( "alice bob" ) ); /* space */
    ASSERT( !wex_valid_account_name( "alice\tbob" ) ); /* tab */
    ASSERT( !wex_valid_account_name( "alice\nbob" ) ); /* newline */

    return 1;
}

static int test_secure_buffer_wipe( void )
{
    unsigned char canary[64];
    size_t i;

    memset( canary, 0x5A, sizeof(canary) );
    ASSERT( !wex_is_buffer_zeroed( canary, sizeof(canary) ) );

    wex_secure_wipe( canary, sizeof(canary) );
    ASSERT( wex_is_buffer_zeroed( canary, sizeof(canary) ) );

    for (i = 0; i < sizeof(canary); i++)
    {
        ASSERT( canary[i] == 0 );
    }

    return 1;
}

static int test_conversation_states_and_confirm( void )
{
    struct wex_conversation conv;
    uint8_t nonce[32], b_digest[32], r_digest[32], channel[16];
    uint8_t ans_buf[WEX_S1_ANSWER_LENGTH];
    size_t ans_len = 0;

    memset( nonce, 0x11, 32 );
    memset( b_digest, 0x22, 32 );
    memset( r_digest, 0x33, 32 );
    memset( channel, 0x44, 16 );

    wex_conversation_init( &conv, 0x1000, 1, nonce, b_digest, r_digest, channel );
    ASSERT( conv.state == WEX_CONV_STATE_INIT );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* Set valid account moves to prompting */
    ASSERT( wex_conversation_set_account( &conv, "alice" ) == WEX_STATUS_OK );
    ASSERT( conv.state == WEX_CONV_STATE_PROMPTING );
    ASSERT( !strcmp( conv.typed_account, "alice" ) );

    /* Build valid confirm answer */
    ASSERT( wex_build_answer_record( 0x1000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                                     nonce, b_digest, r_digest, channel,
                                     ans_buf, sizeof(ans_buf), &ans_len ) == WEX_STATUS_OK );
    ASSERT( ans_len == WEX_S1_ANSWER_LENGTH );

    /* Apply confirm answer */
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 100, 200 ) == WEX_STATUS_OK );
    ASSERT( conv.state == WEX_CONV_STATE_CONFIRMED );
    ASSERT( conv.confirmed == 1 );
    ASSERT( wex_conversation_is_approved( &conv ) );

    /* Wiping clears account and digests */
    wex_conversation_wipe( &conv );
    ASSERT( wex_is_buffer_zeroed( conv.nonce, 32 ) );
    ASSERT( wex_is_buffer_zeroed( conv.typed_account, sizeof(conv.typed_account) ) );

    return 1;
}

static int test_conversation_early_exit_never_approves( void )
{
    struct wex_conversation conv;
    uint8_t nonce[32], b_digest[32], r_digest[32], channel[16];

    memset( nonce, 0x11, 32 );
    memset( b_digest, 0x22, 32 );
    memset( r_digest, 0x33, 32 );
    memset( channel, 0x44, 16 );

    wex_conversation_init( &conv, 0x2000, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );

    /* Child process terminates with exit code 0 WITHOUT sending confirm */
    wex_conversation_child_exit( &conv, 0 );
    ASSERT( conv.state == WEX_CONV_STATE_DENIED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* Reset and test non-zero exit code (crash / kill) */
    wex_conversation_init( &conv, 0x2001, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "bob" );
    wex_conversation_child_exit( &conv, 1 );
    ASSERT( conv.state == WEX_CONV_STATE_DENIED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    return 1;
}

static int test_conversation_binding_mismatch_denies( void )
{
    struct wex_conversation conv;
    uint8_t nonce[32], b_digest[32], r_digest[32], channel[16];
    uint8_t wrong_nonce[32], wrong_digest[32], wrong_channel[16];
    uint8_t ans_buf[WEX_S1_ANSWER_LENGTH];
    size_t ans_len = 0;

    memset( nonce, 0x11, 32 );
    memset( b_digest, 0x22, 32 );
    memset( r_digest, 0x33, 32 );
    memset( channel, 0x44, 16 );

    memset( wrong_nonce, 0x99, 32 );
    memset( wrong_digest, 0x88, 32 );
    memset( wrong_channel, 0x77, 16 );

    wex_conversation_init( &conv, 0x3000, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );

    /* Mismatched nonce */
    wex_build_answer_record( 0x3000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                             wrong_nonce, b_digest, r_digest, channel,
                             ans_buf, sizeof(ans_buf), &ans_len );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 100, 200 ) == WEX_STATUS_DENIED );
    ASSERT( conv.state == WEX_CONV_STATE_DENIED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* Mismatched request ID */
    wex_conversation_init( &conv, 0x3001, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );
    wex_build_answer_record( 0x9999, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                             nonce, b_digest, r_digest, channel,
                             ans_buf, sizeof(ans_buf), &ans_len );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 100, 200 ) == WEX_STATUS_DENIED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* Mismatched channel token */
    wex_conversation_init( &conv, 0x3002, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );
    wex_build_answer_record( 0x3002, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                             nonce, b_digest, r_digest, wrong_channel,
                             ans_buf, sizeof(ans_buf), &ans_len );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 100, 200 ) == WEX_STATUS_DENIED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    return 1;
}

static int test_conversation_cancel_and_timeout( void )
{
    struct wex_conversation conv;
    uint8_t nonce[32], b_digest[32], r_digest[32], channel[16];
    uint8_t ans_buf[WEX_S1_ANSWER_LENGTH];
    size_t ans_len = 0;

    memset( nonce, 0x11, 32 );
    memset( b_digest, 0x22, 32 );
    memset( r_digest, 0x33, 32 );
    memset( channel, 0x44, 16 );

    /* Explicit Cancel message */
    wex_conversation_init( &conv, 0x4000, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );
    wex_build_answer_record( 0x4000, 1, 0, 0, WEX_ANSWER_KIND_CANCEL,
                             nonce, b_digest, r_digest, channel,
                             ans_buf, sizeof(ans_buf), &ans_len );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 100, 200 ) == WEX_STATUS_CANCELLED );
    ASSERT( conv.state == WEX_CONV_STATE_CANCELLED );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* Timeout (now >= deadline) */
    wex_conversation_init( &conv, 0x4001, 1, nonce, b_digest, r_digest, channel );
    wex_conversation_set_account( &conv, "alice" );
    wex_build_answer_record( 0x4001, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                             nonce, b_digest, r_digest, channel,
                             ans_buf, sizeof(ans_buf), &ans_len );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, ans_len, 201, 200 ) == WEX_STATUS_TIMED_OUT );
    ASSERT( conv.state == WEX_CONV_STATE_TIMED_OUT );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    return 1;
}

static int test_answer_record_exact_length( void )
{
    struct wex_conversation conv;
    uint8_t nonce[32], b_digest[32], r_digest[32], channel[16];
    uint8_t ans_buf[WEX_S1_ANSWER_LENGTH + 8];
    size_t ans_len = 0;

    memset( nonce, 0x11, 32 );
    memset( b_digest, 0x22, 32 );
    memset( r_digest, 0x33, 32 );
    memset( channel, 0x44, 16 );

    /* Build a valid confirm answer (exactly WEX_S1_ANSWER_LENGTH bytes) */
    ASSERT( wex_build_answer_record( 0x1000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                                     nonce, b_digest, r_digest, channel,
                                     ans_buf, sizeof(ans_buf), &ans_len ) == WEX_STATUS_OK );
    ASSERT( ans_len == WEX_S1_ANSWER_LENGTH );

    /* wex_verify_answer_record: exactly WEX_S1_ANSWER_LENGTH is accepted */
    ASSERT( wex_verify_answer_record( ans_buf, WEX_S1_ANSWER_LENGTH,
                                      0x1000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                                      nonce, b_digest, r_digest, channel ) );

    /* wex_verify_answer_record: one byte long (157) is rejected */
    ASSERT( !wex_verify_answer_record( ans_buf, WEX_S1_ANSWER_LENGTH + 1,
                                       0x1000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                                       nonce, b_digest, r_digest, channel ) );

    /* wex_verify_answer_record: one byte short (155) is rejected */
    ASSERT( !wex_verify_answer_record( ans_buf, WEX_S1_ANSWER_LENGTH - 1,
                                       0x1000, 1, 0, 0, WEX_ANSWER_KIND_CONFIRM,
                                       nonce, b_digest, r_digest, channel ) );

    /* wex_conversation_apply_answer: one byte long (157) is rejected */
    wex_conversation_init( &conv, 0x1000, 1, nonce, b_digest, r_digest, channel );
    ASSERT( wex_conversation_set_account( &conv, "alice" ) == WEX_STATUS_OK );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, WEX_S1_ANSWER_LENGTH + 1, 100, 200 )
            == WEX_STATUS_INVALID_PARAMETER );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    /* wex_conversation_apply_answer: one byte short (155) is rejected */
    wex_conversation_init( &conv, 0x1001, 1, nonce, b_digest, r_digest, channel );
    ASSERT( wex_conversation_set_account( &conv, "alice" ) == WEX_STATUS_OK );
    ASSERT( wex_conversation_apply_answer( &conv, ans_buf, WEX_S1_ANSWER_LENGTH - 1, 100, 200 )
            == WEX_STATUS_INVALID_PARAMETER );
    ASSERT( !wex_conversation_is_approved( &conv ) );

    return 1;
}

static int test_prompt_queue_fifo( void )
{
    struct wex_prompt_queue q;
    struct wex_prompt_queue_entry *e1 = NULL, *e2 = NULL, *e3 = NULL;

    wex_prompt_queue_init( &q );

    /* Enqueue 3 requests from different callers */
    ASSERT( wex_prompt_queue_enqueue( &q, 101, 1001, 2001, 1, "WinSta0\\Winlogon", 1000, &e1 ) == WEX_STATUS_OK );
    ASSERT( wex_prompt_queue_enqueue( &q, 102, 1002, 2002, 1, "WinSta0\\Winlogon", 1000, &e2 ) == WEX_STATUS_OK );
    ASSERT( wex_prompt_queue_enqueue( &q, 103, 1003, 2003, 1, "WinSta0\\Winlogon", 1000, &e3 ) == WEX_STATUS_OK );

    ASSERT( q.total_count == 3 );

    /* e1 must be ACTIVE immediately; e2 and e3 are WAITING */
    ASSERT( e1->is_active == 1 );
    ASSERT( e1->state == WEX_QUEUE_STATE_ACTIVE );
    ASSERT( q.active_entry == e1 );

    ASSERT( e2->is_active == 0 );
    ASSERT( e2->state == WEX_QUEUE_STATE_WAITING );
    ASSERT( e3->is_active == 0 );
    ASSERT( e3->state == WEX_QUEUE_STATE_WAITING );

    /* Release e1 -> e2 becomes active */
    wex_prompt_queue_release( &q, e1 );
    ASSERT( q.total_count == 2 );
    ASSERT( e2->is_active == 1 );
    ASSERT( e2->state == WEX_QUEUE_STATE_ACTIVE );
    ASSERT( q.active_entry == e2 );
    ASSERT( e3->is_active == 0 );

    /* Release e2 -> e3 becomes active */
    wex_prompt_queue_release( &q, e2 );
    ASSERT( q.total_count == 1 );
    ASSERT( e3->is_active == 1 );
    ASSERT( e3->state == WEX_QUEUE_STATE_ACTIVE );
    ASSERT( q.active_entry == e3 );

    /* Release e3 -> queue empty */
    wex_prompt_queue_release( &q, e3 );
    ASSERT( q.total_count == 0 );
    ASSERT( q.active_entry == NULL );

    wex_prompt_queue_destroy( &q );
    return 1;
}

static int test_prompt_queue_per_caller_limit( void )
{
    struct wex_prompt_queue q;
    struct wex_prompt_queue_entry *e1 = NULL, *e2 = NULL, *e3 = NULL, *e4 = NULL;

    wex_prompt_queue_init( &q );

    /* Caller 5001 enqueues two requests (reaches limit of 2) */
    ASSERT( wex_prompt_queue_enqueue( &q, 201, 1001, 5001, 1, "WinSta0\\Winlogon", 1000, &e1 ) == WEX_STATUS_OK );
    ASSERT( wex_prompt_queue_enqueue( &q, 202, 1001, 5001, 1, "WinSta0\\Winlogon", 1000, &e2 ) == WEX_STATUS_OK );

    /* Third request from same caller is REJECTED with quota exceeded */
    ASSERT( wex_prompt_queue_enqueue( &q, 203, 1001, 5001, 1, "WinSta0\\Winlogon", 1000, &e3 ) == WEX_STATUS_QUOTA_EXCEEDED );
    ASSERT( e3 == NULL );

    /* Another caller (5002) is not blocked */
    ASSERT( wex_prompt_queue_enqueue( &q, 204, 1002, 5002, 1, "WinSta0\\Winlogon", 1000, &e4 ) == WEX_STATUS_OK );
    ASSERT( q.total_count == 3 );

    /* Release e1 from caller 5001 -> caller 5001 can enqueue again */
    wex_prompt_queue_release( &q, e1 );
    ASSERT( wex_prompt_queue_enqueue( &q, 205, 1001, 5001, 1, "WinSta0\\Winlogon", 1000, &e3 ) == WEX_STATUS_OK );

    wex_prompt_queue_destroy( &q );
    return 1;
}

static int test_prompt_queue_cancel_and_timeout( void )
{
    struct wex_prompt_queue q;
    struct wex_prompt_queue_entry *e1 = NULL, *e2 = NULL, *e3 = NULL;

    wex_prompt_queue_init( &q );

    /* Enqueue active e1, and waiting e2 */
    ASSERT( wex_prompt_queue_enqueue( &q, 301, 1001, 6001, 1, "WinSta0\\Winlogon", 500, &e1 ) == WEX_STATUS_OK );
    ASSERT( wex_prompt_queue_enqueue( &q, 302, 1002, 6002, 1, "WinSta0\\Winlogon", 500, &e2 ) == WEX_STATUS_OK );
    ASSERT( q.total_count == 2 );

    /* Cancel waiting e2: unlinked immediately */
    wex_prompt_queue_cancel( &q, e2 );
    ASSERT( q.total_count == 1 );
    ASSERT( q.active_entry == e1 );
    ASSERT( e2->state == WEX_QUEUE_STATE_CANCELLED ); /* still owned by its request thread */
    ASSERT( wex_prompt_queue_release( &q, e2 ) == WEX_STATUS_OK );

    /* Enqueue e3 with deadline 100 */
    ASSERT( wex_prompt_queue_enqueue( &q, 303, 1003, 6003, 1, "WinSta0\\Winlogon", 100, &e3 ) == WEX_STATUS_OK );

    /* Now advance time past e1 (deadline 500) and check deadlines at now=600 */
    ASSERT( wex_prompt_queue_check_deadlines( &q, 600 ) == 2 ); /* both e1 and e3 timed out */
    ASSERT( q.total_count == 0 );
    ASSERT( q.active_entry == NULL );
    ASSERT( e1->state == WEX_QUEUE_STATE_TIMED_OUT && e3->state == WEX_QUEUE_STATE_TIMED_OUT );
    ASSERT( wex_prompt_queue_release( &q, e1 ) == WEX_STATUS_OK );
    ASSERT( wex_prompt_queue_release( &q, e3 ) == WEX_STATUS_OK );

    wex_prompt_queue_destroy( &q );
    return 1;
}

int main( int argc, char **argv )
{
    const char *test = (argc > 1) ? argv[1] : "all";

    printf( "==> Running E1-S17 credential dialog tests [%s]\n", test );

    if (!strcmp( test, "test_disabled" ))
    {
        /* Production exclusion check: verifies credentials mode fails closed */
        struct wex_conversation conv;
        wex_conversation_init( &conv, 0x1, 1, NULL, NULL, NULL, NULL );
        wex_conversation_child_exit( &conv, 0 );
        if (wex_conversation_is_approved( &conv ))
        {
            fprintf( stderr, "FAIL: approved without confirm\n" );
            return 1;
        }
        printf( "PASS test_disabled\n" );
        return 0;
    }

    if (!strcmp( test, "all" ) || !strcmp( test, "account" ))
    {
        if (test_account_name_validation()) printf( "PASS test_account_name_validation\n" );
        else return 1;
    }

    if (!strcmp( test, "all" ) || !strcmp( test, "wipe" ))
    {
        if (test_secure_buffer_wipe()) printf( "PASS test_secure_buffer_wipe\n" );
        else return 1;
    }

    if (!strcmp( test, "all" ) || !strcmp( test, "conversation" ))
    {
        if (test_conversation_states_and_confirm()) printf( "PASS test_conversation_states_and_confirm\n" );
        else return 1;
        if (test_conversation_early_exit_never_approves()) printf( "PASS test_conversation_early_exit_never_approves\n" );
        else return 1;
        if (test_conversation_binding_mismatch_denies()) printf( "PASS test_conversation_binding_mismatch_denies\n" );
        else return 1;
        if (test_conversation_cancel_and_timeout()) printf( "PASS test_conversation_cancel_and_timeout\n" );
        else return 1;
        if (test_answer_record_exact_length()) printf( "PASS test_answer_record_exact_length\n" );
        else return 1;
    }

    if (!strcmp( test, "all" ) || !strcmp( test, "queue" ))
    {
        if (test_prompt_queue_fifo()) printf( "PASS test_prompt_queue_fifo\n" );
        else return 1;
        if (test_prompt_queue_per_caller_limit()) printf( "PASS test_prompt_queue_per_caller_limit\n" );
        else return 1;
        if (test_prompt_queue_cancel_and_timeout()) printf( "PASS test_prompt_queue_cancel_and_timeout\n" );
        else return 1;
    }

    if (failed_count > 0)
    {
        fprintf( stderr, "FAILED: %d tests failed\n", failed_count );
        return 1;
    }

    printf( "==> ALL E1-S17 tests passed successfully.\n" );
    return 0;
}
