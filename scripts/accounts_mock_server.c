/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * A stand-in for the accounts service on a pipe of its own, for the client's tests (netapi32, docs/LOCAL_USERS_DESIGN.md): it
 * answers WEXA_LIST_USERS in two pages, and the second page has a larger total than the first (an account was made between
 * the two calls). The client has to refuse the list (ERROR_INVALID_DATA), not write past the buffer it sized for the first page.
 * The suite's section s0_accounts_chain starts it and points netapi32 at it through the test hook
 * HKLM\Software\WineExplorer\AccountsTest\Pipe.
 *
 * Usage: accounts_mock_server.exe PIPE-NAME   serves two requests and ends; prints "served=N"
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "wexaccounts_proto.h"

static int put_str(BYTE *p, const char *s)
{
    WORD n = (WORD)strlen(s);

    memcpy(p, &n, 2);
    memcpy(p + 2, s, n);
    return 2 + n;
}

static int put_user(BYTE *p, const char *name, DWORD uid)
{
    DWORD zero = 0;
    int n = put_str(p, name);

    n += put_str(p + n, "");
    memcpy(p + n, &uid, 4); n += 4;
    memcpy(p + n, &zero, 4); n += 4;   /* standard */
    memcpy(p + n, &zero, 4); n += 4;   /* not locked */
    return n;
}

int main(int argc, char **argv)
{
    HANDLE pipe;
    int served = 0;

    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, FALSE };

    if (argc < 2) return 2;
    /* open to everyone: the client is a program of a session, the server a process of session 0 */
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    pipe = CreateNamedPipeA(argv[1], PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
                            WEXA_MAX_FRAME, WEXA_MAX_FRAME, 0, &sa);
    if (pipe == INVALID_HANDLE_VALUE) { printf("create_error=%lu\n", GetLastError()); return 1; }
    while (served < 2)
    {
        BYTE in[WEXA_MAX_FRAME], out[WEXA_MAX_FRAME];
        struct wexa_request req;
        struct wexa_reply rep = { WEXA_MAGIC, WEXA_VERSION, WEXA_OK, 0, 0 };
        DWORD got, wrote, start = 0, total, first, count, n = 12, i;

        if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) break;
        if (ReadFile(pipe, in, sizeof(in), &got, NULL) && got >= sizeof(req))
        {
            memcpy(&req, in, sizeof(req));
            if (req.body_len >= 4) memcpy(&start, in + sizeof(req), 4);
            rep.request_id = req.request_id;
            if (start == 0) { total = 3; first = 0; count = 2; }
            else            { total = 5; first = 2; count = 3; }   /* two accounts were made in between */
            memcpy(out, &total, 4);
            memcpy(out + 4, &first, 4);
            memcpy(out + 8, &count, 4);
            for (i = 0; i < count; i++)
            {
                char name[16];

                sprintf(name, "mock%lu", first + i);
                n += put_user(out + n, name, 2000 + first + i);
            }
            rep.body_len = n;
            {
                BYTE frame[sizeof(rep) + WEXA_MAX_FRAME];

                memcpy(frame, &rep, sizeof(rep));
                memcpy(frame + sizeof(rep), out, n);
                WriteFile(pipe, frame, sizeof(rep) + n, &wrote, NULL);
            }
            served++;
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
    }
    printf("served=%d\n", served);
    return 0;
}
