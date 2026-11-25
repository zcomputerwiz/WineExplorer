/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * A client of the accounts service through the whole chain (docs/LOCAL_USERS_DESIGN.md): this program in a session -> the
 * bridge -> wexaccounts in session 0 -> the server's accounts_forward -> the helper wex-accountsd (the suite's section
 * s0_accounts_chain runs a lab build of it on a fake AccountsService). Speaks the frame of wexaccounts_proto.h, as netapi32 will.
 *
 * Usage: accounts_chain_probe.exe MODE [ARG...] [--out FILE]
 *   read NAME         WEXA_GET_USER            prints "status=N" and, for status 0, "user=NAME uid=U type=T locked=L full=F"
 *   list              WEXA_LIST_USERS          prints "status=N total=T count=C names=a,b,c"
 *   admins            WEXA_ADMIN_MEMBERS       prints "status=N total=T count=C names=a,b"
 *   create NAME       WEXA_CREATE_USER         a standard user "Chain Test" with a made-up SHA-512 hash; "status=N"
 *   delete NAME       WEXA_DELETE_USER         "status=N"
 *   fullname NAME TXT WEXA_SET_FULLNAME        "status=N" (the suite's slow call: a helper that goes away during it)
 * The output goes to FILE when --out is given (an elevated process has no console). The pipe is \\.\pipe\WexSession0\accounts, or the
 * name in the environment variable WEX_ACCOUNTS_PIPE (a session 0 process uses it to try the local, not bridged, name).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wexaccounts_proto.h"

static int put_str(BYTE *p, const char *s)
{
    WORD n = (WORD)strlen(s);
    memcpy(p, &n, 2);
    memcpy(p + 2, s, n);
    return 2 + n;
}

static const char *get_str(const BYTE *p, DWORD *off, DWORD len, char *out, size_t size)
{
    WORD n;

    out[0] = 0;
    if (*off + 2 > len) return out;
    memcpy(&n, p + *off, 2);
    *off += 2;
    if (*off + n > len || n >= size) return out;
    memcpy(out, p + *off, n);
    out[n] = 0;
    *off += n;
    return out;
}

int main(int argc, char **argv)
{
    static BYTE reply[WEXA_MAX_FRAME];
    BYTE body[1024];
    const char *mode = argc > 1 ? argv[1] : "", *pipe_name = getenv("WEX_ACCOUNTS_PIPE");
    char name[512], full[512];
    struct wexa_request req = { WEXA_MAGIC, WEXA_VERSION, 0, 1, 0, 2, 0, 0 };
    struct wexa_reply rep;
    HANDLE pipe;
    DWORD mode_msg = PIPE_READMODE_MESSAGE, wrote, got = 0, off, n = 0, i;
    int a;

    for (a = 1; a + 1 < argc; a++)
        if (!strcmp(argv[a], "--out")) { freopen(argv[a + 1], "w", stdout); argc = a; break; }
    if (!pipe_name) pipe_name = "\\\\.\\pipe\\WexSession0\\accounts";
    if (!strcmp(mode, "read") && argc > 2) { req.op = WEXA_GET_USER; n = put_str(body, argv[2]); }
    else if (!strcmp(mode, "list")) req.op = WEXA_LIST_USERS;
    else if (!strcmp(mode, "admins")) req.op = WEXA_ADMIN_MEMBERS;
    else if (!strcmp(mode, "create") && argc > 2)
    {
        char hash[128] = "$6$chainsalt$";   /* a SHA-512 crypt form: the salt, then 86 characters of the alphabet */
        DWORD zero = 0;

        memset(hash + strlen(hash), 'A', 86);
        hash[strlen("$6$chainsalt$") + 86] = 0;

        req.op = WEXA_CREATE_USER;
        n = put_str(body, argv[2]);
        n += put_str(body + n, "Chain Test");
        memcpy(body + n, &zero, 4);
        n += 4;
        n += put_str(body + n, hash);
    }
    else if (!strcmp(mode, "delete") && argc > 2) { req.op = WEXA_DELETE_USER; n = put_str(body, argv[2]); }
    else if (!strcmp(mode, "fullname") && argc > 3)
    {
        req.op = WEXA_SET_FULLNAME;
        n = put_str(body, argv[2]);
        n += put_str(body + n, argv[3]);
    }
    else { printf("usage\n"); return 2; }
    req.body_len = n;

    pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    for (i = 0; pipe == INVALID_HANDLE_VALUE && i < 20; i++)
    {
        if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeA(pipe_name, 1000); else Sleep(300);
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (pipe == INVALID_HANDLE_VALUE) { printf("open_error=%lu\n", GetLastError()); return 1; }
    SetNamedPipeHandleState(pipe, &mode_msg, NULL, NULL);
    {
        static BYTE frame[sizeof(req) + 1024];
        memcpy(frame, &req, sizeof(req));
        memcpy(frame + sizeof(req), body, n);
        if (!WriteFile(pipe, frame, sizeof(req) + n, &wrote, NULL)) { printf("write_error=%lu\n", GetLastError()); return 1; }
    }
    if (!ReadFile(pipe, reply, sizeof(reply), &got, NULL) || got < sizeof(rep))
    {
        printf("closed\n");   /* the service ended the connection */
        return 1;
    }
    memcpy(&rep, reply, sizeof(rep));
    printf("status=%u\n", rep.status);
    if (rep.status == WEXA_OK || rep.status == WEXA_SYNC_PENDING)
    {
        DWORD len = got - sizeof(rep);
        const BYTE *b = reply + sizeof(rep);

        off = 0;
        if (req.op == WEXA_GET_USER || req.op == WEXA_CREATE_USER)
        {
            DWORD uid = 0, type = 0, locked = 0;

            get_str(b, &off, len, name, sizeof(name));
            get_str(b, &off, len, full, sizeof(full));
            if (off + 12 <= len) { memcpy(&uid, b + off, 4); memcpy(&type, b + off + 4, 4); memcpy(&locked, b + off + 8, 4); }
            printf("user=%s uid=%lu type=%lu locked=%lu full=%s\n", name, uid, type, locked, full);
        }
        else if (req.op == WEXA_LIST_USERS || req.op == WEXA_ADMIN_MEMBERS)
        {
            DWORD total = 0, first = 0, count = 0;

            if (len >= 12) { memcpy(&total, b, 4); memcpy(&first, b + 4, 4); memcpy(&count, b + 8, 4); off = 12; }
            printf("total=%lu count=%lu names=", total, count);
            for (i = 0; i < count; i++)
            {
                get_str(b, &off, len, name, sizeof(name));
                if (req.op == WEXA_LIST_USERS) { get_str(b, &off, len, full, sizeof(full)); off += 12; }
                printf("%s%s", i ? "," : "", name);
            }
            printf("\n");
        }
    }
    fflush(stdout);
    CloseHandle(pipe);
    return 0;
}
