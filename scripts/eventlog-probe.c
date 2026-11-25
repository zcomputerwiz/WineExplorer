/*
 * Probe of the EventLog service's pipe protocol (WineExplorer L1, docs/EVENTLOG_DESIGN.md)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* Speaks the protocol of include/wine/wex_eventlog.h directly (L2's advapi32 will be the real client), so that the
 * tests can also send what a client never would. Built with MinGW, 64 and 32 bit. Each mode prints key=value lines;
 * "status=N" is a Win32 error code (0 success).
 *
 *   squat                          create \\.\pipe\WexSession0\eventlog (a session must be refused)
 *   report SOURCE TYPE ID [STR...] RegisterEventSource + ReportEvent
 *   read LOG [back] [seek N]        all records (or from N), one "rec=" line each
 *   count LOG | oldest LOG | info LOG
 *   clear LOG [FILE]               with FILE: back up first (made here, E3), then clear
 *   backup LOG FILE | openbackup FILE
 *   frame KIND                     badmagic, oversize, badver, badop, longstring, manystrings, badsid, bigread, seekbad
 *   caps connections|handles|subs  fill one per-account cap; prints how many were granted and the refusal
 *   notify LOG SECONDS             wait for one record written to LOG
 *   hold LOG SECONDS               a handle and a subscription that reconnect after a restart of the service
 *   sqos                           the client's open asks for identification level only (prints the flags) */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wine/wex_eventlog.h"   /* -idirafter wine/include */

#define CLIENT_FLAGS (SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION)

NTSYSAPI DWORD WINAPI RtlComputeCrc32( DWORD initial, const BYTE *data, INT len );  /* ntdll; not in MinGW's winternl.h */

static HANDLE connect_log(void)
{
    HANDLE pipe;
    DWORD mode = PIPE_READMODE_MESSAGE, i;

    for (i = 0; i < 50; i++)
    {
        pipe = CreateFileW( WEX_EVTLOG_PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, CLIENT_FLAGS, NULL );
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return pipe;
        WaitNamedPipeW( WEX_EVTLOG_PIPE, 200 );
    }
    if (pipe != INVALID_HANDLE_VALUE) SetNamedPipeHandleState( pipe, &mode, NULL, NULL );
    return pipe;
}

/* one request and its reply; the reply's body (malloc'd) and size. Returns the status, or (DWORD)-1 when the
 * connection failed */
static DWORD call( HANDLE pipe, unsigned int op, unsigned int handle, unsigned int arg0, unsigned int arg1,
                   const void *body, DWORD body_size, struct wex_evtlog_header *out, BYTE **reply, DWORD *reply_size )
{
    struct wex_evtlog_header hdr = { WEX_EVTLOG_MAGIC, WEX_EVTLOG_VERSION, op, sizeof(hdr) + body_size, 0, handle,
                                     { arg0, arg1 } };
    BYTE *msg = malloc( hdr.size ), *buf = NULL;
    DWORD written, got = 0, cap = 65536, left;

    memcpy( msg, &hdr, sizeof(hdr) );
    if (body_size) memcpy( msg + sizeof(hdr), body, body_size );
    if (!WriteFile( pipe, msg, hdr.size, &written, NULL ))
    {
        free( msg );
        return (DWORD)-1;
    }
    free( msg );
    buf = malloc( cap );
    if (!ReadFile( pipe, buf, cap, &got, NULL ))
    {
        if (GetLastError() != ERROR_MORE_DATA || !PeekNamedPipe( pipe, NULL, 0, NULL, NULL, &left ))
        {
            free( buf );
            return (DWORD)-1;
        }
        buf = realloc( buf, got + left );
        if (!ReadFile( pipe, buf + got, left, &written, NULL ))
        {
            free( buf );
            return (DWORD)-1;
        }
        got += left;
    }
    if (got < sizeof(*out))
    {
        free( buf );
        return (DWORD)-1;
    }
    memcpy( out, buf, sizeof(*out) );
    if (reply)
    {
        *reply_size = got - sizeof(*out);
        *reply = malloc( *reply_size + 1 );
        memcpy( *reply, buf + sizeof(*out), *reply_size );
    }
    free( buf );
    return out->status;
}

static DWORD open_handle( HANDLE pipe, unsigned int flags, const WCHAR *name, unsigned int *handle )
{
    DWORD name_size = (wcslen( name ) + 1) * sizeof(WCHAR), status;
    struct wex_evtlog_header out;
    BYTE *body = malloc( sizeof(struct wex_evtlog_open) + name_size );
    struct wex_evtlog_open open = { flags, name_size };

    memcpy( body, &open, sizeof(open) );
    memcpy( body + sizeof(open), name, name_size );
    status = call( pipe, WEX_EVTLOG_OPEN, 0, 0, 0, body, sizeof(open) + name_size, &out, NULL, NULL );
    free( body );
    *handle = out.handle;
    return status;
}

static WCHAR *wide( const char *s )
{
    int len = MultiByteToWideChar( CP_UTF8, 0, s, -1, NULL, 0 );
    WCHAR *w = malloc( len * sizeof(WCHAR) );
    MultiByteToWideChar( CP_UTF8, 0, s, -1, w, len );
    return w;
}

/* a report body: type, id, the strings, optional SID and data */
static BYTE *report_body( WORD type, DWORD id, int count, WCHAR **strings, const BYTE *sid, DWORD sid_size,
                          DWORD data_size, DWORD *size )
{
    struct wex_evtlog_report rep = { type, 0, id, count, 0, data_size, sid_size };
    DWORD pos;
    BYTE *body;
    int i;

    for (i = 0; i < count; i++) rep.strings_size += (wcslen( strings[i] ) + 1) * sizeof(WCHAR);
    *size = sizeof(rep) + sid_size + rep.strings_size + data_size;
    body = calloc( 1, *size );
    memcpy( body, &rep, sizeof(rep) );
    pos = sizeof(rep);
    if (sid_size) memcpy( body + pos, sid, sid_size );
    pos += sid_size;
    for (i = 0; i < count; i++)
    {
        DWORD len = (wcslen( strings[i] ) + 1) * sizeof(WCHAR);
        memcpy( body + pos, strings[i], len );
        pos += len;
    }
    return body;
}

static void print_records( const BYTE *buf, DWORD size )
{
    DWORD pos = 0;

    while (pos + sizeof(EVENTLOGRECORD) <= size)
    {
        const EVENTLOGRECORD *rec = (const EVENTLOGRECORD *)(buf + pos);
        const WCHAR *source = (const WCHAR *)(rec + 1);
        const WCHAR *str = (const WCHAR *)(buf + pos + rec->StringOffset);

        if (rec->Length < sizeof(*rec) || rec->Length > size - pos) break;
        printf( "rec=%lu id=%lu type=%u source=%ls strings=%u first=%ls\n", (unsigned long)rec->RecordNumber,
                (unsigned long)rec->EventID, rec->EventType, source, rec->NumStrings, rec->NumStrings ? str : L"" );
        pos += rec->Length;
    }
}

/* the backup file, our storage format (E3: written by the client, with its own rights): header, records, trailers */
struct file_header { char magic[8]; DWORD version, next; ULONGLONG created, written, read, reserved; };

/* stream the records from *next on into file (appended); *next and *last follow what was written */
static DWORD stream_into( HANDLE pipe, unsigned int handle, HANDLE file, DWORD *next, DWORD *records, DWORD *last )
{
    struct wex_evtlog_header out;
    DWORD status, written;

    for (;;)
    {
        BYTE *buf;
        DWORD size, pos = 0;

        status = call( pipe, WEX_EVTLOG_STREAM, handle, *next, 0, NULL, 0, &out, &buf, &size );
        if (status) { free( buf ); break; }
        while (pos + sizeof(EVENTLOGRECORD) <= size)
        {
            const EVENTLOGRECORD *rec = (const EVENTLOGRECORD *)(buf + pos);
            DWORD trailer[2] = { RtlComputeCrc32( 0, buf + pos, rec->Length ), rec->RecordNumber };

            WriteFile( file, buf + pos, rec->Length, &written, NULL );
            WriteFile( file, trailer, sizeof(trailer), &written, NULL );
            *last = rec->RecordNumber;
            pos += rec->Length;
            (*records)++;
        }
        free( buf );
        *next = out.arg[0];
    }
    return status == ERROR_HANDLE_EOF ? ERROR_SUCCESS : status;
}

/* a backup and, with clear, the clear after it: CLEAR names the last record the backup has, and newer records
 * (ERROR_RETRY) make the client stream the rest and clear again, a bounded number of times (N6) */
static DWORD backup_and_clear( HANDLE pipe, unsigned int handle, const char *path, BOOL clear, DWORD *records,
                               DWORD *last, DWORD *retries )
{
    struct file_header fh = { "WXEVLOG1", 1, 0, 0, 0, 0, 0 };
    struct wex_evtlog_header out;
    DWORD next = 0, status, written;
    HANDLE file = CreateFileA( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );

    *records = *last = *retries = 0;
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    WriteFile( file, &fh, sizeof(fh), &written, NULL );
    status = stream_into( pipe, handle, file, &next, records, last );
    while (!status && clear)
    {
        /* after 8 retries against a steady writer: clear through the backup's last record, keep the newer ones */
        status = call( pipe, WEX_EVTLOG_CLEAR, handle, *retries >= 8 ? 2 : 1, *last, NULL, 0, &out, NULL, NULL );
        if (status != ERROR_RETRY) break;
        ++*retries;
        status = stream_into( pipe, handle, file, &next, records, last );
    }
    CloseHandle( file );
    return status;
}

static DWORD backup( HANDLE pipe, unsigned int handle, const char *path, DWORD *records )
{
    DWORD last, retries;
    return backup_and_clear( pipe, handle, path, FALSE, records, &last, &retries );
}

static int frame_test( HANDLE pipe, const char *kind )
{
    struct wex_evtlog_header out, hdr;
    unsigned int handle = 0;
    DWORD status, written, size;
    BYTE *body;

    if (!strcmp( kind, "badmagic" ) || !strcmp( kind, "oversize" ))
    {
        /* a malformed frame closes the connection */
        DWORD len = !strcmp( kind, "oversize" ) ? WEX_EVTLOG_MAX_MSG + 4096 : sizeof(hdr);
        BYTE *msg = calloc( 1, len );

        hdr.magic = !strcmp( kind, "oversize" ) ? WEX_EVTLOG_MAGIC : 0x12345678;
        hdr.version = WEX_EVTLOG_VERSION;
        hdr.op = WEX_EVTLOG_COUNT;
        hdr.size = len;
        hdr.status = hdr.handle = hdr.arg[0] = hdr.arg[1] = 0;
        memcpy( msg, &hdr, sizeof(hdr) );
        WriteFile( pipe, msg, len, &written, NULL );
        free( msg );
        status = call( pipe, WEX_EVTLOG_OPEN, 0, 0, 0, NULL, 0, &out, NULL, NULL );
        printf( "frame=%s closed=%d\n", kind, status == (DWORD)-1 );
        return 0;
    }
    if (!strcmp( kind, "badver" ))
    {
        BYTE msg[sizeof(hdr)];
        hdr.magic = WEX_EVTLOG_MAGIC; hdr.version = 99; hdr.op = WEX_EVTLOG_COUNT; hdr.size = sizeof(hdr);
        hdr.status = hdr.handle = hdr.arg[0] = hdr.arg[1] = 0;
        memcpy( msg, &hdr, sizeof(hdr) );
        WriteFile( pipe, msg, sizeof(msg), &written, NULL );
        ReadFile( pipe, &out, sizeof(out), &written, NULL );
        printf( "frame=badver status=%lu\n", (unsigned long)out.status );
        return 0;
    }
    if (!strcmp( kind, "badop" ))
    {
        open_handle( pipe, WEX_EVTLOG_OPEN_LOG, L"Application", &handle );
        status = call( pipe, 77, handle, 0, 0, NULL, 0, &out, NULL, NULL );
        printf( "frame=badop status=%lu\n", (unsigned long)status );
        return 0;
    }
    if (!strcmp( kind, "bigread" ) || !strcmp( kind, "seekbad" ))
    {
        DWORD bufsize = !strcmp( kind, "bigread" ) ? 0x40000000 : 4096;
        BYTE *reply = NULL;
        open_handle( pipe, WEX_EVTLOG_OPEN_LOG, L"Application", &handle );
        if (!strcmp( kind, "bigread" ))
            status = call( pipe, WEX_EVTLOG_READ, handle, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0,
                           &bufsize, sizeof(bufsize), &out, &reply, &size );
        else
            status = call( pipe, WEX_EVTLOG_READ, handle, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, 0x7fffffff,
                           &bufsize, sizeof(bufsize), &out, &reply, &size );
        printf( "frame=%s status=%lu size=%lu\n", kind, (unsigned long)status, reply ? (unsigned long)size : 0ul );
        free( reply );
        return 0;
    }
    /* reports with too long a string, too many strings, a malformed SID */
    open_handle( pipe, WEX_EVTLOG_OPEN_SOURCE, L"WexProbe", &handle );
    if (!strcmp( kind, "longstring" ))
    {
        WCHAR *s = malloc( (WEX_EVTLOG_MAX_STRING + 2) * sizeof(WCHAR) );
        int i;
        for (i = 0; i <= WEX_EVTLOG_MAX_STRING; i++) s[i] = 'x';
        s[WEX_EVTLOG_MAX_STRING + 1] = 0;
        body = report_body( EVENTLOG_INFORMATION_TYPE, 1, 1, &s, NULL, 0, 0, &size );
    }
    else if (!strcmp( kind, "manystrings" ))
    {
        WCHAR *strings[WEX_EVTLOG_MAX_STRINGS + 1];
        int i;
        for (i = 0; i <= WEX_EVTLOG_MAX_STRINGS; i++) strings[i] = (WCHAR *)L"s";
        body = report_body( EVENTLOG_INFORMATION_TYPE, 1, WEX_EVTLOG_MAX_STRINGS + 1, strings, NULL, 0, 0, &size );
    }
    else
    {
        BYTE sid[12] = { 9, 9, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0 };  /* revision 9: not a SID */
        body = report_body( EVENTLOG_INFORMATION_TYPE, 1, 0, NULL, sid, sizeof(sid), 0, &size );
    }
    status = call( pipe, WEX_EVTLOG_REPORT, handle, 0, 0, body, size, &out, NULL, NULL );
    printf( "frame=%s status=%lu\n", kind, (unsigned long)status );
    free( body );
    return 0;
}

static int caps_test( const char *kind )
{
    HANDLE pipes[300];
    struct wex_evtlog_header out;
    unsigned int handle;
    DWORD status = 0, granted = 0, i, n = 0;

    if (!strcmp( kind, "connections" ))
    {
        /* each connection is refused at once when the account holds the cap */
        DWORD connect_error = 0;
        for (n = 0; n < 300; n++)
        {
            /* a refusal at the open (the bridge's own cap) or the service closing the connection (its cap) */
            if ((pipes[n] = connect_log()) == INVALID_HANDLE_VALUE)
            {
                connect_error = GetLastError();
                break;
            }
            status = open_handle( pipes[n], WEX_EVTLOG_OPEN_LOG, L"Application", &handle );
            if (status) break;
            granted++;
        }
        printf( "caps=connections granted=%lu refused=%lu by=%s connect_error=%lu\n", (unsigned long)granted,
                (unsigned long)status, connect_error ? "open" : "service", (unsigned long)connect_error );
    }
    else if (!strcmp( kind, "handles" ))
    {
        /* 64 handles a connection: across connections up to the account's 1024 */
        for (n = 0; n < 20 && status != ERROR_NOT_ENOUGH_QUOTA; n++)
        {
            if ((pipes[n] = connect_log()) == INVALID_HANDLE_VALUE) break;
            for (i = 0; i < 64; i++)
            {
                if ((status = open_handle( pipes[n], WEX_EVTLOG_OPEN_LOG, L"Application", &handle ))) break;
                granted++;
            }
        }
        printf( "caps=handles granted=%lu refused=%lu\n", (unsigned long)granted, (unsigned long)status );
    }
    else
    {
        /* subscriptions: each holds a connection with one NOTIFY in flight; they end with the connection cap */
        for (n = 0; n < 300; n++)
        {
            struct wex_evtlog_header hdr = { WEX_EVTLOG_MAGIC, WEX_EVTLOG_VERSION, WEX_EVTLOG_NOTIFY, sizeof(hdr), 0, 0, { 0 } };
            DWORD written;

            if ((pipes[n] = connect_log()) == INVALID_HANDLE_VALUE) break;
            if ((status = open_handle( pipes[n], WEX_EVTLOG_OPEN_LOG, L"Application", &handle ))) break;
            hdr.handle = handle;
            WriteFile( pipes[n], &hdr, sizeof(hdr), &written, NULL );  /* left in flight */
            granted++;
        }
        printf( "caps=subs granted=%lu refused=%lu\n", (unsigned long)granted, (unsigned long)status );
    }
    for (i = 0; i < n && i < 300; i++) if (pipes[i] != INVALID_HANDLE_VALUE) CloseHandle( pipes[i] );
    (void)out;
    return 0;
}

/* a handle and a subscription that survive a restart of the service: reconnect, open again, subscribe again */
static int hold( const char *log, DWORD seconds )
{
    WCHAR *name = wide( log );
    DWORD start = GetTickCount(), status, reconnects = 0;
    struct wex_evtlog_header out;
    unsigned int handle;

    while (GetTickCount() - start < seconds * 1000)
    {
        HANDLE pipe = connect_log();

        if (pipe == INVALID_HANDLE_VALUE || open_handle( pipe, WEX_EVTLOG_OPEN_LOG, name, &handle ))
        {
            if (pipe != INVALID_HANDLE_VALUE) CloseHandle( pipe );
            Sleep( 500 );
            continue;
        }
        printf( "hold=subscribed reconnects=%lu\n", (unsigned long)reconnects );
        fflush( stdout );
        status = call( pipe, WEX_EVTLOG_NOTIFY, handle, 0, 0, NULL, 0, &out, NULL, NULL );
        CloseHandle( pipe );
        if (!status)
        {
            printf( "hold=notified reconnects=%lu\n", (unsigned long)reconnects );
            if (reconnects) return 0;
            continue;
        }
        reconnects++;  /* the service went away (restart): connect again */
        Sleep( 500 );
    }
    printf( "hold=timeout reconnects=%lu\n", (unsigned long)reconnects );
    return 1;
}

int main( int argc, char **argv )
{
    struct wex_evtlog_header out;
    unsigned int handle = 0;
    DWORD status, size;
    HANDLE pipe;
    BYTE *buf;

    if (argc < 2) return 2;
    if (!strcmp( argv[1], "squat" ))
    {
        HANDLE h = CreateNamedPipeW( WEX_EVTLOG_PIPE, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE, 1, 4096, 4096, 0, NULL );
        printf( "squat=%lu\n", h == INVALID_HANDLE_VALUE ? (unsigned long)GetLastError() : 0ul );
        return 0;
    }
    if (!strcmp( argv[1], "sqos" ))
    {
        printf( "sqos=%#lx identification=%d\n", (unsigned long)CLIENT_FLAGS,
                (CLIENT_FLAGS & 0x30000) == SECURITY_IDENTIFICATION );
        return 0;
    }
    if (!strcmp( argv[1], "openbackup" ) && argc == 3)
    {
        /* parsed here, the service is not involved */
        HANDLE file = CreateFileA( argv[2], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        DWORD len, got, pos = sizeof(struct file_header), count = 0;
        if (file == INVALID_HANDLE_VALUE) { printf( "openbackup=%lu\n", (unsigned long)GetLastError() ); return 1; }
        len = GetFileSize( file, NULL );
        buf = malloc( len );
        ReadFile( file, buf, len, &got, NULL );
        CloseHandle( file );
        while (pos + sizeof(EVENTLOGRECORD) + 8 <= len)
        {
            const EVENTLOGRECORD *rec = (const EVENTLOGRECORD *)(buf + pos);
            if (rec->Length > len - pos - 8 || RtlComputeCrc32( 0, buf + pos, rec->Length ) != *(DWORD *)(buf + pos + rec->Length))
                break;
            count++;
            pos += rec->Length + 8;
        }
        printf( "openbackup=0 records=%lu\n", (unsigned long)count );
        return 0;
    }
    if (!strcmp( argv[1], "caps" ) && argc == 3) return caps_test( argv[2] );
    if (!strcmp( argv[1], "hold" ) && argc == 4) return hold( argv[2], atoi( argv[3] ) );

    if ((pipe = connect_log()) == INVALID_HANDLE_VALUE)
    {
        printf( "connect=%lu\n", (unsigned long)GetLastError() );
        return 1;
    }
    if (!strcmp( argv[1], "frame" ) && argc == 3) return frame_test( pipe, argv[2] );
    if (!strcmp( argv[1], "report" ) && argc >= 5)
    {
        WCHAR *strings[64];
        int i, count = argc - 5 > 64 ? 64 : argc - 5;
        BYTE *body;

        for (i = 0; i < count; i++) strings[i] = wide( argv[5 + i] );
        if ((status = open_handle( pipe, WEX_EVTLOG_OPEN_SOURCE, wide( argv[2] ), &handle )))
        {
            printf( "report=%lu (open)\n", (unsigned long)status );
            return 0;
        }
        body = report_body( atoi( argv[3] ), strtoul( argv[4], NULL, 0 ), count, strings, NULL, 0, 0, &size );
        status = call( pipe, WEX_EVTLOG_REPORT, handle, 0, 0, body, size, &out, NULL, NULL );
        printf( "report=%lu\n", (unsigned long)status );
        return 0;
    }
    if (!strcmp( argv[1], "fill" ) && argc == 5)
    {
        /* N reports of one string of SIZE characters, until the first failure */
        DWORD n, count = strtoul( argv[3], NULL, 0 ), len = strtoul( argv[4], NULL, 0 ), i;
        WCHAR *s = malloc( (len + 1) * sizeof(WCHAR) );
        BYTE *body;

        for (i = 0; i < len; i++) s[i] = 'a' + i % 26;
        s[len] = 0;
        if ((status = open_handle( pipe, WEX_EVTLOG_OPEN_SOURCE, wide( argv[2] ), &handle )))
        {
            printf( "fill=%lu n=0 (open)\n", (unsigned long)status );
            return 0;
        }
        body = report_body( EVENTLOG_INFORMATION_TYPE, 2000, 1, &s, NULL, 0, 0, &size );
        for (n = 0, status = 0; n < count; n++)
            if ((status = call( pipe, WEX_EVTLOG_REPORT, handle, 0, 0, body, size, &out, NULL, NULL ))) break;
        printf( "fill=%lu n=%lu\n", (unsigned long)status, (unsigned long)n );
        return 0;
    }
    if (argc < 3) return 2;
    if ((status = open_handle( pipe, WEX_EVTLOG_OPEN_LOG, wide( argv[2] ), &handle )))
    {
        printf( "%s=%lu (open)\n", argv[1], (unsigned long)status );
        return 0;
    }
    if (!strcmp( argv[1], "read" ))
    {
        DWORD flags = EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, number = 0, bufsize = 0x10000, n = 0;
        int i;
        for (i = 3; i < argc; i++)
        {
            if (!strcmp( argv[i], "back" )) flags = (flags & ~EVENTLOG_FORWARDS_READ) | EVENTLOG_BACKWARDS_READ;
            if (!strcmp( argv[i], "seek" ) && i + 1 < argc)
            {
                flags = (flags & ~EVENTLOG_SEQUENTIAL_READ) | EVENTLOG_SEEK_READ;
                number = strtoul( argv[++i], NULL, 0 );
            }
        }
        for (;;)
        {
            status = call( pipe, WEX_EVTLOG_READ, handle, flags, number, &bufsize, sizeof(bufsize), &out, &buf, &size );
            if (status) break;
            print_records( buf, size );
            free( buf );
            n++;
            if (flags & EVENTLOG_SEEK_READ) break;
        }
        printf( "read=%lu\n", (unsigned long)(status == ERROR_HANDLE_EOF || (n && !status) ? 0 : status) );
        return 0;
    }
    if (!strcmp( argv[1], "count" ) || !strcmp( argv[1], "oldest" ) || !strcmp( argv[1], "info" ))
    {
        unsigned int op = !strcmp( argv[1], "count" ) ? WEX_EVTLOG_COUNT :
                          !strcmp( argv[1], "oldest" ) ? WEX_EVTLOG_OLDEST : WEX_EVTLOG_INFO;
        BYTE *body = NULL;
        status = call( pipe, op, handle, 0, 0, NULL, 0, &out, &body, &size );
        printf( "%s=%lu value=%u", argv[1], (unsigned long)status, out.arg[0] );
        if (op == WEX_EVTLOG_INFO && !status && size >= sizeof(struct wex_evtlog_information))
        {
            struct wex_evtlog_information info;
            memcpy( &info, body, sizeof(info) );
            printf( " full=%u count=%u oldest=%u next=%u maxsize=%u retention=%u filesize=%llu created=%d written=%d read=%d",
                    info.full, info.count, info.oldest, info.next, info.max_size, info.retention, info.file_size,
                    info.created != 0, info.written != 0, info.read != 0 );
        }
        printf( "\n" );
        free( body );
        return 0;
    }
    if (!strcmp( argv[1], "backup" ) && argc == 4)
    {
        DWORD records;
        status = backup( pipe, handle, argv[3], &records );
        printf( "backup=%lu records=%lu\n", (unsigned long)status, (unsigned long)records );
        return 0;
    }
    if (!strcmp( argv[1], "clear" ))
    {
        DWORD records = 0, last = 0, retries = 0;
        if (argc == 4) status = backup_and_clear( pipe, handle, argv[3], TRUE, &records, &last, &retries );
        else status = call( pipe, WEX_EVTLOG_CLEAR, handle, 0, 0, NULL, 0, &out, NULL, NULL );
        printf( "clear=%lu backed=%lu last=%lu retries=%lu\n", (unsigned long)status, (unsigned long)records,
                (unsigned long)last, (unsigned long)retries );
        return 0;
    }
    if (!strcmp( argv[1], "notify" ) && argc == 4)
    {
        status = call( pipe, WEX_EVTLOG_NOTIFY, handle, 0, 0, NULL, 0, &out, NULL, NULL );
        printf( "notify=%lu\n", (unsigned long)status );
        return 0;
    }
    return 2;
}
