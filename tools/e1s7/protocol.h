/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#ifndef WEX_E1S7_PROTOCOL_H
#define WEX_E1S7_PROTOCOL_H

/* Private worker framing, NOT S1 evidence or a new S1 record kind. All fields
 * are explicitly encoded little endian; never send a native C structure. */
#define S7_HEADER 72u
#define S7_TEXT 4096u
#define S7_PACKET (S7_HEADER + S7_TEXT)
#define S7_ROUNDS 8u
#define S7_MESSAGES 8u
#define S7_ATTEMPTS 3u
#define S7_SECONDS 120u
#define S7_START 1u
#define S7_PROMPT 2u
#define S7_ANSWER 3u
#define S7_CANCEL 4u
#define S7_RESULT 5u
/* Root-service-only decoy: same dialog/deadline, can only return failure. */
#define S7_DECOY 6u
/* Every refusal is released this long after the answer arrived: PAM's default failure delay (2 s),
 * without its random part, so a wrong password, an unknown account and a decoy look the same. It is
 * also the time PAM gets for the check. A success is released at once. */
#define S7_FAILURE_NS UINT64_C(2000000000)
#define S7_RESULT_SIZE (S7_HEADER + 68u)

#endif
