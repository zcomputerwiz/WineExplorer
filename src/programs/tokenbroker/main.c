/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token broker: the system role behind Windows.Security.Authentication.Web.Core (docs/WAM_BROKER_PLAN.md,
 * docs/WAM_BROKER_DESIGN.md). One per user and session, started by the client module on demand; it ends after
 * TOKENBROKER_IDLE_MS without a client.
 *
 * Step 1 of the plan: the process, the pipe and the protocol (wine/tokenbroker.h). It answers as Wine's diagnostic
 * provider does (a silent request needs the user; an interactive one has no provider yet), so a program sees no
 * change; the providers (step 2) and the sign-in (steps 3 and 4) answer here later.
 *
 *  - The pipe is TOKENBROKER_PIPE_PREFIX and the user's SID; its descriptor admits that user only, the first instance
 *    is created with FILE_FLAG_FIRST_PIPE_INSTANCE (a second broker of the user ends at once), and every client's
 *    token user must be the broker's own.
 *  - Never in session 0: tokens are the user's, never the machine's.
 *  - Nothing secret is logged, also not at trace level.
 */

#include <stdarg.h>
#include <stdlib.h>

#include <windows.h>
#include <sddl.h>

#include "wine/tokenbroker.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

#define TOKENBROKER_IDLE_MS (10 * 60 * 1000)

/* WebTokenRequestStatus */
#define STATUS_USER_INTERACTION_REQUIRED 3
#define STATUS_PROVIDER_ERROR            5

static SID *own_sid;
static LONG active_clients;

/* the process token's user, allocated */
static SID *token_user( HANDLE token )
{
    char buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    DWORD len;
    SID *sid;

    if (!GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len )) return NULL;
    len = GetLengthSid( user->User.Sid );
    if (!(sid = malloc( len ))) return NULL;
    CopySid( len, sid, user->User.Sid );
    return sid;
}

/* is the client at the other end of the pipe a process of this broker's user? */
static BOOL client_is_own_user( HANDLE pipe )
{
    HANDLE token;
    SID *sid;
    BOOL ret = FALSE;

    if (!ImpersonateNamedPipeClient( pipe )) return FALSE;
    if (OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ))
    {
        if ((sid = token_user( token )))
        {
            ret = EqualSid( sid, own_sid );
            free( sid );
        }
        CloseHandle( token );
    }
    RevertToSelf();
    return ret;
}

/* a field of a request, as a counted UTF-16 string into the message */
struct field
{
    const WCHAR *text;
    unsigned int len;   /* characters */
};

static BOOL parse_request( const char *buffer, DWORD size, const struct tokenbroker_request **out,
                           struct field fields[TOKENBROKER_FIELD_COUNT] )
{
    const struct tokenbroker_request *req = (const struct tokenbroker_request *)buffer;
    DWORD offset = sizeof(*req);
    unsigned int i;

    if (size < sizeof(req->hdr) || req->hdr.magic != TOKENBROKER_MAGIC || req->hdr.version != TOKENBROKER_VERSION ||
        req->hdr.size != size)
        return FALSE;
    if (req->hdr.kind == TOKENBROKER_HELLO)
    {
        *out = req;
        return TRUE;
    }
    if (size < sizeof(*req)) return FALSE;
    for (i = 0; i < TOKENBROKER_FIELD_COUNT; i++)
    {
        if (req->field_len[i] % sizeof(WCHAR) || req->field_len[i] > size - offset) return FALSE;
        fields[i].text = (const WCHAR *)(buffer + offset);
        fields[i].len = req->field_len[i] / sizeof(WCHAR);
        offset += req->field_len[i];
    }
    if (offset != size) return FALSE;
    *out = req;
    return TRUE;
}

static BOOL field_is( const struct field *field, const WCHAR *text )
{
    return field->len == wcslen( text ) && !memcmp( field->text, text, field->len * sizeof(WCHAR) );
}

/* Step 1: the answers of Wine's diagnostic provider. A silent request needs the user, with the error Windows 11's
 * Microsoft account provider gives (0x80860006 "Error" for the consumers authority, 0xcaa10001 otherwise); an
 * interactive one finishes with ProviderError until the sign-in exists. */
static void answer( const struct tokenbroker_request *req, const struct field *fields, int *status,
                    unsigned int *code, const WCHAR **message )
{
    switch (req->hdr.kind)
    {
    case TOKENBROKER_HELLO:
        *status = 0;
        *code = 0;
        *message = L"";
        break;
    case TOKENBROKER_GET_TOKEN_SILENT:
        *status = STATUS_USER_INTERACTION_REQUIRED;
        if (field_is( &fields[TOKENBROKER_FIELD_AUTHORITY], L"consumers" ))
        {
            *code = 0x80860006;
            *message = L"Error";
        }
        else
        {
            *code = 0xcaa10001;
            *message = L"Need user interaction to continue.";
        }
        break;
    default:
        *status = STATUS_PROVIDER_ERROR;
        *code = E_NOTIMPL;
        *message = L"Wine's token broker has no account provider yet.";
        break;
    }
}

/* one read or write on the pipe (an overlapped handle: the accept loop waits with a timeout), waited for */
static BOOL pipe_io( HANDLE pipe, BOOL write, void *buffer, DWORD size, DWORD *done, HANDLE event )
{
    OVERLAPPED ov = { .hEvent = event };
    BOOL ret;

    ResetEvent( event );
    ret = write ? WriteFile( pipe, buffer, size, done, &ov ) : ReadFile( pipe, buffer, size, done, &ov );
    if (!ret && GetLastError() == ERROR_IO_PENDING) ret = GetOverlappedResult( pipe, &ov, done, TRUE );
    return ret;
}

static DWORD WINAPI client_thread( void *arg )
{
    HANDLE pipe = arg;
    struct field fields[TOKENBROKER_FIELD_COUNT] = {{0}};
    const struct tokenbroker_request *req;
    struct tokenbroker_reply *reply;
    char *buffer, *out;
    const WCHAR *message;
    unsigned int code, message_len;
    DWORD size, done;
    HANDLE event;
    BOOL checked = FALSE;
    int status;

    buffer = malloc( TOKENBROKER_MAX_MESSAGE );
    out = malloc( TOKENBROKER_MAX_MESSAGE );
    event = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (!buffer || !out || !event) goto done;
    while (pipe_io( pipe, FALSE, buffer, TOKENBROKER_MAX_MESSAGE, &size, event ))
    {
        /* the client can be impersonated once something was read from it */
        if (!checked && !(checked = client_is_own_user( pipe )))
        {
            WARN( "a client of another user was refused\n" );
            break;
        }
        if (!parse_request( buffer, size, &req, fields ))
        {
            WARN( "a malformed request was refused\n" );
            break;
        }
        TRACE( "request %u kind %u, prompt type %u, window %#x\n", req->hdr.request_id, req->hdr.kind,
               req->hdr.kind == TOKENBROKER_HELLO ? 0 : req->prompt_type,
               req->hdr.kind == TOKENBROKER_HELLO ? 0 : req->window );
        answer( req, fields, &status, &code, &message );
        message_len = wcslen( message ) * sizeof(WCHAR);
        reply = (struct tokenbroker_reply *)out;
        reply->hdr.magic = TOKENBROKER_MAGIC;
        reply->hdr.version = TOKENBROKER_VERSION;
        reply->hdr.kind = req->hdr.kind;
        reply->hdr.request_id = req->hdr.request_id;
        reply->hdr.size = sizeof(*reply) + message_len;
        reply->status = status;
        reply->error_code = code;
        reply->message_len = message_len;
        memcpy( reply + 1, message, message_len );
        if (!pipe_io( pipe, TRUE, out, reply->hdr.size, &done, event ) || done != reply->hdr.size) break;
    }
done:
    if (event) CloseHandle( event );
    free( buffer );
    free( out );
    DisconnectNamedPipe( pipe );
    CloseHandle( pipe );
    InterlockedDecrement( &active_clients );
    return 0;
}

static HANDLE create_instance( const WCHAR *name, SECURITY_ATTRIBUTES *sa, BOOL first )
{
    return CreateNamedPipeW( name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             PIPE_UNLIMITED_INSTANCES, TOKENBROKER_MAX_MESSAGE, TOKENBROKER_MAX_MESSAGE, 0, sa );
}

int __cdecl wmain( int argc, WCHAR *argv[] )
{
    WCHAR name[256], sddl[256], *sid_string;
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    OVERLAPPED ov = {0};
    HANDLE token, pipe, thread;
    DWORD session = 0, wait;
    BOOL first = TRUE;

    if (ProcessIdToSessionId( GetCurrentProcessId(), &session ) && !session)
    {
        ERR( "the token broker does not run in session 0\n" );
        return 1;
    }
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return 1;
    own_sid = token_user( token );
    CloseHandle( token );
    if (!own_sid || !ConvertSidToStringSidW( own_sid, &sid_string )) return 1;
    swprintf( name, ARRAY_SIZE(name), L"%s%s", TOKENBROKER_PIPE_PREFIX, sid_string );
    /* the user only: full control for the user's SID, nothing for anyone else */
    swprintf( sddl, ARRAY_SIZE(sddl), L"D:P(A;;GA;;;%s)", sid_string );
    LocalFree( sid_string );
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL ))
        return 1;
    if (!(ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL ))) return 1;

    for (;;)
    {
        if ((pipe = create_instance( name, &sa, first )) == INVALID_HANDLE_VALUE)
        {
            /* another broker of the user has the pipe: this one is not needed */
            if (first && GetLastError() == ERROR_ACCESS_DENIED) return 0;
            ERR( "cannot create the pipe: %lu\n", GetLastError() );
            return 1;
        }
        first = FALSE;
        ResetEvent( ov.hEvent );
        if (!ConnectNamedPipe( pipe, &ov ))
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                /* idle: no new client for TOKENBROKER_IDLE_MS while none is connected ends the broker */
                while ((wait = WaitForSingleObject( ov.hEvent, TOKENBROKER_IDLE_MS )) == WAIT_TIMEOUT)
                    if (!active_clients) return 0;
                if (wait != WAIT_OBJECT_0)
                {
                    CloseHandle( pipe );
                    continue;
                }
            }
            else if (GetLastError() != ERROR_PIPE_CONNECTED)
            {
                CloseHandle( pipe );
                continue;
            }
        }
        InterlockedIncrement( &active_clients );
        if (!(thread = CreateThread( NULL, 0, client_thread, pipe, 0, NULL )))
        {
            InterlockedDecrement( &active_clients );
            DisconnectNamedPipe( pipe );
            CloseHandle( pipe );
            continue;
        }
        CloseHandle( thread );
    }
}
