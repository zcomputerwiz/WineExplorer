/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The client of the lab build of wexaccounts (scripts/test_accounts_service.sh; docs/LOCAL_USERS_DESIGN.md): speaks the frame of
 * wexaccounts_proto.h over the accounts pipe and checks what the service itself keeps: the syntax and limits of the pipe,
 * the order of changes, the wait for the account map, the number of connections. The helper behind the service is the lab
 * build's stand-in (names: "slow" takes 3 s, "down" is unreachable, "last" is the last administrator, "stats" asks it what
 * it saw), and the map is the file given to the service.
 *
 * Usage: accounts_service_probe.exe PIPE MAPFILE   Prints "ok NAME" and "FAIL NAME ..." lines and "accounts_service=PASS|FAIL".
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wexaccounts_proto.h"

static int failures;
static const WCHAR *pipe_name, *map_file;

static void check(int ok, const char *name, const char *detail)
{
    printf("%s %s%s%s\n", ok ? "ok" : "FAIL", name, detail && !ok ? " " : "", detail && !ok ? detail : "");
    if (!ok) failures++;
}

static HANDLE connect_pipe(void)
{
    HANDLE p;
    DWORD mode = PIPE_READMODE_MESSAGE, i;

    for (i = 0; i < 50; i++)
    {
        p = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (p != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(pipe_name, 1000);
        else Sleep(200);
    }
    if (p != INVALID_HANDLE_VALUE) SetNamedPipeHandleState(p, &mode, NULL, NULL);
    return p;
}

static int put_str(BYTE *p, const char *s)
{
    WORD n = (WORD)strlen(s);
    memcpy(p, &n, 2);
    memcpy(p + 2, s, n);
    return 2 + n;
}

/* one request; returns 1 when a reply came (status and body filled), 0 when the service closed the connection */
static int xact(HANDLE p, unsigned op, const BYTE *body, DWORD len, unsigned *status, BYTE *reply, DWORD *reply_len)
{
    static BYTE frame[WEXA_MAX_FRAME + 64];
    struct wexa_request req = { WEXA_MAGIC, WEXA_VERSION, (WORD)op, 7, 0, 2, 0, len };
    BYTE in[WEXA_MAX_FRAME + 1];
    DWORD got = 0, wrote = 0;

    memcpy(frame, &req, sizeof(req));
    if (len) memcpy(frame + sizeof(req), body, len);
    if (!WriteFile(p, frame, sizeof(req) + len, &wrote, NULL)) return 0;
    if (!ReadFile(p, in, sizeof(in), &got, NULL) || got < sizeof(struct wexa_reply)) return 0;
    {
        struct wexa_reply rep;
        memcpy(&rep, in, sizeof(rep));
        *status = rep.status;
        if (reply) memcpy(reply, in + sizeof(rep), got - sizeof(rep));
        if (reply_len) *reply_len = got - sizeof(rep);
    }
    return 1;
}

static int raw(HANDLE p, const void *data, DWORD len)   /* a datagram that is not a request: 1 if the service answers, 0 if it closes */
{
    BYTE in[256];
    DWORD got = 0, wrote = 0;

    if (!WriteFile(p, data, len, &wrote, NULL)) return 0;
    return ReadFile(p, in, sizeof(in), &got, NULL) && got > 0;
}

static unsigned one(unsigned op, const char *name)   /* a request with a one-string body; the status, or 999 when closed */
{
    HANDLE p = connect_pipe();
    BYTE body[300];
    unsigned st = 999;
    int n = put_str(body, name);

    if (p == INVALID_HANDLE_VALUE) return 998;
    if (!xact(p, op, body, n, &st, NULL, NULL)) st = 999;
    CloseHandle(p);
    return st;
}

static void write_map(unsigned gen)
{
    FILE *f = _wfopen(map_file, L"wb");
    if (!f) return;
    fprintf(f, "WEXACCOUNTS 1 %u 1001 1 2 3\n", gen);
    fclose(f);
}

static DWORD WINAPI bump_map(void *arg)
{
    Sleep((DWORD)(ULONG_PTR)arg);
    write_map(6);
    return 0;
}

static DWORD WINAPI slow_create(void *arg)
{
    BYTE body[600];
    int n = put_str(body, "slow");
    unsigned st = 999;
    HANDLE p = connect_pipe();

    n += put_str(body + n, "S"); *(DWORD *)(body + n) = 0; n += 4; n += put_str(body + n, "$6$salt$");
    if (p != INVALID_HANDLE_VALUE) { if (!xact(p, WEXA_CREATE_USER, body, n, &st, NULL, NULL)) st = 999; CloseHandle(p); }
    return st;
}

int wmain(int argc, WCHAR **argv)
{
    HANDLE p, many[140];
    BYTE body[1024], reply[WEXA_MAX_FRAME];
    DWORD reply_len, t0, i, n;
    int served;
    unsigned st;
    char what[160];

    if (argc < 3) return 2;
    pipe_name = argv[1];
    map_file = argv[2];
    write_map(5);

    /* reads and the record that comes back */
    p = connect_pipe();
    check(p != INVALID_HANDLE_VALUE, "the pipe can be opened", NULL);
    n = put_str(body, "alice");
    st = 999;
    check(xact(p, WEXA_GET_USER, body, n, &st, reply, &reply_len) && st == WEXA_OK && reply_len == 2 + 5 + 2 + 8 + 12, "a read is relayed and its record comes back", NULL);
    check(xact(p, WEXA_LIST_USERS, NULL, 0, &st, reply, &reply_len) && st == WEXA_OK, "a list with no start index is relayed", NULL);
    n = 4; *(DWORD *)body = 100;
    check(xact(p, WEXA_LIST_USERS, body, n, &st, reply, &reply_len) && st == WEXA_OK, "a list with a start index is relayed", NULL);
    check(xact(p, WEXA_LIST_USERS, body, 3, &st, reply, &reply_len) && st == WEXA_BAD_REQUEST, "a start index that is not a u32 is BAD_REQUEST", NULL);
    check(xact(p, WEXA_GET_USER, NULL, 0, &st, reply, &reply_len) && st == WEXA_BAD_REQUEST, "a missing string is BAD_REQUEST", NULL);
    n = put_str(body, "ab"); body[n++] = 'x';
    check(xact(p, WEXA_GET_USER, body, n, &st, reply, &reply_len) && st == WEXA_BAD_REQUEST, "trailing bytes in a body are BAD_REQUEST", NULL);
    *(WORD *)body = 300;
    memset(body + 2, 'a', 300);
    check(xact(p, WEXA_GET_USER, body, 302, &st, reply, &reply_len) && st == WEXA_BAD_REQUEST, "a string over 255 bytes is BAD_REQUEST", NULL);
    *(WORD *)body = 3; memcpy(body + 2, "a\0b", 3);
    check(xact(p, WEXA_GET_USER, body, 5, &st, reply, &reply_len) && st == WEXA_BAD_REQUEST, "a NUL inside a string is BAD_REQUEST", NULL);
    {
        struct wexa_request r = { WEXA_MAGIC, WEXA_VERSION, WEXA_GET_USER, 9, 1, 2, 0, 0 };
        check(raw(p, &r, sizeof(r)), "a request with a flag is answered (BAD_REQUEST), the connection stays", NULL);
    }
    n = put_str(body, "alice");
    check(xact(p, WEXA_GET_USER, body, n, &st, reply, &reply_len) && st == WEXA_OK, "the connection still works after a refused request", NULL);

    /* the stand-in's answers pass through: down is UNAVAILABLE, a protective refusal keeps its status */
    check(one(WEXA_GET_USER, "down") == WEXA_UNAVAILABLE, "the helper not reachable gives UNAVAILABLE", NULL);
    check(one(WEXA_DELETE_USER, "last") == WEXA_LAST_ADMIN, "the helper's own refusal passes through", NULL);

    /* frames that end the connection */
    {
        struct wexa_request bad = { 0x12345678, WEXA_VERSION, WEXA_GET_USER, 1, 0, 2, 0, 0 };
        HANDLE q = connect_pipe();
        check(!raw(q, &bad, sizeof(bad)), "a bad magic closes the connection", NULL);
        CloseHandle(q);
        bad.magic = WEXA_MAGIC; bad.version = 9;
        q = connect_pipe();
        check(!raw(q, &bad, sizeof(bad)), "an unknown version closes the connection", NULL);
        CloseHandle(q);
        bad.version = WEXA_VERSION; bad.op = 99;
        q = connect_pipe();
        check(!raw(q, &bad, sizeof(bad)), "an unknown operation closes the connection", NULL);
        CloseHandle(q);
        bad.op = WEXA_GET_USER; bad.body_len = 50;
        q = connect_pipe();
        check(!raw(q, &bad, sizeof(bad)), "a body length that does not match closes the connection", NULL);
        CloseHandle(q);
        q = connect_pipe();
        check(!raw(q, "WEXA", 4), "a short frame closes the connection", NULL);
        CloseHandle(q);
        q = connect_pipe();
        {
            static BYTE big[WEXA_MAX_FRAME + 100];
            check(!raw(q, big, sizeof(big)), "a frame over the limit closes the connection", NULL);
        }
        CloseHandle(q);
    }
    CloseHandle(p);

    /* the wait for the map: a created user is answered when the generation moves; a map that stays gives the warning */
    {
        HANDLE bumper;
        BYTE cb[600];
        int cn = put_str(cb, "newuser");
        cn += put_str(cb + cn, "New"); *(DWORD *)(cb + cn) = 0; cn += 4; cn += put_str(cb + cn, "$6$salt$");
        write_map(5);
        bumper = CreateThread(NULL, 0, bump_map, (void *)(ULONG_PTR)1000, 0, NULL);
        p = connect_pipe();
        t0 = GetTickCount();
        st = 999;
        xact(p, WEXA_CREATE_USER, cb, cn, &st, reply, &reply_len);
        i = GetTickCount() - t0;
        sprintf(what, "(status %u after %lu ms)", st, i);
        check(st == WEXA_OK && i >= 900 && i < 4000, "a create is answered when the map's generation moves", what);
        WaitForSingleObject(bumper, 5000);
        CloseHandle(bumper);
        write_map(7);   /* nothing moves it now */
        t0 = GetTickCount();
        st = 999;
        xact(p, WEXA_CREATE_USER, cb, cn, &st, reply, &reply_len);
        i = GetTickCount() - t0;
        sprintf(what, "(status %u after %lu ms)", st, i);
        check(st == WEXA_SYNC_PENDING && i >= 9000 && i < 14000, "a map that does not move gives SYNC_PENDING after about 10 s", what);
        DeleteFileW(map_file);
        t0 = GetTickCount();
        st = 999;
        xact(p, WEXA_CREATE_USER, cb, cn, &st, reply, &reply_len);
        i = GetTickCount() - t0;
        sprintf(what, "(status %u after %lu ms)", st, i);
        check(st == WEXA_OK && i < 2000, "no map to watch: no wait", what);
        CloseHandle(p);
        write_map(5);
    }

    /* one change at a time, service-wide */
    {
        HANDLE threads[2];
        DWORD codes[2];
        BYTE sb[64];
        int sn = put_str(sb, "stats");

        t0 = GetTickCount();
        for (i = 0; i < 2; i++) threads[i] = CreateThread(NULL, 0, slow_create, NULL, 0, NULL);
        WaitForMultipleObjects(2, threads, TRUE, 30000);
        for (i = 0; i < 2; i++) { GetExitCodeThread(threads[i], &codes[i]); CloseHandle(threads[i]); }
        i = GetTickCount() - t0;
        sprintf(what, "(two slow creates took %lu ms, statuses %lu %lu)", i, codes[0], codes[1]);
        check(i >= 5800 && codes[0] <= WEXA_SYNC_PENDING && codes[1] <= WEXA_SYNC_PENDING, "two changes at once are served one after the other", what);
        p = connect_pipe();
        st = 999;
        xact(p, WEXA_GET_USER, sb, sn, &st, reply, &reply_len);
        {
            DWORD in_flight = 0;
            if (reply_len >= 2 + 5 + 2 + 8 + 12) memcpy(&in_flight, reply + 2 + 5 + 2 + 8, 4);   /* the record's uid: the most changes seen at once */
            sprintf(what, "(%lu at once)", in_flight);
            check(in_flight == 1, "the helper never saw two changes at once", what);
        }
        CloseHandle(p);
    }

    /* the number of connections: 128, the next is dropped */
    n = 0;
    served = 0;
    for (i = 0; i < 140; i++)
    {
        DWORD tries, mode = PIPE_READMODE_MESSAGE;
        BYTE gb[16];
        int gn = put_str(gb, "alice");

        /* the service makes the next pipe instance after it has taken the last connection: a busy pipe is waited for */
        for (tries = 0; tries < 20; tries++)
        {
            many[i] = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (many[i] != INVALID_HANDLE_VALUE) break;
            if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(pipe_name, 500);
            else Sleep(50);
        }
        if (many[i] != INVALID_HANDLE_VALUE)
        {
            /* the request goes at once: an idle connection is closed after 30 s, and the connecting takes time */
            SetNamedPipeHandleState(many[i], &mode, NULL, NULL);
            n++;
            if (xact(many[i], WEXA_GET_USER, gb, gn, &st, reply, &reply_len) && st == WEXA_OK) served++;
        }
    }
    {
        sprintf(what, "(%d connected, %d served)", (int)n, served);
        check(served >= 120 && served <= 128, "no more than 128 connections are served at once", what);
    }
    for (i = 0; i < 140; i++) if (many[i] != INVALID_HANDLE_VALUE) CloseHandle(many[i]);
    Sleep(1500);
    check(one(WEXA_GET_USER, "alice") == WEXA_OK, "the service serves again after the connections are gone", NULL);

    printf("accounts_service=%s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
