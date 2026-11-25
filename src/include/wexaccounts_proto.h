/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The link between session 0's wineserver and the accounts helper wex-accountsd (docs/LOCAL_USERS_DESIGN.md,
 * "Draft 3: the helper wex-accountsd and its link"): one SEQPACKET connection, one request datagram, one reply
 * datagram. Shared by the helper, the server's side of the link and the tests.
 *
 * A request: struct wexa_request, then body_len bytes. A reply: struct wexa_reply, then body_len bytes. A body is a
 * sequence of fields: a string is a u16 length (at most WEXA_MAX_STR) and that many bytes of UTF-8 without a NUL; a
 * number is a u32. All integers are little-endian as laid out here (the machine is one host).
 *
 * Operations and their body (request -> reply):
 *   WEXA_LIST_USERS     ([u32 start])                    -> u32 total, u32 first, u32 count, count x record
 *   WEXA_GET_USER       (name)                           -> record
 *   WEXA_ADMIN_MEMBERS  ([u32 start])                    -> u32 total, u32 first, u32 count, count x name
 * The two lists come in pages: a page holds what fits in one frame (it stops before a record that does not), `first` is
 * the start index asked for (0 when there is none), `total` the number of entries; ask again with start = first + count
 * until that reaches total. A page may hold fewer than the frame could, never a partial record.
 *   WEXA_CREATE_USER    (name, full name, u32 type, hash)-> record
 *   WEXA_DELETE_USER    (name)                           -> ()
 *   WEXA_RENAME_USER    (name, new name)                 -> ()
 *   WEXA_SET_FULLNAME   (name, full name)                -> ()
 *   WEXA_SET_LOCKED     (name, u32 locked)               -> ()
 *   WEXA_SET_TYPE       (name, u32 type)                 -> ()
 *   WEXA_SET_PASSWORD   (name, hash)                     -> ()
 *   WEXA_START_SYNC     ()                               -> ()
 * A record: name, full name, u32 uid, u32 type (0 standard, 1 administrator), u32 locked (0 or 1).
 * The password is only ever a crypt(3) SHA-512 hash made by the client (the helper checks its form).
 */
#ifndef WEX_ACCOUNTSD_PROTO_H
#define WEX_ACCOUNTSD_PROTO_H

#include <stdint.h>

#define WEXA_MAGIC       0x41584557u    /* "WEXA" */
#define WEXA_VERSION     1
#define WEXA_MAX_FRAME   65536
#define WEXA_MAX_STR     255
#define WEXA_SOCKET      "/run/wineexplorer/accounts.sock"
#define WEXA_CONFIG      "/etc/wineexplorer/accountsd.conf"

enum wexa_op
{
    WEXA_LIST_USERS = 1, WEXA_GET_USER, WEXA_ADMIN_MEMBERS, WEXA_CREATE_USER, WEXA_DELETE_USER, WEXA_RENAME_USER,
    WEXA_SET_FULLNAME, WEXA_SET_LOCKED, WEXA_SET_TYPE, WEXA_SET_PASSWORD, WEXA_START_SYNC, WEXA_OP_LAST
};

/* what the server says about the caller: it allows a class of operation, never more */
enum wexa_verdict
{
    WEXA_VERDICT_NONE = 0,       /* no caller: everything refused */
    WEXA_VERDICT_USER = 1,       /* an authenticated caller: the reads */
    WEXA_VERDICT_ELEVATED = 2    /* a real user's elevated administrator token: the changes and the sync */
};

enum wexa_status
{
    WEXA_OK = 0, WEXA_DENIED, WEXA_NOT_FOUND, WEXA_EXISTS, WEXA_BAD_ARGUMENT, WEXA_LOGGED_IN, WEXA_BUSY,
    WEXA_UNAVAILABLE, WEXA_LAST_ADMIN, WEXA_BAD_REQUEST,
    /* made by the Windows-side service, never by the helper: the change was made, and the account map had not shown it
     * after the wait (about 10 s). The client treats it as success with a warning. */
    WEXA_SYNC_PENDING
};

struct wexa_request
{
    uint32_t magic;          /* WEXA_MAGIC */
    uint16_t version;        /* WEXA_VERSION */
    uint16_t op;             /* enum wexa_op */
    uint32_t request_id;     /* echoed in the reply */
    uint32_t flags;          /* 0 */
    uint32_t verdict;        /* enum wexa_verdict */
    uint32_t caller_uid;     /* the caller's unix uid, from the link: the account that must not remove itself */
    uint32_t body_len;
};

struct wexa_reply
{
    uint32_t magic;
    uint16_t version;
    uint16_t status;         /* enum wexa_status */
    uint32_t request_id;
    uint32_t body_len;
};

#endif
