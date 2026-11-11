/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Disabled E1-S6 lab transport. Bytes, never native structure layouts.
 */
#ifndef WEX_E1S6_PROTOCOL_H
#define WEX_E1S6_PROTOCOL_H

#define E1S6_REG_SIZE 112
#define E1S6_OPEN_SIZE (2 * E1S6_REG_SIZE)
#define E1S6_ANSWER_SIZE 156
#define E1S6_ENDORSE_SIZE 96
#define E1S6_REDEEM_REQ_SIZE 252
#define E1S6_REDEEM_RESP_SIZE 240
#define E1S6_BUS "org.wineexplorer.E1S6Lab"
#define E1S6_PATH "/org/wineexplorer/E1S6Lab"
/* No S1 readiness capability is advertised by this metadata-only transport. */
#define E1S6_OPEN 1
#define E1S6_CHILD_START 2
#define E1S6_EXCHANGE 3
#define E1S6_CLOSE 4
/* Fault injections exist only inside WEX_E1S6_TRANSPORT_TEST Unix builds. */
#define E1S6_TEST_KILL_CHILD 5
#define E1S6_TEST_WRONG_OWNER 6
#define E1S6_TEST_DISCONNECT 7
#define E1S6_TEST_CHILD_IMPOSTOR 8
/* E1-S18d operations */
#define E1S6_REDEEM 9
/* Retired, never reuse. The storage generation now comes from the wineserver. */
#define E1S6_STORAGE_QUERY 10
/* WO-25: the verifier's unique bus name recorded at OPEN, for the R8 broker. */
#define E1S6_OWNER_QUERY 11
/* WO-28: send an R8 START (97..159 bytes) on the data socket; the buffer returns the 76-byte WORKER_READY. */
#define E1S6_R8_START 12
/* R8 steps 5-8 (lab). Broker side, on the open channel:
 * RECV: the 512-byte buffer carries a timeout in milliseconds (u32) on input and one E1R8 frame from the
 *       verifier on output (its length is the header's length field).
 * SEND: the buffer is one E1R8 frame of exactly `size` bytes (76..512). */
#define E1S6_R8_RECV 13
#define E1S6_R8_SEND 14
#define E1S6_R8_FRAME_MAX 512
/* Dialog side (consent.exe, no S6 channel; pipe and channel are 0):
 * ATTACH: 512-byte buffer. In: verifier owner (NUL-terminated) at 0, request_id u64 at 256, attempt u32 at
 *         264, nonce at 268, channel_id at 300, dialog_id at 316, the 76-byte ATTACH_ACK frame at 332.
 *         Out: the verifier's tuple tail (160) at 0, the worker's pid u32 at 160 and uid u32 at 164.
 * PROMPT: 512-byte buffer. In: timeout in milliseconds (u32). Out: the worker's 178-byte prompt.
 * ANSWER: the buffer is the secret S1 answer record (157..4252 bytes), sent to the worker and wiped.
 * CLOSE:  closes the secret socket and the worker pin. */
#define E1S6_DLG_ATTACH 20
#define E1S6_DLG_PROMPT 21
#define E1S6_DLG_ANSWER 22
#define E1S6_DLG_CLOSE 23
#define E1S6_DLG_BUFFER 512
#define E1S6_DLG_ANSWER_MIN 157
#define E1S6_DLG_ANSWER_MAX 4252

static inline unsigned int e1s6_u32( const unsigned char *p )
{
    return p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24;
}
static inline unsigned long long e1s6_u64( const unsigned char *p )
{
    return e1s6_u32( p ) | (unsigned long long)e1s6_u32( p + 4 ) << 32;
}
static inline void e1s6_put32( unsigned char *p, unsigned int n )
{
    unsigned int i;
    for (i = 0; i < 4; i++) p[i] = n >> (8 * i);
}
static inline void e1s6_put64( unsigned char *p, unsigned long long n )
{
    e1s6_put32( p, n ); e1s6_put32( p + 4, n >> 32 );
}
#endif
