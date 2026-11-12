/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * WO-18: E1R8 C codec test (R8 step 2, part 1).
 *
 * Native test for e1r8_codec.h. For every message type it performs a round
 * trip at minimum and maximum length, then exercises every section 11 codec
 * vector (one byte short and long, wrong magic/version/type/reserved,
 * unknown enum values, each bad string kind, bad S1 fields, refused RESULT
 * with a nonzero field, the UTF-8 list, 4096 accepted and 4097 refused,
 * empty password payload refused). A "golden" argument prints, one per
 * line, "<type> <hex>" for each message type encoded from a fixed input;
 * these lines are WO-19's cross-check input.
 */

#include "e1r8_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

#define CHECK( cond, msg ) do { \
    if (!(cond)) { \
        fprintf( stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg ); \
        g_failed = 1; \
        return 1; \
    } \
} while (0)

/*
 * The fixed golden input (WO-18 item 2).
 */
static void fill_golden_tuple( struct e1r8_tuple *t )
{
    int i;
    memset( t, 0, sizeof(*t) );
    t->request_id = 0x0102030405060708ULL;
    t->attempt = 1;
    for (i = 0; i < 32; ++i) t->nonce[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; ++i) t->channel_id[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < 32; ++i) t->binding_digest[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 32; ++i) t->request_digest[i] = (uint8_t)(0x60 + i);
    t->deadline = 0x1122334455667788ULL;
    t->root_gen = 1;
    t->server_gen = 2;
    t->broker_gen = 3;
    t->verifier_gen = 4;
    t->map_gen = 5;
    t->policy_gen = 6;
    t->caller_session = 7;
    for (i = 0; i < 16; ++i) t->desktop_route[i] = (uint8_t)(0x80 + i);
    t->desktop_mode = 1;
    for (i = 0; i < 16; ++i) t->dialog_id[i] = (uint8_t)(0x90 + i);
}

static void fill_range( uint8_t *p, size_t n, uint8_t base )
{
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (uint8_t)(base + i);
}

/* Convert an ASCII C string to UTF-16LE bytes; returns the byte length. */
static size_t to_utf16le( const char *s, uint16_t *out )
{
    size_t i, n = 0;
    for (i = 0; s[i]; ++i)
        out[n++] = (uint16_t)(unsigned char)s[i];
    return n * 2;
}

static void print_hex( const uint8_t *buf, size_t len )
{
    size_t i;
    for (i = 0; i < len; ++i)
        printf( "%02x", buf[i] );
    printf( "\n" );
}

/*
 * Round trips at minimum and maximum length for every message type.
 */
static int test_round_trips( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    int i;

    fill_golden_tuple( &t );

    /* Type 1: INIT (256..2067). Minimum: empty display strings, ":1.1". */
    {
        uint8_t vo[4] = { ':', '1', '.', '1' };
        struct e1r8_init in;
        int err = e1r8_encode_init( buf, 256, &t, vo, 4, NULL, 0, NULL, 0, NULL, 0 );
        CHECK( err == E1R8_OK, "INIT min encode" );
        err = e1r8_decode_init( buf, 256, &t, &in );
        CHECK( err == E1R8_OK, "INIT min decode" );
        CHECK( in.verifier_owner_len == 4, "INIT min vo len" );
        CHECK( in.requester_len == 0 && in.image_len == 0 && in.publisher_len == 0,
               "INIT min empty display" );
    }
    /* Maximum: 255-byte owner, 520-byte display strings. */
    {
        uint8_t vo[255], req[520], img[520], pub[520];
        struct e1r8_init in;
        uint32_t total;
        int err;
        /* build a valid 255-byte owner: ':' + digits + '.' + digits */
        vo[0] = ':';
        for (i = 1; i < 253; ++i) vo[i] = '1';
        vo[253] = '.'; vo[254] = '1';
        for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
        total = 76 + 160 + (4 + 255) + (4 + 520) + (4 + 520) + (4 + 520);
        CHECK( total == 2067, "INIT max total" );
        err = e1r8_encode_init( buf, total, &t, vo, 255, req, 520, img, 520, pub, 520 );
        CHECK( err == E1R8_OK, "INIT max encode" );
        err = e1r8_decode_init( buf, total, &t, &in );
        CHECK( err == E1R8_OK, "INIT max decode" );
        CHECK( in.verifier_owner_len == 255, "INIT max vo len" );
        CHECK( in.requester_len == 520 && in.image_len == 520 && in.publisher_len == 520,
               "INIT max display len" );
    }

    /* Type 2: ACK (156). */
    {
        struct e1r8_ack a;
        int err = e1r8_encode_ack( buf, 156, &t );
        CHECK( err == E1R8_OK, "ACK encode" );
        err = e1r8_decode_ack( buf, 156, &t, &a );
        CHECK( err == E1R8_OK, "ACK decode" );
        CHECK( memcmp( a.binding_digest, t.binding_digest, 32 ) == 0, "ACK binding" );
        CHECK( memcmp( a.request_digest, t.request_digest, 32 ) == 0, "ACK request" );
        CHECK( memcmp( a.dialog_id, t.dialog_id, 16 ) == 0, "ACK dialog" );
    }

    /* Type 3: SELECT_OK (76). */
    CHECK( e1r8_encode_select_ok( buf, 76, &t ) == E1R8_OK, "SELECT_OK encode" );
    CHECK( e1r8_decode_select_ok( buf, 76, &t ) == E1R8_OK, "SELECT_OK decode" );

    /* Type 4: SELECT (81..143). */
    {
        struct e1r8_select s;
        int err = e1r8_encode_select( buf, 81, &t, (const uint8_t *)"a", 1 );
        CHECK( err == E1R8_OK, "SELECT min encode" );
        err = e1r8_decode_select( buf, 81, &t, &s );
        CHECK( err == E1R8_OK, "SELECT min decode" );
        CHECK( s.account_len == 1 && s.account[0] == 'a', "SELECT min account" );
        err = e1r8_encode_select( buf, 143, &t, (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63 );
        CHECK( err == E1R8_OK, "SELECT max encode" );
        err = e1r8_decode_select( buf, 143, &t, &s );
        CHECK( err == E1R8_OK, "SELECT max decode" );
        CHECK( s.account_len == 63, "SELECT max account" );
    }

    /* Type 5: READY (76). */
    CHECK( e1r8_encode_ready( buf, 76, &t ) == E1R8_OK, "READY encode" );
    CHECK( e1r8_decode_ready( buf, 76, &t ) == E1R8_OK, "READY decode" );

    /* Type 6: RESULT (116 refused, 117..179 authenticated). */
    {
        struct e1r8_result r;
        uint8_t zero[32];
        memset( zero, 0, 32 );
        int err = e1r8_encode_result( buf, 116, &t, 2, NULL, 0, zero );
        CHECK( err == E1R8_OK, "RESULT refused encode" );
        err = e1r8_decode_result( buf, 116, &t, &r );
        CHECK( err == E1R8_OK, "RESULT refused decode" );
        CHECK( r.status == 2 && r.account_len == 0, "RESULT refused fields" );
        err = e1r8_encode_result( buf, 179, &t, 1,
                                  (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63,
                                  t.binding_digest );
        CHECK( err == E1R8_OK, "RESULT auth max encode" );
        err = e1r8_decode_result( buf, 179, &t, &r );
        CHECK( err == E1R8_OK, "RESULT auth max decode" );
        CHECK( r.status == 1 && r.account_len == 63, "RESULT auth max fields" );
        CHECK( memcmp( r.challenge, t.binding_digest, 32 ) == 0, "RESULT challenge" );
    }

    /* Type 7: ANSWER (264). Confirm and Cancel. */
    {
        struct e1r8_answer a;
        int err = e1r8_encode_answer( buf, 264, &t, t.binding_digest, 2, 2, 0, 0x3ff );
        CHECK( err == E1R8_OK, "ANSWER confirm encode" );
        err = e1r8_decode_answer( buf, 264, &t, 0x3ff, &a );
        CHECK( err == E1R8_OK, "ANSWER confirm decode" );
        CHECK( a.answer_kind == 2 && a.round == 2 && a.message_index == 0, "ANSWER confirm fields" );
        CHECK( memcmp( a.challenge, t.binding_digest, 32 ) == 0, "ANSWER confirm challenge" );
        {
            uint8_t zero[32];
            memset( zero, 0, 32 );
            err = e1r8_encode_answer( buf, 264, &t, zero, 3, 0, 0, 0x3ff );
            CHECK( err == E1R8_OK, "ANSWER cancel encode" );
            err = e1r8_decode_answer( buf, 264, &t, 0x3ff, &a );
            CHECK( err == E1R8_OK, "ANSWER cancel decode" );
            CHECK( a.answer_kind == 3 && a.round == 0, "ANSWER cancel fields" );
        }
    }

    /* Type 8: DONE (80). */
    {
        struct e1r8_done d;
        CHECK( e1r8_encode_done( buf, 80, &t, 1 ) == E1R8_OK, "DONE encode" );
        CHECK( e1r8_decode_done( buf, 80, &t, &d ) == E1R8_OK, "DONE decode" );
        CHECK( d.status == 1, "DONE status" );
    }

    /* Type 9: ATTACH_OK (76). */
    CHECK( e1r8_encode_attach_ok( buf, 76, &t ) == E1R8_OK, "ATTACH_OK encode" );
    CHECK( e1r8_decode_attach_ok( buf, 76, &t ) == E1R8_OK, "ATTACH_OK decode" );

    /* Type 16: ENDORSED (244). */
    {
        struct e1r8_endorsed e;
        CHECK( e1r8_encode_endorsed( buf, 244, &t, 4242, 0 ) == E1R8_OK, "ENDORSED encode" );
        CHECK( e1r8_decode_endorsed( buf, 244, &t, &e ) == E1R8_OK, "ENDORSED decode" );
        CHECK( e.pid == 4242 && e.uid == 0, "ENDORSED pid/uid" );
    }

    /* Type 17: START (97..159). */
    {
        uint8_t key[16];
        struct e1r8_start s;
        fill_range( key, 16, 0xa0 );
        CHECK( e1r8_encode_start( buf, 97, &t, (const uint8_t *)"a", 1, key ) == E1R8_OK,
               "START min encode" );
        CHECK( e1r8_decode_start( buf, 97, &t, &s ) == E1R8_OK, "START min decode" );
        CHECK( s.account_len == 1 && memcmp( s.account_key, key, 16 ) == 0, "START min fields" );
        CHECK( e1r8_encode_start( buf, 159, &t,
                                  (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63, key ) == E1R8_OK,
               "START max encode" );
        CHECK( e1r8_decode_start( buf, 159, &t, &s ) == E1R8_OK, "START max decode" );
        CHECK( s.account_len == 63, "START max account" );
    }

    /* Type 18: WORKER_READY (76). */
    CHECK( e1r8_encode_worker_ready( buf, 76, &t ) == E1R8_OK, "WORKER_READY encode" );
    CHECK( e1r8_decode_worker_ready( buf, 76, &t ) == E1R8_OK, "WORKER_READY decode" );

    /* Type 19: DELEGATED (76). */
    CHECK( e1r8_encode_delegated( buf, 76, &t ) == E1R8_OK, "DELEGATED encode" );
    CHECK( e1r8_decode_delegated( buf, 76, &t ) == E1R8_OK, "DELEGATED decode" );

    /* Type 20: RESULT_V (116 refused, 117..179 authenticated). */
    {
        uint8_t key[16], ev[16], zero[16];
        struct e1r8_result_v r;
        fill_range( key, 16, 0xa0 );
        fill_range( ev, 16, 0xb0 );
        memset( zero, 0, 16 );
        CHECK( e1r8_encode_result_v( buf, 116, &t, 2, zero, NULL, 0, zero ) == E1R8_OK,
               "RESULT_V refused encode" );
        CHECK( e1r8_decode_result_v( buf, 116, &t, &r ) == E1R8_OK, "RESULT_V refused decode" );
        CHECK( r.status == 2 && r.account_len == 0, "RESULT_V refused fields" );
        CHECK( e1r8_encode_result_v( buf, 179, &t, 1, key,
                                     (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63, ev ) == E1R8_OK,
               "RESULT_V auth max encode" );
        CHECK( e1r8_decode_result_v( buf, 179, &t, &r ) == E1R8_OK, "RESULT_V auth max decode" );
        CHECK( r.status == 1 && r.account_len == 63, "RESULT_V auth max fields" );
        CHECK( memcmp( r.account_key, key, 16 ) == 0 && memcmp( r.evidence_ref, ev, 16 ) == 0,
               "RESULT_V key/evidence" );
    }

    /* Type 21: CANCEL (80). */
    {
        struct e1r8_cancel c;
        CHECK( e1r8_encode_cancel( buf, 80, &t, 1 ) == E1R8_OK, "CANCEL encode" );
        CHECK( e1r8_decode_cancel( buf, 80, &t, &c ) == E1R8_OK, "CANCEL decode" );
        CHECK( c.reason == 1, "CANCEL reason" );
    }

    /* Type 22: W_ACK (76). */
    CHECK( e1r8_encode_w_ack( buf, 76, &t ) == E1R8_OK, "W_ACK encode" );
    CHECK( e1r8_decode_w_ack( buf, 76, &t ) == E1R8_OK, "W_ACK decode" );

    /* Type 23: ATTACH_ACK (76). */
    CHECK( e1r8_encode_attach_ack( buf, 76, &t ) == E1R8_OK, "ATTACH_ACK encode" );
    CHECK( e1r8_decode_attach_ack( buf, 76, &t ) == E1R8_OK, "ATTACH_ACK decode" );

    /* Type 24: PROMPT_RELEASE (76). */
    CHECK( e1r8_encode_prompt_release( buf, 76, &t ) == E1R8_OK, "PROMPT_RELEASE encode" );
    CHECK( e1r8_decode_prompt_release( buf, 76, &t ) == E1R8_OK, "PROMPT_RELEASE decode" );

    /* Type 25: RELEASE (80). */
    {
        struct e1r8_release r;
        CHECK( e1r8_encode_release( buf, 80, &t, 1 ) == E1R8_OK, "RELEASE encode" );
        CHECK( e1r8_decode_release( buf, 80, &t, &r ) == E1R8_OK, "RELEASE decode" );
        CHECK( r.outcome == 1, "RELEASE outcome" );
    }

    /* Type 26: RELEASED (76). */
    CHECK( e1r8_encode_released( buf, 76, &t ) == E1R8_OK, "RELEASED encode" );
    CHECK( e1r8_decode_released( buf, 76, &t ) == E1R8_OK, "RELEASED decode" );

    /* Type 27: W_SETUP (244). */
    {
        struct e1r8_w_setup w;
        CHECK( e1r8_encode_w_setup( buf, 244, &t, 4242, 0 ) == E1R8_OK, "W_SETUP encode" );
        CHECK( e1r8_decode_w_setup( buf, 244, &t, &w ) == E1R8_OK, "W_SETUP decode" );
        CHECK( w.pid == 4242 && w.uid == 0, "W_SETUP pid/uid" );
    }

    /* S7 version 2 PROMPT (178). */
    CHECK( e1r8_encode_s7_prompt( buf, 178, &t ) == E1R8_OK, "S7 PROMPT encode" );
    CHECK( e1r8_decode_s7_prompt( buf, 178, &t ) == E1R8_OK, "S7 PROMPT decode" );

    /* Secret password answer (157..4252). */
    {
        struct e1r8_answer a;
        const uint8_t pw[] = { 0x70, 0xc3, 0xa4, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64 };
        CHECK( e1r8_encode_secret_answer( buf, 156 + 9, &t, 0x3ff, pw, 9 ) == E1R8_OK,
               "secret answer encode" );
        CHECK( e1r8_decode_secret_answer( buf, 156 + 9, &t, 0x3ff, &a ) == E1R8_OK,
               "secret answer decode" );
        CHECK( a.answer_kind == 1 && a.round == 1 && a.message_index == 0, "secret answer fields" );
        CHECK( a.payload_len == 9 && memcmp( a.payload, pw, 9 ) == 0, "secret answer payload" );
    }

    return 0;
}

/*
 * Pre-endorsement and sealed tuple round trips at the section 4 offsets.
 */
static int test_tuples( void )
{
    struct e1r8_tuple t, d;
    uint8_t buf[256];

    fill_golden_tuple( &t );

    /* Pre-endorsement tuple (204 bytes): section 4 offsets. */
    CHECK( e1r8_encode_pre_endorsement_tuple( buf, E1R8_PRE_ENDORSEMENT_SIZE, &t ) == E1R8_OK,
           "pre-endorsement encode" );
    CHECK( e1r8_get_u64( buf + 0 ) == t.request_id, "pre-endorsement request_id @0" );
    CHECK( e1r8_get_u32( buf + 8 ) == t.attempt, "pre-endorsement attempt @8" );
    CHECK( memcmp( buf + 12, t.nonce, 32 ) == 0, "pre-endorsement nonce @12" );
    CHECK( memcmp( buf + 44, t.channel_id, 16 ) == 0, "pre-endorsement channel_id @44" );
    CHECK( memcmp( buf + 60, t.binding_digest, 32 ) == 0, "pre-endorsement binding @60" );
    CHECK( memcmp( buf + 92, t.request_digest, 32 ) == 0, "pre-endorsement request @92" );
    CHECK( e1r8_get_u64( buf + 124 ) == t.deadline, "pre-endorsement deadline @124" );
    CHECK( e1r8_get_u64( buf + 132 ) == t.root_gen, "pre-endorsement root_gen @132" );
    CHECK( e1r8_get_u64( buf + 140 ) == t.server_gen, "pre-endorsement server_gen @140" );
    CHECK( e1r8_get_u64( buf + 148 ) == t.broker_gen, "pre-endorsement broker_gen @148" );
    CHECK( e1r8_get_u64( buf + 156 ) == t.verifier_gen, "pre-endorsement verifier_gen @156" );
    CHECK( e1r8_get_u64( buf + 164 ) == t.map_gen, "pre-endorsement map_gen @164" );
    CHECK( e1r8_get_u64( buf + 172 ) == t.policy_gen, "pre-endorsement policy_gen @172" );
    CHECK( e1r8_get_u32( buf + 180 ) == t.caller_session, "pre-endorsement caller_session @180" );
    CHECK( memcmp( buf + 184, t.desktop_route, 16 ) == 0, "pre-endorsement desktop_route @184" );
    CHECK( e1r8_get_u32( buf + 200 ) == t.desktop_mode, "pre-endorsement desktop_mode @200" );
    /* dialog_id is not part of the pre-endorsement tuple; preset it so the
     * full-struct round trip below is meaningful. */
    memset( &d, 0, sizeof(d) );
    memcpy( d.dialog_id, t.dialog_id, 16 );
    CHECK( e1r8_decode_pre_endorsement_tuple( buf, E1R8_PRE_ENDORSEMENT_SIZE, &d ) == E1R8_OK,
           "pre-endorsement decode" );
    CHECK( memcmp( &d, &t, sizeof(t) ) == 0, "pre-endorsement round trip" );
    CHECK( e1r8_decode_pre_endorsement_tuple( buf, E1R8_PRE_ENDORSEMENT_SIZE - 1, &d ) != E1R8_OK,
           "pre-endorsement one short" );
    buf[E1R8_PRE_ENDORSEMENT_SIZE] = 0;
    CHECK( e1r8_decode_pre_endorsement_tuple( buf, E1R8_PRE_ENDORSEMENT_SIZE + 1, &d ) != E1R8_OK,
           "pre-endorsement one long" );

    /* Sealed tuple (220 bytes): section 4 offsets, dialog_id at 204. */
    CHECK( e1r8_encode_sealed_tuple( buf, E1R8_SEALED_SIZE, &t ) == E1R8_OK, "sealed encode" );
    CHECK( e1r8_get_u64( buf + 0 ) == t.request_id, "sealed request_id @0" );
    CHECK( e1r8_get_u32( buf + 8 ) == t.attempt, "sealed attempt @8" );
    CHECK( memcmp( buf + 12, t.nonce, 32 ) == 0, "sealed nonce @12" );
    CHECK( memcmp( buf + 44, t.channel_id, 16 ) == 0, "sealed channel_id @44" );
    CHECK( memcmp( buf + 60, t.binding_digest, 32 ) == 0, "sealed binding @60" );
    CHECK( memcmp( buf + 92, t.request_digest, 32 ) == 0, "sealed request @92" );
    CHECK( e1r8_get_u64( buf + 124 ) == t.deadline, "sealed deadline @124" );
    CHECK( e1r8_get_u64( buf + 132 ) == t.root_gen, "sealed root_gen @132" );
    CHECK( e1r8_get_u64( buf + 140 ) == t.server_gen, "sealed server_gen @140" );
    CHECK( e1r8_get_u64( buf + 148 ) == t.broker_gen, "sealed broker_gen @148" );
    CHECK( e1r8_get_u64( buf + 156 ) == t.verifier_gen, "sealed verifier_gen @156" );
    CHECK( e1r8_get_u64( buf + 164 ) == t.map_gen, "sealed map_gen @164" );
    CHECK( e1r8_get_u64( buf + 172 ) == t.policy_gen, "sealed policy_gen @172" );
    CHECK( e1r8_get_u32( buf + 180 ) == t.caller_session, "sealed caller_session @180" );
    CHECK( memcmp( buf + 184, t.desktop_route, 16 ) == 0, "sealed desktop_route @184" );
    CHECK( e1r8_get_u32( buf + 200 ) == t.desktop_mode, "sealed desktop_mode @200" );
    CHECK( memcmp( buf + 204, t.dialog_id, 16 ) == 0, "sealed dialog_id @204" );
    memset( &d, 0, sizeof(d) );
    CHECK( e1r8_decode_sealed_tuple( buf, E1R8_SEALED_SIZE, &d ) == E1R8_OK, "sealed decode" );
    CHECK( memcmp( &d, &t, sizeof(t) ) == 0, "sealed round trip" );
    CHECK( e1r8_decode_sealed_tuple( buf, E1R8_SEALED_SIZE - 1, &d ) != E1R8_OK,
           "sealed one short" );
    buf[E1R8_SEALED_SIZE] = 0;
    CHECK( e1r8_decode_sealed_tuple( buf, E1R8_SEALED_SIZE + 1, &d ) != E1R8_OK,
           "sealed one long" );

    return 0;
}

/*
 * One byte short and one byte long for a representative set of types.
 */
static int test_length_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    uint8_t key[16];

    fill_golden_tuple( &t );
    fill_range( key, 16, 0xa0 );

    /* ACK: fixed 156. */
    CHECK( e1r8_encode_ack( buf, 156, &t ) == E1R8_OK, "ACK encode" );
    {
        struct e1r8_ack a;
        CHECK( e1r8_decode_ack( buf, 155, &t, &a ) != E1R8_OK, "ACK one short" );
        buf[156] = 0;
        CHECK( e1r8_decode_ack( buf, 157, &t, &a ) != E1R8_OK, "ACK one long" );
    }

    /* SELECT: 81..143. */
    CHECK( e1r8_encode_select( buf, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "SELECT encode" );
    {
        struct e1r8_select s;
        CHECK( e1r8_decode_select( buf, 80, &t, &s ) != E1R8_OK, "SELECT one short" );
        buf[81] = 0;
        CHECK( e1r8_decode_select( buf, 82, &t, &s ) != E1R8_OK, "SELECT one long" );
    }

    /* START: 97..159. */
    CHECK( e1r8_encode_start( buf, 97, &t, (const uint8_t *)"a", 1, key ) == E1R8_OK, "START encode" );
    {
        struct e1r8_start s;
        CHECK( e1r8_decode_start( buf, 96, &t, &s ) != E1R8_OK, "START one short" );
        buf[97] = 0;
        CHECK( e1r8_decode_start( buf, 98, &t, &s ) != E1R8_OK, "START one long" );
    }

    /* INIT: 256..2067. */
    {
        uint8_t vo[4] = { ':', '1', '.', '1' };
        struct e1r8_init in;
        CHECK( e1r8_encode_init( buf, 256, &t, vo, 4, NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
               "INIT encode" );
        CHECK( e1r8_decode_init( buf, 255, &t, &in ) != E1R8_OK, "INIT one short" );
        buf[256] = 0;
        CHECK( e1r8_decode_init( buf, 257, &t, &in ) != E1R8_OK, "INIT one long" );
    }

    /* ANSWER: fixed 264. */
    CHECK( e1r8_encode_answer( buf, 264, &t, t.binding_digest, 2, 2, 0, 0x3ff ) == E1R8_OK,
           "ANSWER encode" );
    {
        struct e1r8_answer a;
        CHECK( e1r8_decode_answer( buf, 263, &t, 0x3ff, &a ) != E1R8_OK, "ANSWER one short" );
        buf[264] = 0;
        CHECK( e1r8_decode_answer( buf, 265, &t, 0x3ff, &a ) != E1R8_OK, "ANSWER one long" );
    }

    /* ENDORSED: fixed 244. */
    CHECK( e1r8_encode_endorsed( buf, 244, &t, 4242, 0 ) == E1R8_OK, "ENDORSED encode" );
    {
        struct e1r8_endorsed e;
        CHECK( e1r8_decode_endorsed( buf, 243, &t, &e ) != E1R8_OK, "ENDORSED one short" );
        buf[244] = 0;
        CHECK( e1r8_decode_endorsed( buf, 245, &t, &e ) != E1R8_OK, "ENDORSED one long" );
    }

    /* Secret password: 157..4252. */
    {
        struct e1r8_answer a;
        const uint8_t pw[] = { 0x70, 0xc3, 0xa4, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64 };
        CHECK( e1r8_encode_secret_answer( buf, 156 + 9, &t, 0x3ff, pw, 9 ) == E1R8_OK,
               "secret encode" );
        CHECK( e1r8_decode_secret_answer( buf, 156 + 8, &t, 0x3ff, &a ) != E1R8_OK,
               "secret one short" );
        buf[156 + 9] = 0;
        CHECK( e1r8_decode_secret_answer( buf, 156 + 10, &t, 0x3ff, &a ) != E1R8_OK,
               "secret one long" );
    }

    return 0;
}

/*
 * Wrong magic, version, type, reserved.
 */
static int test_header_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    struct e1r8_ack a;

    fill_golden_tuple( &t );
    CHECK( e1r8_encode_ack( buf, 156, &t ) == E1R8_OK, "ACK encode" );

    /* wrong magic */
    buf[0] = 'X';
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_MAGIC, "wrong magic" );
    buf[0] = 'E';

    /* wrong version */
    buf[4] = 2;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_VERSION, "wrong version" );
    buf[4] = 1;

    /* wrong type */
    buf[6] = 3;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_TYPE, "wrong type" );
    buf[6] = 2;

    /* nonzero reserved */
    buf[24] = 1;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_RESERVED, "nonzero reserved" );
    buf[24] = 0;

    /* wrong request_id */
    buf[12] = 0xff;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_ID, "wrong request_id" );
    buf[12] = 0x08;

    /* wrong nonce */
    buf[28] = 0xff;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_ID, "wrong nonce" );
    buf[28] = 0x10;

    /* wrong channel_id */
    buf[60] = 0xff;
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_ERR_ID, "wrong channel_id" );
    buf[60] = 0x30;

    /* valid again */
    CHECK( e1r8_decode_ack( buf, 156, &t, &a ) == E1R8_OK, "ACK valid after restore" );
    return 0;
}

/*
 * Unknown enum values.
 */
static int test_enum_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    uint8_t zero[32];
    struct e1r8_done d;
    struct e1r8_cancel c;
    struct e1r8_release r;
    struct e1r8_result res;

    fill_golden_tuple( &t );
    memset( zero, 0, 32 );

    /* DONE: status must be 1 or 2. */
    CHECK( e1r8_encode_done( buf, 80, &t, 1 ) == E1R8_OK, "DONE encode" );
    buf[76] = 3;
    CHECK( e1r8_decode_done( buf, 80, &t, &d ) == E1R8_ERR_ENUM, "DONE bad status" );

    /* CANCEL: reason must be 1. */
    CHECK( e1r8_encode_cancel( buf, 80, &t, 1 ) == E1R8_OK, "CANCEL encode" );
    buf[76] = 2;
    CHECK( e1r8_decode_cancel( buf, 80, &t, &c ) == E1R8_ERR_ENUM, "CANCEL bad reason" );

    /* RELEASE: outcome must be 1 or 2. */
    CHECK( e1r8_encode_release( buf, 80, &t, 1 ) == E1R8_OK, "RELEASE encode" );
    buf[76] = 3;
    CHECK( e1r8_decode_release( buf, 80, &t, &r ) == E1R8_ERR_ENUM, "RELEASE bad outcome" );

    /* RESULT: status must be 1 or 2. */
    CHECK( e1r8_encode_result( buf, 116, &t, 2, NULL, 0, zero ) == E1R8_OK, "RESULT encode" );
    buf[76] = 3;
    CHECK( e1r8_decode_result( buf, 116, &t, &res ) == E1R8_ERR_ENUM, "RESULT bad status" );

    /* desktop_mode in the tuple tail must be 1 or 2. */
    {
        struct e1r8_endorsed e;
        CHECK( e1r8_encode_endorsed( buf, 244, &t, 4242, 0 ) == E1R8_OK, "ENDORSED encode" );
        /* desktop_mode is at 76 + 140 = 216 */
        buf[216] = 3;
        CHECK( e1r8_decode_endorsed( buf, 244, &t, &e ) == E1R8_ERR_ENUM, "ENDORSED bad desktop_mode" );
    }
    return 0;
}

/*
 * Each bad string kind.
 */
static int test_string_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    uint16_t utf16[300];
    size_t req_len;
    uint8_t vo[4] = { ':', '1', '.', '1' };
    struct e1r8_init in;
    struct e1r8_select s;

    fill_golden_tuple( &t );
    req_len = to_utf16le( "Caller", utf16 );

    /* SELECT: non-ASCII account name. */
    {
        uint8_t bad[] = { 'a', 0x80, 'b' };
        CHECK( e1r8_encode_select( buf, 83, &t, bad, 3 ) == E1R8_ERR_STRING,
               "SELECT non-ASCII account encode" );
    }
    /* SELECT: leading dash. */
    CHECK( e1r8_encode_select( buf, 82, &t, (const uint8_t *)"-a", 2 ) == E1R8_ERR_STRING,
           "SELECT leading dash" );
    /* SELECT: 64-byte account (overlength). */
    CHECK( e1r8_encode_select( buf, 144, &t,
                               (const uint8_t *)"a123456789012345678901234567890123456789012345678901234567890123", 64 )
           == E1R8_ERR_STRING, "SELECT overlength account" );

    /* INIT: bad verifier owner (no dot). */
    CHECK( e1r8_encode_init( buf, 256 + 2, &t, (const uint8_t *)":11", 3,
                             (const uint8_t *)utf16, (uint32_t)req_len, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
           "INIT bad verifier owner" );
    /* INIT: bad verifier owner (non-digit). */
    CHECK( e1r8_encode_init( buf, 256 + 3, &t, (const uint8_t *)":1.a", 4,
                             (const uint8_t *)utf16, (uint32_t)req_len, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
           "INIT verifier owner non-digit" );

    /* INIT: display string with NUL (0x0000 code unit). */
    {
        uint16_t nul[4];
        nul[0] = 0; nul[1] = 0; nul[2] = 'b'; nul[3] = 0;
        CHECK( e1r8_encode_init( buf, 256 + 4, &t, vo, 4, (const uint8_t *)nul, 4, NULL, 0, NULL, 0 )
               == E1R8_ERR_STRING, "INIT display NUL" );
    }
    /* INIT: display string with control character. */
    {
        uint16_t ctrl[2];
        ctrl[0] = 0x01; ctrl[1] = 0;
        CHECK( e1r8_encode_init( buf, 256 + 2, &t, vo, 4, (const uint8_t *)ctrl, 2, NULL, 0, NULL, 0 )
               == E1R8_ERR_STRING, "INIT display control" );
    }
    /* INIT: display string with unpaired surrogate. */
    {
        uint16_t surr[2];
        surr[0] = 0xd800; surr[1] = 0;
        CHECK( e1r8_encode_init( buf, 256 + 2, &t, vo, 4, (const uint8_t *)surr, 2, NULL, 0, NULL, 0 )
               == E1R8_ERR_STRING, "INIT display unpaired surrogate" );
    }
    /* INIT: display string with odd length. */
    CHECK( e1r8_encode_init( buf, 256 + 1, &t, vo, 4, (const uint8_t *)"a", 1, NULL, 0, NULL, 0 )
           == E1R8_ERR_STRING, "INIT display odd length" );

    /* valid INIT again */
    CHECK( e1r8_encode_init( buf, 256 + (uint32_t)req_len, &t, vo, 4, (const uint8_t *)utf16,
                             (uint32_t)req_len, NULL, 0, NULL, 0 ) == E1R8_OK,
           "INIT valid" );
    CHECK( e1r8_decode_init( buf, 256 + (uint32_t)req_len, &t, &in ) == E1R8_OK,
           "INIT valid decode" );

    /* SELECT decode: bad account in a crafted buffer. */
    CHECK( e1r8_encode_select( buf, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "SELECT encode" );
    buf[80] = 0x80; /* make the account non-ASCII */
    CHECK( e1r8_decode_select( buf, 81, &t, &s ) == E1R8_ERR_STRING, "SELECT decode non-ASCII" );
    return 0;
}

/*
 * Bad S1 fields in the ANSWER message.
 */
static int test_s1_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    struct e1r8_answer a;

    fill_golden_tuple( &t );
    CHECK( e1r8_encode_answer( buf, 264, &t, t.binding_digest, 2, 2, 0, 0x3ff ) == E1R8_OK,
           "ANSWER encode" );

    /* wrong kind */
    buf[108] = 1;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 wrong kind" );
    buf[108] = 2;

    /* wrong version */
    buf[110] = 2;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 wrong version" );
    buf[110] = 1;

    /* wrong length */
    buf[112] = 157;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 wrong length" );
    buf[112] = 156;

    /* caps outside the mask: required_caps is at 116..124; 0x3ff allows
     * bits 0..9, so set bit 10 (0x04 in byte 117). */
    buf[117] = 0x07;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 caps outside mask" );
    buf[117] = 0x03;

    /* wrong request_id */
    buf[124] = 0xff;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 wrong request_id" );
    buf[124] = 0x08;

    /* wrong nonce */
    buf[148] = 0xff;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 wrong nonce" );
    buf[148] = 0x10;

    /* answer_kind 1 refused on the broker pipe */
    buf[144] = 1;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 answer_kind 1 refused" );
    buf[144] = 2;

    /* Confirm with wrong round */
    buf[136] = 1;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 Confirm wrong round" );
    buf[136] = 2;

    /* Confirm with nonzero payload */
    buf[260] = 1;
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_ERR_S1, "S1 Confirm payload" );
    buf[260] = 0;

    /* valid again */
    CHECK( e1r8_decode_answer( buf, 264, &t, 0x3ff, &a ) == E1R8_OK, "S1 valid after restore" );
    return 0;
}

/*
 * Refused RESULT with a nonzero field.
 */
static int test_refused_result_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    uint8_t zero[32];
    uint8_t key[16], ev[16];
    struct e1r8_result r;
    struct e1r8_result_v rv;

    fill_golden_tuple( &t );
    memset( zero, 0, 32 );
    memset( key, 0, 16 );
    memset( ev, 0, 16 );

    /* RESULT (type 6) refused: nonzero challenge. The challenge occupies
     * bytes 84..116 (status 76..80, empty account length 80..84). */
    CHECK( e1r8_encode_result( buf, 116, &t, 2, NULL, 0, zero ) == E1R8_OK, "RESULT refused encode" );
    buf[115] = 1;
    CHECK( e1r8_decode_result( buf, 116, &t, &r ) == E1R8_ERR_REFUSED, "RESULT refused nonzero challenge" );

    /* RESULT_V (type 20) refused: nonzero account_key. */
    CHECK( e1r8_encode_result_v( buf, 116, &t, 2, key, NULL, 0, ev ) == E1R8_OK,
           "RESULT_V refused encode" );
    buf[80] = 1; /* account_key */
    CHECK( e1r8_decode_result_v( buf, 116, &t, &rv ) == E1R8_ERR_REFUSED,
           "RESULT_V refused nonzero account_key" );

    /* RESULT_V (type 20) refused: nonzero evidence_ref. */
    CHECK( e1r8_encode_result_v( buf, 116, &t, 2, key, NULL, 0, ev ) == E1R8_OK,
           "RESULT_V refused encode" );
    buf[115] = 1; /* evidence_ref last byte */
    CHECK( e1r8_decode_result_v( buf, 116, &t, &rv ) == E1R8_ERR_REFUSED,
           "RESULT_V refused nonzero evidence_ref" );
    return 0;
}

/*
 * The UTF-8 list (section 11).
 */
static int test_utf8_errors( void )
{
    struct e1r8_tuple t;
    uint8_t buf[8192];
    struct e1r8_answer a;

    fill_golden_tuple( &t );

    /* truncated continuation */
    {
        uint8_t pw[] = { 0xc3 };
        CHECK( e1r8_encode_secret_answer( buf, 157, &t, 0x3ff, pw, 1 ) == E1R8_ERR_UTF8,
               "UTF-8 truncated continuation" );
    }
    /* missing continuation (stray continuation byte) */
    {
        uint8_t pw[] = { 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 157, &t, 0x3ff, pw, 1 ) == E1R8_ERR_UTF8,
               "UTF-8 stray continuation" );
    }
    /* overlong C0 80 */
    {
        uint8_t pw[] = { 0xc0, 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 158, &t, 0x3ff, pw, 2 ) == E1R8_ERR_UTF8,
               "UTF-8 overlong C0 80" );
    }
    /* overlong E0 80 80 */
    {
        uint8_t pw[] = { 0xe0, 0x80, 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 159, &t, 0x3ff, pw, 3 ) == E1R8_ERR_UTF8,
               "UTF-8 overlong E0 80 80" );
    }
    /* overlong F0 80 80 80 */
    {
        uint8_t pw[] = { 0xf0, 0x80, 0x80, 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 160, &t, 0x3ff, pw, 4 ) == E1R8_ERR_UTF8,
               "UTF-8 overlong F0 80 80 80" );
    }
    /* encoded surrogate ED A0 80 */
    {
        uint8_t pw[] = { 0xed, 0xa0, 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 159, &t, 0x3ff, pw, 3 ) == E1R8_ERR_UTF8,
               "UTF-8 encoded surrogate ED A0 80" );
    }
    /* encoded surrogate ED BF BF */
    {
        uint8_t pw[] = { 0xed, 0xbf, 0xbf };
        CHECK( e1r8_encode_secret_answer( buf, 159, &t, 0x3ff, pw, 3 ) == E1R8_ERR_UTF8,
               "UTF-8 encoded surrogate ED BF BF" );
    }
    /* scalar above U+10FFFF F4 90 80 80 */
    {
        uint8_t pw[] = { 0xf4, 0x90, 0x80, 0x80 };
        CHECK( e1r8_encode_secret_answer( buf, 160, &t, 0x3ff, pw, 4 ) == E1R8_ERR_UTF8,
               "UTF-8 above U+10FFFF" );
    }
    /* bytes F5..FF */
    {
        uint8_t pw[] = { 0xf5 };
        CHECK( e1r8_encode_secret_answer( buf, 157, &t, 0x3ff, pw, 1 ) == E1R8_ERR_UTF8,
               "UTF-8 F5" );
        pw[0] = 0xff;
        CHECK( e1r8_encode_secret_answer( buf, 157, &t, 0x3ff, pw, 1 ) == E1R8_ERR_UTF8,
               "UTF-8 FF" );
    }
    /* embedded NUL */
    {
        uint8_t pw[] = { 0x70, 0x00, 0x73 };
        CHECK( e1r8_encode_secret_answer( buf, 159, &t, 0x3ff, pw, 3 ) == E1R8_ERR_UTF8,
               "UTF-8 embedded NUL" );
    }
    /* valid 4096-byte payload accepted */
    {
        uint8_t pw[4096];
        int i;
        for (i = 0; i < 4096; ++i) pw[i] = (uint8_t)('a' + (i % 26));
        CHECK( e1r8_encode_secret_answer( buf, 156 + 4096, &t, 0x3ff, pw, 4096 ) == E1R8_OK,
               "UTF-8 4096 accepted" );
        CHECK( e1r8_decode_secret_answer( buf, 156 + 4096, &t, 0x3ff, &a ) == E1R8_OK,
               "UTF-8 4096 decode" );
    }
    /* 4097-byte payload refused */
    {
        uint8_t pw[4097];
        int i;
        for (i = 0; i < 4097; ++i) pw[i] = (uint8_t)('a' + (i % 26));
        CHECK( e1r8_encode_secret_answer( buf, 156 + 4097, &t, 0x3ff, pw, 4097 ) == E1R8_ERR_UTF8,
               "UTF-8 4097 refused" );
    }
    /* empty password payload refused */
    {
        uint8_t pw[1] = { 0x70 };
        CHECK( e1r8_encode_secret_answer( buf, 156, &t, 0x3ff, pw, 0 ) == E1R8_ERR_UTF8,
               "empty password payload refused" );
    }
    return 0;
}

/*
 * R9 (docs/E1_S18E_R9_SPEC.md) round trips at minimum and maximum length
 * for every new message type.
 */
static int test_r9_round_trips( void )
{
    struct e1r8_tuple t, t2;
    uint8_t buf[8192];
    uint8_t emb[8192];
    uint8_t key[16], ev[16];
    uint8_t chan[16];
    int i;

    fill_golden_tuple( &t );
    t2 = t;
    t2.attempt = 2;
    fill_range( key, 16, 0xa0 );
    fill_range( ev, 16, 0xb0 );
    fill_range( chan, 16, 0x50 );

    /* Type 32: GRANT (108). */
    {
        struct e1r8_grant g;
        CHECK( e1r8_encode_grant( buf, 108, &t, key, ev ) == E1R8_OK, "GRANT encode" );
        CHECK( e1r8_decode_grant( buf, 108, &t, &g ) == E1R8_OK, "GRANT decode" );
        CHECK( memcmp( g.account_key, key, 16 ) == 0 && memcmp( g.evidence_ref, ev, 16 ) == 0,
               "GRANT fields" );
    }

    /* Type 33: GRANTED (76). */
    CHECK( e1r8_encode_granted( buf, 76, &t ) == E1R8_OK, "GRANTED encode" );
    CHECK( e1r8_decode_granted( buf, 76, &t ) == E1R8_OK, "GRANTED decode" );

    /* Type 40: REQUEST (200..1760). */
    {
        struct e1r8_request r;
        /* Minimum: empty display strings. */
        CHECK( e1r8_encode_request( buf, 200, &t, 1, chan, t.binding_digest, t.request_digest,
                                    t.server_gen, t.desktop_route, 1,
                                    NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
               "REQUEST min encode" );
        CHECK( e1r8_decode_request( buf, 200, &t, &r ) == E1R8_OK, "REQUEST min decode" );
        CHECK( r.kind == 1 && r.secure_desktop == 1, "REQUEST min fields" );
        CHECK( memcmp( r.channel_id, chan, 16 ) == 0, "REQUEST min channel" );
        CHECK( r.requester_len == 0 && r.image_len == 0 && r.publisher_len == 0,
               "REQUEST min empty display" );
        /* Maximum: 520-byte display strings. */
        {
            uint8_t req[520], img[520], pub[520];
            for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
            for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
            for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
            CHECK( e1r8_encode_request( buf, 1760, &t, 2, chan, t.binding_digest, t.request_digest,
                                        t.server_gen, t.desktop_route, 0,
                                        req, 520, img, 520, pub, 520 ) == E1R8_OK,
                   "REQUEST max encode" );
            CHECK( e1r8_decode_request( buf, 1760, &t, &r ) == E1R8_OK, "REQUEST max decode" );
            CHECK( r.kind == 2 && r.secure_desktop == 0, "REQUEST max fields" );
            CHECK( r.requester_len == 520 && r.image_len == 520 && r.publisher_len == 520,
                   "REQUEST max display len" );
        }
    }

    /* Type 41: DECISION (84 deny, 284 prompt). */
    {
        struct e1r8_decision d;
        CHECK( e1r8_encode_decision( buf, 84, &t, 2, 1 ) == E1R8_OK, "DECISION deny encode" );
        CHECK( e1r8_decode_decision( buf, 84, &t, &d ) == E1R8_OK, "DECISION deny decode" );
        CHECK( d.status == 2 && d.reason == 1, "DECISION deny fields" );
        CHECK( e1r8_encode_decision( buf, 284, &t, 1, 0 ) == E1R8_OK, "DECISION prompt encode" );
        CHECK( e1r8_decode_decision( buf, 284, &t, &d ) == E1R8_OK, "DECISION prompt decode" );
        CHECK( d.status == 1, "DECISION prompt status" );
        CHECK( memcmp( &d.tuple, &t, sizeof(t) ) == 0, "DECISION prompt tuple" );
    }

    /* Type 42: ENDORSED_LOCAL (96). */
    {
        struct e1r8_endorsed_local e;
        CHECK( e1r8_encode_endorsed_local( buf, 96, &t, t.dialog_id, 1 ) == E1R8_OK,
               "ENDORSED_LOCAL encode" );
        CHECK( e1r8_decode_endorsed_local( buf, 96, &t, &e ) == E1R8_OK,
               "ENDORSED_LOCAL decode" );
        CHECK( e.status == 1 && memcmp( e.dialog_id, t.dialog_id, 16 ) == 0,
               "ENDORSED_LOCAL fields" );
    }

    /* Type 43: TO_D (152..2143). */
    {
        struct e1r8_to_d td;
        /* Minimum: embedded SELECT_OK (76). */
        CHECK( e1r8_encode_select_ok( emb, 76, &t ) == E1R8_OK, "TO_D emb SELECT_OK" );
        CHECK( e1r8_encode_to_d( buf, 152, &t, emb, 76 ) == E1R8_OK, "TO_D min encode" );
        CHECK( e1r8_decode_to_d( buf, 152, &t, &td ) == E1R8_OK, "TO_D min decode" );
        CHECK( td.type == E1R8_TYPE_SELECT_OK && td.frame_len == 76, "TO_D min fields" );
        /* Maximum: embedded INIT max (2067). */
        {
            uint8_t vo[255], req[520], img[520], pub[520];
            vo[0] = ':';
            for (i = 1; i < 253; ++i) vo[i] = '1';
            vo[253] = '.'; vo[254] = '1';
            for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
            for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
            for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
            CHECK( e1r8_encode_init( emb, 2067, &t, vo, 255, req, 520, img, 520, pub, 520 ) == E1R8_OK,
                   "TO_D emb INIT" );
            CHECK( e1r8_encode_to_d( buf, 2143, &t, emb, 2067 ) == E1R8_OK, "TO_D max encode" );
            CHECK( e1r8_decode_to_d( buf, 2143, &t, &td ) == E1R8_OK, "TO_D max decode" );
            CHECK( td.type == E1R8_TYPE_INIT && td.frame_len == 2067, "TO_D max fields" );
        }
    }

    /* Type 44: FROM_D (157..340). */
    {
        struct e1r8_from_d fd;
        /* Minimum: embedded SELECT "a" (81). */
        CHECK( e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK,
               "FROM_D emb SELECT" );
        CHECK( e1r8_encode_from_d( buf, 157, &t, emb, 81 ) == E1R8_OK, "FROM_D min encode" );
        CHECK( e1r8_decode_from_d( buf, 157, &t, &fd ) == E1R8_OK, "FROM_D min decode" );
        CHECK( fd.type == E1R8_TYPE_SELECT && fd.frame_len == 81, "FROM_D min fields" );
        /* Maximum: embedded ANSWER (264). */
        CHECK( e1r8_encode_answer( emb, 264, &t, t.binding_digest, 2, 2, 0, 0x3ff ) == E1R8_OK,
               "FROM_D emb ANSWER" );
        CHECK( e1r8_encode_from_d( buf, 340, &t, emb, 264 ) == E1R8_OK, "FROM_D max encode" );
        CHECK( e1r8_decode_from_d( buf, 340, &t, &fd ) == E1R8_OK, "FROM_D max decode" );
        CHECK( fd.type == E1R8_TYPE_ANSWER && fd.frame_len == 264, "FROM_D max fields" );
    }

    /* Type 45: RECHECK (80). */
    {
        struct e1r8_recheck rc;
        CHECK( e1r8_encode_recheck( buf, 80, &t, 1 ) == E1R8_OK, "RECHECK encode" );
        CHECK( e1r8_decode_recheck( buf, 80, &t, &rc ) == E1R8_OK, "RECHECK decode" );
        CHECK( rc.step == 1, "RECHECK step" );
    }

    /* Type 46: RECHECKED (84). */
    {
        struct e1r8_rechecked rc;
        CHECK( e1r8_encode_rechecked( buf, 84, &t, 1, 1 ) == E1R8_OK, "RECHECKED encode" );
        CHECK( e1r8_decode_rechecked( buf, 84, &t, &rc ) == E1R8_OK, "RECHECKED decode" );
        CHECK( rc.step == 1 && rc.status == 1, "RECHECKED fields" );
    }

    /* Type 47: LAUNCH (97..159). */
    {
        struct e1r8_launch l;
        CHECK( e1r8_encode_launch( buf, 97, &t, (const uint8_t *)"a", 1, ev ) == E1R8_OK,
               "LAUNCH min encode" );
        CHECK( e1r8_decode_launch( buf, 97, &t, &l ) == E1R8_OK, "LAUNCH min decode" );
        CHECK( l.account_len == 1 && memcmp( l.evidence_ref, ev, 16 ) == 0, "LAUNCH min fields" );
        CHECK( e1r8_encode_launch( buf, 159, &t,
                                   (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63, ev ) == E1R8_OK,
               "LAUNCH max encode" );
        CHECK( e1r8_decode_launch( buf, 159, &t, &l ) == E1R8_OK, "LAUNCH max decode" );
        CHECK( l.account_len == 63, "LAUNCH max account" );
    }

    /* Type 48: LAUNCHED (84). */
    {
        struct e1r8_launched l;
        CHECK( e1r8_encode_launched( buf, 84, &t, 1, 0 ) == E1R8_OK, "LAUNCHED encode" );
        CHECK( e1r8_decode_launched( buf, 84, &t, &l ) == E1R8_OK, "LAUNCHED decode" );
        CHECK( l.status == 1 && l.win32 == 0, "LAUNCHED fields" );
    }

    /* Type 49: CANCEL_R9 (80). */
    {
        struct e1r8_cancel_r9 c;
        CHECK( e1r8_encode_cancel_r9( buf, 80, &t, 1 ) == E1R8_OK, "CANCEL_R9 encode" );
        CHECK( e1r8_decode_cancel_r9( buf, 80, &t, &c ) == E1R8_OK, "CANCEL_R9 decode" );
        CHECK( c.reason == 1, "CANCEL_R9 reason" );
    }

    /* Type 50: END (80). */
    {
        struct e1r8_end en;
        CHECK( e1r8_encode_end( buf, 80, &t, 1 ) == E1R8_OK, "END encode" );
        CHECK( e1r8_decode_end( buf, 80, &t, &en ) == E1R8_OK, "END decode" );
        CHECK( en.outcome == 1, "END outcome" );
    }

    /* Type 51: NEXT_ATTEMPT (280, attempt 2). */
    {
        struct e1r8_next_attempt na;
        CHECK( e1r8_encode_next_attempt( buf, 280, &t2 ) == E1R8_OK, "NEXT_ATTEMPT encode" );
        CHECK( e1r8_decode_next_attempt( buf, 280, &t2, &na ) == E1R8_OK, "NEXT_ATTEMPT decode" );
        CHECK( memcmp( &na.tuple, &t2, sizeof(t2) ) == 0, "NEXT_ATTEMPT tuple" );
    }

    return 0;
}

/*
 * R9 error vectors: one byte short and long, wrong type, nonzero reserved,
 * unknown enum values, invalid strings, embedded frame errors, and the
 * DECISION/NEXT_ATTEMPT/GRANT id and zero-field checks.
 */
static int test_r9_errors( void )
{
    struct e1r8_tuple t, t2;
    uint8_t buf[8192];
    uint8_t emb[8192];
    uint8_t key[16], ev[16], zero16[16];
    uint8_t chan[16];
    int i;

    fill_golden_tuple( &t );
    t2 = t;
    t2.attempt = 2;
    fill_range( key, 16, 0xa0 );
    fill_range( ev, 16, 0xb0 );
    memset( zero16, 0, 16 );
    fill_range( chan, 16, 0x50 );

    /* --- One byte short and one byte long --- */
    CHECK( e1r8_encode_grant( buf, 108, &t, key, ev ) == E1R8_OK, "GRANT encode" );
    {
        struct e1r8_grant g;
        CHECK( e1r8_decode_grant( buf, 107, &t, &g ) != E1R8_OK, "GRANT one short" );
        buf[108] = 0;
        CHECK( e1r8_decode_grant( buf, 109, &t, &g ) != E1R8_OK, "GRANT one long" );
    }
    CHECK( e1r8_encode_request( buf, 200, &t, 1, chan, t.binding_digest, t.request_digest,
                                t.server_gen, t.desktop_route, 1, NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
           "REQUEST encode" );
    {
        struct e1r8_request r;
        CHECK( e1r8_decode_request( buf, 199, &t, &r ) != E1R8_OK, "REQUEST one short" );
        buf[200] = 0;
        CHECK( e1r8_decode_request( buf, 201, &t, &r ) != E1R8_OK, "REQUEST one long" );
    }
    CHECK( e1r8_encode_decision( buf, 84, &t, 2, 1 ) == E1R8_OK, "DECISION deny encode" );
    {
        struct e1r8_decision d;
        CHECK( e1r8_decode_decision( buf, 83, &t, &d ) != E1R8_OK, "DECISION deny one short" );
        buf[84] = 0;
        CHECK( e1r8_decode_decision( buf, 85, &t, &d ) != E1R8_OK, "DECISION deny one long" );
    }
    CHECK( e1r8_encode_select_ok( emb, 76, &t ) == E1R8_OK, "TO_D emb" );
    CHECK( e1r8_encode_to_d( buf, 152, &t, emb, 76 ) == E1R8_OK, "TO_D encode" );
    {
        struct e1r8_to_d td;
        CHECK( e1r8_decode_to_d( buf, 151, &t, &td ) != E1R8_OK, "TO_D one short" );
        buf[152] = 0;
        CHECK( e1r8_decode_to_d( buf, 153, &t, &td ) != E1R8_OK, "TO_D one long" );
    }
    CHECK( e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "FROM_D emb" );
    CHECK( e1r8_encode_from_d( buf, 157, &t, emb, 81 ) == E1R8_OK, "FROM_D encode" );
    {
        struct e1r8_from_d fd;
        CHECK( e1r8_decode_from_d( buf, 156, &t, &fd ) != E1R8_OK, "FROM_D one short" );
        buf[157] = 0;
        CHECK( e1r8_decode_from_d( buf, 158, &t, &fd ) != E1R8_OK, "FROM_D one long" );
    }
    CHECK( e1r8_encode_launch( buf, 97, &t, (const uint8_t *)"a", 1, ev ) == E1R8_OK, "LAUNCH encode" );
    {
        struct e1r8_launch l;
        CHECK( e1r8_decode_launch( buf, 96, &t, &l ) != E1R8_OK, "LAUNCH one short" );
        buf[97] = 0;
        CHECK( e1r8_decode_launch( buf, 98, &t, &l ) != E1R8_OK, "LAUNCH one long" );
    }
    CHECK( e1r8_encode_next_attempt( buf, 280, &t2 ) == E1R8_OK, "NEXT_ATTEMPT encode" );
    {
        struct e1r8_next_attempt na;
        CHECK( e1r8_decode_next_attempt( buf, 279, &t2, &na ) != E1R8_OK, "NEXT_ATTEMPT one short" );
        buf[280] = 0;
        CHECK( e1r8_decode_next_attempt( buf, 281, &t2, &na ) != E1R8_OK, "NEXT_ATTEMPT one long" );
    }

    /* --- Wrong type and nonzero reserved --- */
    CHECK( e1r8_encode_recheck( buf, 80, &t, 1 ) == E1R8_OK, "RECHECK encode" );
    {
        struct e1r8_recheck rc;
        e1r8_put_u16( buf + 6, 46 ); /* wrong type */
        CHECK( e1r8_decode_recheck( buf, 80, &t, &rc ) == E1R8_ERR_TYPE, "RECHECK wrong type" );
        e1r8_put_u16( buf + 6, E1R8_TYPE_RECHECK );
        buf[24] = 1; /* nonzero reserved */
        CHECK( e1r8_decode_recheck( buf, 80, &t, &rc ) == E1R8_ERR_RESERVED, "RECHECK nonzero reserved" );
        buf[24] = 0;
    }

    /* --- Unknown enum values (0 and max+1) --- */
    CHECK( e1r8_encode_request( buf, 200, &t, 1, chan, t.binding_digest, t.request_digest,
                                t.server_gen, t.desktop_route, 1, NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
           "REQUEST encode" );
    {
        struct e1r8_request r;
        buf[76] = 0; /* kind 0 */
        CHECK( e1r8_decode_request( buf, 200, &t, &r ) == E1R8_ERR_ENUM, "REQUEST kind 0" );
        buf[76] = 3; /* kind 3 (max+1) */
        CHECK( e1r8_decode_request( buf, 200, &t, &r ) == E1R8_ERR_ENUM, "REQUEST kind 3" );
        buf[76] = 1;
        buf[184] = 2; /* secure_desktop 2 (max+1) */
        CHECK( e1r8_decode_request( buf, 200, &t, &r ) == E1R8_ERR_ENUM, "REQUEST secure_desktop 2" );
        buf[184] = 1;
    }
    CHECK( e1r8_encode_decision( buf, 84, &t, 2, 1 ) == E1R8_OK, "DECISION deny encode" );
    {
        struct e1r8_decision d;
        buf[76] = 0; /* status 0 */
        CHECK( e1r8_decode_decision( buf, 84, &t, &d ) == E1R8_ERR_ENUM, "DECISION status 0" );
        buf[76] = 3; /* status 3 (max+1) */
        CHECK( e1r8_decode_decision( buf, 84, &t, &d ) == E1R8_ERR_ENUM, "DECISION status 3" );
        buf[76] = 2;
        buf[80] = 0; /* reason 0 */
        CHECK( e1r8_decode_decision( buf, 84, &t, &d ) == E1R8_ERR_ENUM, "DECISION reason 0" );
        buf[80] = 4; /* reason 4 (max+1) */
        CHECK( e1r8_decode_decision( buf, 84, &t, &d ) == E1R8_ERR_ENUM, "DECISION reason 4" );
        buf[80] = 1;
    }
    CHECK( e1r8_encode_endorsed_local( buf, 96, &t, t.dialog_id, 1 ) == E1R8_OK, "ENDORSED_LOCAL encode" );
    {
        struct e1r8_endorsed_local e;
        buf[92] = 0; /* status 0 */
        CHECK( e1r8_decode_endorsed_local( buf, 96, &t, &e ) == E1R8_ERR_ENUM, "ENDORSED_LOCAL status 0" );
        buf[92] = 3; /* status 3 (max+1) */
        CHECK( e1r8_decode_endorsed_local( buf, 96, &t, &e ) == E1R8_ERR_ENUM, "ENDORSED_LOCAL status 3" );
        buf[92] = 1;
    }
    CHECK( e1r8_encode_recheck( buf, 80, &t, 1 ) == E1R8_OK, "RECHECK encode" );
    {
        struct e1r8_recheck rc;
        buf[76] = 0; /* step 0 */
        CHECK( e1r8_decode_recheck( buf, 80, &t, &rc ) == E1R8_ERR_ENUM, "RECHECK step 0" );
        buf[76] = 5; /* step 5 (max+1) */
        CHECK( e1r8_decode_recheck( buf, 80, &t, &rc ) == E1R8_ERR_ENUM, "RECHECK step 5" );
        buf[76] = 1;
    }
    CHECK( e1r8_encode_rechecked( buf, 84, &t, 1, 1 ) == E1R8_OK, "RECHECKED encode" );
    {
        struct e1r8_rechecked rc;
        buf[76] = 0; /* step 0 */
        CHECK( e1r8_decode_rechecked( buf, 84, &t, &rc ) == E1R8_ERR_ENUM, "RECHECKED step 0" );
        buf[76] = 5; /* step 5 (max+1) */
        CHECK( e1r8_decode_rechecked( buf, 84, &t, &rc ) == E1R8_ERR_ENUM, "RECHECKED step 5" );
        buf[76] = 1;
        buf[80] = 0; /* status 0 */
        CHECK( e1r8_decode_rechecked( buf, 84, &t, &rc ) == E1R8_ERR_ENUM, "RECHECKED status 0" );
        buf[80] = 3; /* status 3 (max+1) */
        CHECK( e1r8_decode_rechecked( buf, 84, &t, &rc ) == E1R8_ERR_ENUM, "RECHECKED status 3" );
        buf[80] = 1;
    }
    CHECK( e1r8_encode_launched( buf, 84, &t, 1, 0 ) == E1R8_OK, "LAUNCHED encode" );
    {
        struct e1r8_launched l;
        buf[76] = 0; /* status 0 */
        CHECK( e1r8_decode_launched( buf, 84, &t, &l ) == E1R8_ERR_ENUM, "LAUNCHED status 0" );
        buf[76] = 4; /* status 4 (max+1) */
        CHECK( e1r8_decode_launched( buf, 84, &t, &l ) == E1R8_ERR_ENUM, "LAUNCHED status 4" );
        buf[76] = 1;
    }
    CHECK( e1r8_encode_cancel_r9( buf, 80, &t, 1 ) == E1R8_OK, "CANCEL_R9 encode" );
    {
        struct e1r8_cancel_r9 c;
        buf[76] = 0; /* reason 0 */
        CHECK( e1r8_decode_cancel_r9( buf, 80, &t, &c ) == E1R8_ERR_ENUM, "CANCEL_R9 reason 0" );
        buf[76] = 7; /* reason 7 (max+1) */
        CHECK( e1r8_decode_cancel_r9( buf, 80, &t, &c ) == E1R8_ERR_ENUM, "CANCEL_R9 reason 7" );
        buf[76] = 1;
    }
    CHECK( e1r8_encode_end( buf, 80, &t, 1 ) == E1R8_OK, "END encode" );
    {
        struct e1r8_end en;
        buf[76] = 0; /* outcome 0 */
        CHECK( e1r8_decode_end( buf, 80, &t, &en ) == E1R8_ERR_ENUM, "END outcome 0" );
        buf[76] = 4; /* outcome 4 (max+1) */
        CHECK( e1r8_decode_end( buf, 80, &t, &en ) == E1R8_ERR_ENUM, "END outcome 4" );
        buf[76] = 1;
    }

    /* --- Invalid strings --- */
    /* REQUEST: display string with odd length. */
    CHECK( e1r8_encode_request( buf, 201, &t, 1, chan, t.binding_digest, t.request_digest,
                                t.server_gen, t.desktop_route, 1,
                                (const uint8_t *)"a", 1, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
           "REQUEST display odd length" );
    /* REQUEST: display string overlength (522). */
    {
        uint8_t big[522];
        for (i = 0; i < 522; i += 2) { big[i] = 'a'; big[i+1] = 0; }
        CHECK( e1r8_encode_request( buf, 200 + 522, &t, 1, chan, t.binding_digest, t.request_digest,
                                    t.server_gen, t.desktop_route, 1,
                                    big, 522, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
               "REQUEST display overlength" );
    }
    /* REQUEST: display string with NUL. */
    {
        uint16_t nul[4];
        nul[0] = 0; nul[1] = 0; nul[2] = 'b'; nul[3] = 0;
        CHECK( e1r8_encode_request( buf, 204, &t, 1, chan, t.binding_digest, t.request_digest,
                                    t.server_gen, t.desktop_route, 1,
                                    (const uint8_t *)nul, 4, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
               "REQUEST display NUL" );
    }
    /* REQUEST: display string with control character. */
    {
        uint16_t ctrl[2];
        ctrl[0] = 0x01; ctrl[1] = 0;
        CHECK( e1r8_encode_request( buf, 202, &t, 1, chan, t.binding_digest, t.request_digest,
                                    t.server_gen, t.desktop_route, 1,
                                    (const uint8_t *)ctrl, 2, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
               "REQUEST display control" );
    }
    /* REQUEST: display string with unpaired surrogate. */
    {
        uint16_t surr[2];
        surr[0] = 0xd800; surr[1] = 0;
        CHECK( e1r8_encode_request( buf, 202, &t, 1, chan, t.binding_digest, t.request_digest,
                                    t.server_gen, t.desktop_route, 1,
                                    (const uint8_t *)surr, 2, NULL, 0, NULL, 0 ) == E1R8_ERR_STRING,
               "REQUEST display unpaired surrogate" );
    }
    /* LAUNCH: non-ASCII account name. */
    {
        uint8_t bad[] = { 'a', 0x80, 'b' };
        CHECK( e1r8_encode_launch( buf, 99, &t, bad, 3, ev ) == E1R8_ERR_STRING,
               "LAUNCH non-ASCII account" );
    }
    /* LAUNCH: 64-byte account (overlength). */
    CHECK( e1r8_encode_launch( buf, 160, &t,
                               (const uint8_t *)"a123456789012345678901234567890123456789012345678901234567890123", 64, ev )
           == E1R8_ERR_STRING, "LAUNCH overlength account" );

    /* --- TO_D / FROM_D embedded frame errors --- */
    /* TO_D: embedded frame of a wrong type (ACK is not a B->D type). */
    CHECK( e1r8_encode_ack( emb, 156, &t ) == E1R8_OK, "TO_D emb ACK" );
    CHECK( e1r8_encode_to_d( buf, 76 + 156, &t, emb, 156 ) == E1R8_ERR_TYPE,
           "TO_D embedded wrong type" );
    /* TO_D: embedded length disagrees. */
    CHECK( e1r8_encode_select_ok( emb, 76, &t ) == E1R8_OK, "TO_D emb SELECT_OK" );
    e1r8_put_u32( emb + 8, 77 );
    CHECK( e1r8_encode_to_d( buf, 76 + 76, &t, emb, 76 ) == E1R8_ERR_LENGTH,
           "TO_D embedded length disagrees" );
    /* TO_D: embedded frame below its type's bounds (INIT 255). */
    {
        uint8_t vo[4] = { ':', '1', '.', '1' };
        CHECK( e1r8_encode_init( emb, 256, &t, vo, 4, NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
               "TO_D emb INIT" );
        e1r8_put_u32( emb + 8, 255 );
        CHECK( e1r8_encode_to_d( buf, 76 + 255, &t, emb, 255 ) == E1R8_ERR_TYPE,
               "TO_D embedded below bounds" );
    }
    /* TO_D: embedded frame above its type's bounds (INIT 2068). */
    {
        uint8_t vo[255], req[520], img[520], pub[520];
        vo[0] = ':';
        for (i = 1; i < 253; ++i) vo[i] = '1';
        vo[253] = '.'; vo[254] = '1';
        for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
        CHECK( e1r8_encode_init( emb, 2067, &t, vo, 255, req, 520, img, 520, pub, 520 ) == E1R8_OK,
               "TO_D emb INIT max" );
        e1r8_put_u32( emb + 8, 2068 );
        CHECK( e1r8_encode_to_d( buf, 76 + 2068, &t, emb, 2068 ) == E1R8_ERR_TYPE,
               "TO_D embedded above bounds" );
    }
    /* FROM_D: embedded frame of a wrong type (INIT is not a D->B type). */
    {
        uint8_t vo[4] = { ':', '1', '.', '1' };
        CHECK( e1r8_encode_init( emb, 256, &t, vo, 4, NULL, 0, NULL, 0, NULL, 0 ) == E1R8_OK,
               "FROM_D emb INIT" );
        CHECK( e1r8_encode_from_d( buf, 76 + 256, &t, emb, 256 ) == E1R8_ERR_TYPE,
               "FROM_D embedded wrong type" );
    }
    /* FROM_D: embedded length disagrees. */
    CHECK( e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "FROM_D emb SELECT" );
    e1r8_put_u32( emb + 8, 82 );
    CHECK( e1r8_encode_from_d( buf, 76 + 81, &t, emb, 81 ) == E1R8_ERR_LENGTH,
           "FROM_D embedded length disagrees" );
    /* FROM_D: embedded frame below its type's bounds (SELECT 80). */
    CHECK( e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "FROM_D emb SELECT" );
    e1r8_put_u32( emb + 8, 80 );
    CHECK( e1r8_encode_from_d( buf, 76 + 80, &t, emb, 80 ) == E1R8_ERR_TYPE,
           "FROM_D embedded below bounds" );
    /* FROM_D: embedded frame above its type's bounds (SELECT 144). */
    CHECK( e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 ) == E1R8_OK, "FROM_D emb SELECT" );
    e1r8_put_u32( emb + 8, 144 );
    CHECK( e1r8_encode_from_d( buf, 76 + 144, &t, emb, 144 ) == E1R8_ERR_TYPE,
           "FROM_D embedded above bounds" );
    /* TO_D decode: embedded frame of a wrong type. */
    {
        struct e1r8_to_d td;
        CHECK( e1r8_encode_select_ok( emb, 76, &t ) == E1R8_OK, "TO_D emb" );
        CHECK( e1r8_encode_to_d( buf, 152, &t, emb, 76 ) == E1R8_OK, "TO_D encode" );
        e1r8_put_u16( buf + 76 + 6, E1R8_TYPE_ACK );
        CHECK( e1r8_decode_to_d( buf, 152, &t, &td ) == E1R8_ERR_TYPE,
               "TO_D decode embedded wrong type" );
    }
    /* TO_D decode: embedded length disagrees. */
    {
        struct e1r8_to_d td;
        CHECK( e1r8_encode_select_ok( emb, 76, &t ) == E1R8_OK, "TO_D emb" );
        CHECK( e1r8_encode_to_d( buf, 152, &t, emb, 76 ) == E1R8_OK, "TO_D encode" );
        e1r8_put_u32( buf + 76 + 8, 77 );
        CHECK( e1r8_decode_to_d( buf, 152, &t, &td ) == E1R8_ERR_LENGTH,
               "TO_D decode embedded length disagrees" );
    }

    /* --- DECISION / NEXT_ATTEMPT header ids differ from the tuple's --- */
    CHECK( e1r8_encode_decision( buf, 284, &t, 1, 0 ) == E1R8_OK, "DECISION prompt encode" );
    {
        struct e1r8_decision d;
        buf[28] = 0xff; /* header nonce differs from the tuple's */
        CHECK( e1r8_decode_decision( buf, 284, &t, &d ) == E1R8_ERR_ID,
               "DECISION prompt header nonce differs" );
        buf[28] = 0x10;
    }
    CHECK( e1r8_encode_next_attempt( buf, 280, &t2 ) == E1R8_OK, "NEXT_ATTEMPT encode" );
    {
        struct e1r8_next_attempt na;
        buf[28] = 0xff; /* header nonce differs from the tuple's */
        CHECK( e1r8_decode_next_attempt( buf, 280, &t2, &na ) == E1R8_ERR_ID,
               "NEXT_ATTEMPT header nonce differs" );
        buf[28] = 0x10;
    }

    /* --- NEXT_ATTEMPT with attempt 1 --- */
    CHECK( e1r8_encode_next_attempt( buf, 280, &t ) == E1R8_ERR_ENUM,
           "NEXT_ATTEMPT attempt 1 refused" );

    /* --- GRANT with a zero key or zero reference --- */
    CHECK( e1r8_encode_grant( buf, 108, &t, zero16, ev ) == E1R8_ERR_ID,
           "GRANT zero key refused" );
    CHECK( e1r8_encode_grant( buf, 108, &t, key, zero16 ) == E1R8_ERR_ID,
           "GRANT zero reference refused" );
    {
        struct e1r8_grant g;
        CHECK( e1r8_encode_grant( buf, 108, &t, key, ev ) == E1R8_OK, "GRANT encode" );
        memset( buf + 76, 0, 16 ); /* zero the account_key */
        CHECK( e1r8_decode_grant( buf, 108, &t, &g ) == E1R8_ERR_ID,
               "GRANT decode zero key refused" );
        memcpy( buf + 76, key, 16 );
        memset( buf + 92, 0, 16 ); /* zero the evidence_ref */
        CHECK( e1r8_decode_grant( buf, 108, &t, &g ) == E1R8_ERR_ID,
               "GRANT decode zero reference refused" );
    }

    return 0;
}

/*
 * Golden output: one line per message type, "<type> <hex>".
 */
static int golden_output( void )
{
    struct e1r8_tuple t, t2;
    uint8_t buf[8192];
    uint8_t emb[8192];
    uint16_t req_utf16[300], img_utf16[300];
    size_t req_len, img_len;
    uint8_t key[16], ev[16], chal[32];
    uint8_t chan[16];
    const uint8_t pw[] = { 0x70, 0xc3, 0xa4, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64 };
    int err, i;

    fill_golden_tuple( &t );
    t2 = t;
    t2.attempt = 2;
    req_len = to_utf16le( "Caller", req_utf16 );
    img_len = to_utf16le( "x.exe", img_utf16 );
    fill_range( key, 16, 0xa0 );
    fill_range( ev, 16, 0xb0 );
    fill_range( chal, 32, 0xc0 );
    fill_range( chan, 16, 0x50 );

    /* Type 1: INIT */
    {
        uint32_t total = 76 + 160 + (4 + 5) + (4 + (uint32_t)req_len) + (4 + (uint32_t)img_len) + 4;
        err = e1r8_encode_init( buf, total, &t, (const uint8_t *)":1.42", 5,
                                (const uint8_t *)req_utf16, (uint32_t)req_len,
                                (const uint8_t *)img_utf16, (uint32_t)img_len, NULL, 0 );
        if (err) return 1;
        printf( "1 " );
        print_hex( buf, total );
    }

    /* Type 2: ACK */
    if (e1r8_encode_ack( buf, 156, &t )) return 1;
    printf( "2 " ); print_hex( buf, 156 );

    /* Type 3: SELECT_OK */
    if (e1r8_encode_select_ok( buf, 76, &t )) return 1;
    printf( "3 " ); print_hex( buf, 76 );

    /* Type 4: SELECT */
    if (e1r8_encode_select( buf, 85, &t, (const uint8_t *)"admin", 5 )) return 1;
    printf( "4 " ); print_hex( buf, 85 );

    /* Type 5: READY */
    if (e1r8_encode_ready( buf, 76, &t )) return 1;
    printf( "5 " ); print_hex( buf, 76 );

    /* Type 6: RESULT (authenticated) */
    if (e1r8_encode_result( buf, 76 + 4 + (4 + 5) + 32, &t, 1, (const uint8_t *)"admin", 5,
                            chal )) return 1;
    printf( "6 " ); print_hex( buf, 76 + 4 + (4 + 5) + 32 );

    /* Type 7: ANSWER (Confirm) */
    if (e1r8_encode_answer( buf, 264, &t, chal, 2, 2, 0, 0x3ff )) return 1;
    printf( "7 " ); print_hex( buf, 264 );

    /* Type 8: DONE (completed) */
    if (e1r8_encode_done( buf, 80, &t, 1 )) return 1;
    printf( "8 " ); print_hex( buf, 80 );

    /* Type 9: ATTACH_OK */
    if (e1r8_encode_attach_ok( buf, 76, &t )) return 1;
    printf( "9 " ); print_hex( buf, 76 );

    /* Type 16: ENDORSED */
    if (e1r8_encode_endorsed( buf, 244, &t, 4242, 0 )) return 1;
    printf( "16 " ); print_hex( buf, 244 );

    /* Type 17: START */
    if (e1r8_encode_start( buf, 76 + (4 + 5) + 16, &t, (const uint8_t *)"admin", 5, key )) return 1;
    printf( "17 " ); print_hex( buf, 76 + (4 + 5) + 16 );

    /* Type 18: WORKER_READY */
    if (e1r8_encode_worker_ready( buf, 76, &t )) return 1;
    printf( "18 " ); print_hex( buf, 76 );

    /* Type 19: DELEGATED */
    if (e1r8_encode_delegated( buf, 76, &t )) return 1;
    printf( "19 " ); print_hex( buf, 76 );

    /* Type 20: RESULT_V (authenticated) */
    if (e1r8_encode_result_v( buf, 76 + 4 + 16 + (4 + 5) + 16, &t, 1, key,
                              (const uint8_t *)"admin", 5, ev )) return 1;
    printf( "20 " ); print_hex( buf, 76 + 4 + 16 + (4 + 5) + 16 );

    /* Type 21: CANCEL */
    if (e1r8_encode_cancel( buf, 80, &t, 1 )) return 1;
    printf( "21 " ); print_hex( buf, 80 );

    /* Type 22: W_ACK */
    if (e1r8_encode_w_ack( buf, 76, &t )) return 1;
    printf( "22 " ); print_hex( buf, 76 );

    /* Type 23: ATTACH_ACK */
    if (e1r8_encode_attach_ack( buf, 76, &t )) return 1;
    printf( "23 " ); print_hex( buf, 76 );

    /* Type 24: PROMPT_RELEASE */
    if (e1r8_encode_prompt_release( buf, 76, &t )) return 1;
    printf( "24 " ); print_hex( buf, 76 );

    /* Type 25: RELEASE (completed) */
    if (e1r8_encode_release( buf, 80, &t, 1 )) return 1;
    printf( "25 " ); print_hex( buf, 80 );

    /* Type 26: RELEASED */
    if (e1r8_encode_released( buf, 76, &t )) return 1;
    printf( "26 " ); print_hex( buf, 76 );

    /* Type 27: W_SETUP */
    if (e1r8_encode_w_setup( buf, 244, &t, 4242, 0 )) return 1;
    printf( "27 " ); print_hex( buf, 244 );

    /* S7 version 2 PROMPT */
    if (e1r8_encode_s7_prompt( buf, 178, &t )) return 1;
    printf( "s7 " ); print_hex( buf, 178 );

    /* Secret password answer */
    if (e1r8_encode_secret_answer( buf, 156 + 9, &t, 0x3ff, pw, 9 )) return 1;
    printf( "secret " ); print_hex( buf, 156 + 9 );

    /* Type 32: GRANT */
    if (e1r8_encode_grant( buf, 108, &t, key, ev )) return 1;
    printf( "32 " ); print_hex( buf, 108 );

    /* Type 33: GRANTED */
    if (e1r8_encode_granted( buf, 76, &t )) return 1;
    printf( "33 " ); print_hex( buf, 76 );

    /* Type 40: REQUEST (min, empty display strings) */
    if (e1r8_encode_request( buf, 200, &t, 1, chan, t.binding_digest, t.request_digest,
                             t.server_gen, t.desktop_route, 1, NULL, 0, NULL, 0, NULL, 0 )) return 1;
    printf( "40 " ); print_hex( buf, 200 );

    /* Type 40: REQUEST (max, 520-byte display strings) */
    {
        uint8_t req[520], img[520], pub[520];
        for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
        if (e1r8_encode_request( buf, 1760, &t, 2, chan, t.binding_digest, t.request_digest,
                                 t.server_gen, t.desktop_route, 0,
                                 req, 520, img, 520, pub, 520 )) return 1;
        printf( "40 " ); print_hex( buf, 1760 );
    }

    /* Type 41: DECISION (deny) */
    if (e1r8_encode_decision( buf, 84, &t, 2, 1 )) return 1;
    printf( "41 " ); print_hex( buf, 84 );

    /* Type 41: DECISION (prompt) */
    if (e1r8_encode_decision( buf, 284, &t, 1, 0 )) return 1;
    printf( "41 " ); print_hex( buf, 284 );

    /* Type 42: ENDORSED_LOCAL */
    if (e1r8_encode_endorsed_local( buf, 96, &t, t.dialog_id, 1 )) return 1;
    printf( "42 " ); print_hex( buf, 96 );

    /* Type 43: TO_D (min, embedded SELECT_OK) */
    if (e1r8_encode_select_ok( emb, 76, &t )) return 1;
    if (e1r8_encode_to_d( buf, 152, &t, emb, 76 )) return 1;
    printf( "43 " ); print_hex( buf, 152 );

    /* Type 43: TO_D (max, embedded INIT max) */
    {
        uint8_t vo[255], req[520], img[520], pub[520];
        vo[0] = ':';
        for (i = 1; i < 253; ++i) vo[i] = '1';
        vo[253] = '.'; vo[254] = '1';
        for (i = 0; i < 520; i += 2) { req[i] = 'a'; req[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { img[i] = 'b'; img[i+1] = 0; }
        for (i = 0; i < 520; i += 2) { pub[i] = 'c'; pub[i+1] = 0; }
        if (e1r8_encode_init( emb, 2067, &t, vo, 255, req, 520, img, 520, pub, 520 )) return 1;
        if (e1r8_encode_to_d( buf, 2143, &t, emb, 2067 )) return 1;
        printf( "43 " ); print_hex( buf, 2143 );
    }

    /* Type 44: FROM_D (min, embedded SELECT "a") */
    if (e1r8_encode_select( emb, 81, &t, (const uint8_t *)"a", 1 )) return 1;
    if (e1r8_encode_from_d( buf, 157, &t, emb, 81 )) return 1;
    printf( "44 " ); print_hex( buf, 157 );

    /* Type 44: FROM_D (max, embedded ANSWER) */
    if (e1r8_encode_answer( emb, 264, &t, t.binding_digest, 2, 2, 0, 0x3ff )) return 1;
    if (e1r8_encode_from_d( buf, 340, &t, emb, 264 )) return 1;
    printf( "44 " ); print_hex( buf, 340 );

    /* Type 45: RECHECK */
    if (e1r8_encode_recheck( buf, 80, &t, 1 )) return 1;
    printf( "45 " ); print_hex( buf, 80 );

    /* Type 46: RECHECKED */
    if (e1r8_encode_rechecked( buf, 84, &t, 1, 1 )) return 1;
    printf( "46 " ); print_hex( buf, 84 );

    /* Type 47: LAUNCH (min) */
    if (e1r8_encode_launch( buf, 97, &t, (const uint8_t *)"a", 1, ev )) return 1;
    printf( "47 " ); print_hex( buf, 97 );

    /* Type 47: LAUNCH (max) */
    if (e1r8_encode_launch( buf, 159, &t,
                            (const uint8_t *)"a12345678901234567890123456789012345678901234567890123456789012", 63, ev )) return 1;
    printf( "47 " ); print_hex( buf, 159 );

    /* Type 48: LAUNCHED */
    if (e1r8_encode_launched( buf, 84, &t, 1, 0 )) return 1;
    printf( "48 " ); print_hex( buf, 84 );

    /* Type 49: CANCEL_R9 */
    if (e1r8_encode_cancel_r9( buf, 80, &t, 1 )) return 1;
    printf( "49 " ); print_hex( buf, 80 );

    /* Type 50: END */
    if (e1r8_encode_end( buf, 80, &t, 1 )) return 1;
    printf( "50 " ); print_hex( buf, 80 );

    /* Type 51: NEXT_ATTEMPT (attempt 2) */
    if (e1r8_encode_next_attempt( buf, 280, &t2 )) return 1;
    printf( "51 " ); print_hex( buf, 280 );

    (void)err;
    return 0;
}

/*
 * D and W learn the tuple from INIT and W_SETUP; a string length near
 * 2^32 must be refused without wrapping (32-bit size_t on i686 PE).
 */
static int test_learn_and_huge_length( void )
{
    struct e1r8_tuple t, other;
    struct e1r8_init in;
    struct e1r8_w_setup ws;
    uint8_t vo[4] = { ':', '1', '.', '1' };
    uint8_t buf[512];
    int err;

    fill_golden_tuple( &t );
    err = e1r8_encode_init( buf, 256, &t, vo, 4, NULL, 0, NULL, 0, NULL, 0 );
    CHECK( err == E1R8_OK, "learn: INIT encode" );
    err = e1r8_decode_init_learn( buf, 256, t.request_id, 1, t.channel_id, &in );
    CHECK( err == E1R8_OK, "learn: INIT decode" );
    CHECK( !memcmp( &in.tuple, &t, sizeof(t) ), "learn: INIT tuple equals the sender's" );
    CHECK( e1r8_decode_init_learn( buf, 256, t.request_id + 1, 1, t.channel_id, &in ) == E1R8_ERR_ID,
           "learn: INIT wrong request id refused" );
    CHECK( e1r8_decode_init_learn( buf, 256, t.request_id, 2, t.channel_id, &in ) == E1R8_ERR_ID,
           "learn: INIT wrong attempt refused" );
    other = t;
    other.channel_id[0] ^= 1;
    CHECK( e1r8_decode_init_learn( buf, 256, t.request_id, 1, other.channel_id, &in ) == E1R8_ERR_ID,
           "learn: INIT wrong channel refused" );
    buf[76 + 140] = 3; /* desktop_mode */
    CHECK( e1r8_decode_init_learn( buf, 256, t.request_id, 1, t.channel_id, &in ) == E1R8_ERR_ENUM,
           "learn: INIT bad desktop_mode refused" );
    buf[76 + 140] = 1;
    buf[0] = 'X';
    CHECK( e1r8_decode_init_learn( buf, 256, t.request_id, 1, t.channel_id, &in ) == E1R8_ERR_MAGIC,
           "learn: INIT bad magic still refused" );
    buf[0] = 'E';
    /* verifier owner length 0xfffffffc: must not wrap past the buffer */
    buf[236] = 0xfc; buf[237] = 0xff; buf[238] = 0xff; buf[239] = 0xff;
    CHECK( e1r8_decode_init( buf, 256, &t, &in ) == E1R8_ERR_SHORT, "huge string length refused" );
    buf[236] = 0xff;
    CHECK( e1r8_decode_init( buf, 256, &t, &in ) == E1R8_ERR_SHORT, "max string length refused" );

    err = e1r8_encode_w_setup( buf, 244, &t, 4242, 0 );
    CHECK( err == E1R8_OK, "learn: W_SETUP encode" );
    err = e1r8_decode_w_setup_learn( buf, 244, t.request_id, 1, t.channel_id, &ws );
    CHECK( err == E1R8_OK && ws.pid == 4242 && ws.uid == 0, "learn: W_SETUP decode" );
    CHECK( !memcmp( &ws.tuple, &t, sizeof(t) ), "learn: W_SETUP tuple equals the sender's" );
    CHECK( e1r8_decode_w_setup_learn( buf, 243, t.request_id, 1, t.channel_id, &ws ) == E1R8_ERR_LONG,
           "learn: W_SETUP 243 refused" );
    return 0;
}

int main( int argc, char **argv )
{
    if (argc > 1 && !strcmp( argv[1], "golden" ))
        return golden_output();

    printf( "==> Running E1R8 codec tests\n" );

    printf( "1. Round trips at min and max length for every message type...\n" );
    if (test_round_trips()) return 1;

    printf( "2. One byte short and long...\n" );
    if (test_length_errors()) return 1;

    printf( "3. Wrong magic/version/type/reserved...\n" );
    if (test_header_errors()) return 1;

    printf( "4. Unknown enum values...\n" );
    if (test_enum_errors()) return 1;

    printf( "5. Each bad string kind...\n" );
    if (test_string_errors()) return 1;

    printf( "6. Bad S1 fields...\n" );
    if (test_s1_errors()) return 1;

    printf( "7. Refused RESULT with a nonzero field...\n" );
    if (test_refused_result_errors()) return 1;

    printf( "8. UTF-8 list, 4096 accepted, 4097 refused, empty payload refused...\n" );
    if (test_utf8_errors()) return 1;

    printf( "9. Pre-endorsement and sealed tuple round trips at section 4 offsets...\n" );
    if (test_tuples()) return 1;

    printf( "10. Tuple learned from INIT and W_SETUP; huge string length...\n" );
    if (test_learn_and_huge_length()) return 1;

    printf( "11. R9 round trips at min and max length...\n" );
    if (test_r9_round_trips()) return 1;

    printf( "12. R9 length, header, enum, string, embedded and id errors...\n" );
    if (test_r9_errors()) return 1;

    if (g_failed == 0)
    {
        printf( "==> ALL E1R8 codec tests PASSED successfully.\n" );
        return 0;
    }
    fprintf( stderr, "==> SOME E1R8 codec tests FAILED.\n" );
    return 1;
}
