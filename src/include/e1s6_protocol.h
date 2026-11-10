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
