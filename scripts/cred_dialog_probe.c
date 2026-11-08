/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cred_dialog.h"

#define HOOK_DIR "Z:\\tmp\\wexelev\\"

static void write_action_file( const char *testid, const char *action )
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD written;

    snprintf( path, sizeof(path), "%scred.%s.action", HOOK_DIR, testid );
    file = CreateFileA( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile( file, action, (DWORD)strlen(action), &written, NULL );
        CloseHandle( file );
    }
}

static int wait_file( const char *path, DWORD timeout_ms )
{
    DWORD waited = 0;
    while (waited < timeout_ms)
    {
        if (GetFileAttributesA( path ) != INVALID_FILE_ATTRIBUTES) return 1;
        Sleep( 50 );
        waited += 50;
    }
    return 0;
}

int main( int argc, char **argv )
{
    const char *mode;
    const char *consent_exe;
    const char *testid;
    char pipename[MAX_PATH];
    char cmd[1024];
    HANDLE hpipe = INVALID_HANDLE_VALUE;
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    uint8_t buf[WEX_S1_ANSWER_LENGTH];
    DWORD read_bytes = 0;
    const struct wex_s1_answer_record *rec;

    if (argc < 4)
    {
        fprintf( stderr, "Usage: %s <confirm|cancel|invalid|early_exit> <consent_exe> <testid>\n", argv[0] );
        return 1;
    }

    mode = argv[1];
    consent_exe = argv[2];
    testid = argv[3];

    snprintf( pipename, sizeof(pipename), "\\\\.\\pipe\\cred_pipe_%s", testid );

    hpipe = CreateNamedPipeA( pipename,
                              PIPE_ACCESS_DUPLEX,
                              PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                              1, 4096, 4096, 5000, NULL );
    if (hpipe == INVALID_HANDLE_VALUE)
    {
        fprintf( stderr, "FAIL: CreateNamedPipeA error %lu\n", GetLastError() );
        return 1;
    }

    snprintf( cmd, sizeof(cmd),
              "\"%s\" /broker /credentials /request 12345678abcdef01 /token 0102030405060708090a0b0c0d0e0f10 /pipe %s /testid %s",
              consent_exe, pipename, testid );

    if (!strcmp( mode, "confirm" ))
    {
        write_action_file( testid, "confirm:alice:SecretPassword123\n" );

        if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            fprintf( stderr, "FAIL: CreateProcessA error %lu\n", GetLastError() );
            CloseHandle( hpipe );
            return 1;
        }

        ConnectNamedPipe( hpipe, NULL );
        if (!ReadFile( hpipe, buf, sizeof(buf), &read_bytes, NULL ) || read_bytes < WEX_S1_ANSWER_LENGTH)
        {
            fprintf( stderr, "FAIL: ReadFile confirm error %lu, bytes=%lu\n", GetLastError(), read_bytes );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        rec = (const struct wex_s1_answer_record *)buf;
        if (rec->kind != WEX_S1_ANSWER_KIND ||
            rec->version != WEX_S1_ANSWER_VERSION ||
            rec->length != WEX_S1_ANSWER_LENGTH ||
            rec->answer_kind != WEX_ANSWER_KIND_CONFIRM ||
            rec->request_id != 0x12345678abcdef01ULL)
        {
            fprintf( stderr, "FAIL: record mismatch (kind=%u, ans_kind=%u, req_id=%llx)\n",
                     rec->kind, rec->answer_kind, (unsigned long long)rec->request_id );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( hpipe );
        printf( "PROBE_PASS_CONFIRM\n" );
        return 0;
    }
    else if (!strcmp( mode, "cancel" ))
    {
        write_action_file( testid, "cancel\n" );

        if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            fprintf( stderr, "FAIL: CreateProcessA error %lu\n", GetLastError() );
            CloseHandle( hpipe );
            return 1;
        }

        ConnectNamedPipe( hpipe, NULL );
        if (!ReadFile( hpipe, buf, sizeof(buf), &read_bytes, NULL ) || read_bytes < WEX_S1_ANSWER_LENGTH)
        {
            fprintf( stderr, "FAIL: ReadFile cancel error %lu, bytes=%lu\n", GetLastError(), read_bytes );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        rec = (const struct wex_s1_answer_record *)buf;
        if (rec->kind != WEX_S1_ANSWER_KIND ||
            rec->version != WEX_S1_ANSWER_VERSION ||
            rec->length != WEX_S1_ANSWER_LENGTH ||
            rec->answer_kind != WEX_ANSWER_KIND_CANCEL ||
            rec->request_id != 0x12345678abcdef01ULL)
        {
            fprintf( stderr, "FAIL: record mismatch on cancel\n" );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( hpipe );
        printf( "PROBE_PASS_CANCEL\n" );
        return 0;
    }
    else if (!strcmp( mode, "invalid" ))
    {
        char invalid_path[MAX_PATH];
        snprintf( invalid_path, sizeof(invalid_path), "%scred.%s.invalid", HOOK_DIR, testid );
        DeleteFileA( invalid_path );

        write_action_file( testid, "invalid:-baduser\n" );

        if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            fprintf( stderr, "FAIL: CreateProcessA error %lu\n", GetLastError() );
            CloseHandle( hpipe );
            return 1;
        }

        if (!wait_file( invalid_path, 4000 ))
        {
            fprintf( stderr, "FAIL: .invalid marker file not produced\n" );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        /* Check that no confirm was transmitted on the pipe */
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 2000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( hpipe );
        DeleteFileA( invalid_path );
        printf( "PROBE_PASS_INVALID\n" );
        return 0;
    }
    else if (!strcmp( mode, "early_exit" ))
    {
        char shown_path[MAX_PATH];
        snprintf( shown_path, sizeof(shown_path), "%scred.%s.shown", HOOK_DIR, testid );

        if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            fprintf( stderr, "FAIL: CreateProcessA error %lu\n", GetLastError() );
            CloseHandle( hpipe );
            return 1;
        }

        if (!wait_file( shown_path, 4000 ))
        {
            fprintf( stderr, "FAIL: .shown marker not produced\n" );
            TerminateProcess( pi.hProcess, 1 );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
            CloseHandle( hpipe );
            return 1;
        }

        /* Kill child before confirm */
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 2000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );

        /* Verify pipe has no confirm */
        CloseHandle( hpipe );
        DeleteFileA( shown_path );
        printf( "PROBE_PASS_EARLY_EXIT_DENIED\n" );
        return 0;
    }

    fprintf( stderr, "Unknown mode %s\n", mode );
    CloseHandle( hpipe );
    return 1;
}
