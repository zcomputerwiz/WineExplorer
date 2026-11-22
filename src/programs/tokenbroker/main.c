/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token broker: the system role behind Windows.Security.Authentication.Web.Core (docs/WAM_BROKER_PLAN.md,
 * docs/WAM_BROKER_DESIGN.md). One per user and session, started by the client module on demand; it ends after
 * TOKENBROKER_IDLE_MS without a client.
 *
 * This file: the process, the pipe and the protocol (wine/tokenbroker.h, step 1 of the plan). The providers, the
 * cache and the sign-in (steps 2 to 4) are in provider.c, cache.c, oauth.c, signin.c and webview.c.
 *
 *  - The pipe is TOKENBROKER_PIPE_PREFIX and the user's SID; its descriptor admits that user only, the first instance
 *    is created with FILE_FLAG_FIRST_PIPE_INSTANCE (a second broker of the user ends at once, saying so), and every
 *    client's token user must be the broker's own: its process's primary token, as Wine does not check the pipe's
 *    descriptor when a client opens it and impersonates the client only while elevation is enforced.
 *  - Callers with a restricted token, below medium integrity or in an AppContainer are refused with an error.
 *  - Never in session 0: tokens are the user's, never the machine's.
 *  - Nothing secret is logged, also not at trace level.
 */

#include "broker.h"

#include <sddl.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

#define TOKENBROKER_IDLE_MS (10 * 60 * 1000)

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

/* What a caller's token allows: CALLER_OK, CALLER_OTHER_USER (the connection is dropped), or a refusal that every
 * request of the connection is answered with (a restricted token, an integrity level below medium, an AppContainer:
 * sandboxed code that must not get the user's tokens). */
enum caller_check
{
    CALLER_OK,
    CALLER_OTHER_USER,
    CALLER_RESTRICTED,
    CALLER_LOW_INTEGRITY,
    CALLER_APPCONTAINER,
};

static enum caller_check check_token( HANDLE token )
{
    char buffer[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    TOKEN_MANDATORY_LABEL *label = (TOKEN_MANDATORY_LABEL *)buffer;
    DWORD len, app_container = 0, rid;
    BOOL same;
    SID *sid;

    if (!(sid = token_user( token ))) return CALLER_OTHER_USER;
    same = EqualSid( sid, own_sid );
    free( sid );
    if (!same) return CALLER_OTHER_USER;
    if (IsTokenRestricted( token )) return CALLER_RESTRICTED;
    /* an integrity level that cannot be read counts as low: the check fails closed */
    if (!GetTokenInformation( token, TokenIntegrityLevel, buffer, sizeof(buffer), &len ) ||
        !*GetSidSubAuthorityCount( label->Label.Sid ))
        return CALLER_LOW_INTEGRITY;
    rid = *GetSidSubAuthority( label->Label.Sid, *GetSidSubAuthorityCount( label->Label.Sid ) - 1 );
    if (rid < SECURITY_MANDATORY_MEDIUM_RID) return CALLER_LOW_INTEGRITY;
    if (GetTokenInformation( token, TokenIsAppContainer, &app_container, sizeof(app_container), &len ) &&
        app_container)
        return CALLER_APPCONTAINER;
    return CALLER_OK;
}

/* The caller at the other end of the pipe. Its process's primary token is checked always: without elevation
 * enforcement Wine's ImpersonateNamedPipeClient gives the server its own token (server/named_pipe.c,
 * FSCTL_PIPE_IMPERSONATE), so the impersonation token alone proves nothing. Where impersonation does give the client's
 * token (the client's thread, possibly impersonating someone else), that one is checked as well. */
static enum caller_check check_caller( HANDLE pipe )
{
    enum caller_check ret;
    HANDLE process, token;
    ULONG pid;

    /* the query right, not only the limited one: with elevation enforced WineExplorer's server gives a process's token
     * only to a handle with PROCESS_QUERY_INFORMATION (server/token.c, open_token) */
    if (!GetNamedPipeClientProcessId( pipe, &pid ) ||
        !(process = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, pid )))
        return CALLER_OTHER_USER;
    if (!OpenProcessToken( process, TOKEN_QUERY, &token ))
    {
        CloseHandle( process );
        return CALLER_OTHER_USER;
    }
    CloseHandle( process );
    ret = check_token( token );
    CloseHandle( token );
    if (ret != CALLER_OK) return ret;

    if (!ImpersonateNamedPipeClient( pipe )) return CALLER_OTHER_USER;
    if (OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ))
    {
        ret = check_token( token );
        CloseHandle( token );
    }
    else ret = CALLER_OTHER_USER;
    RevertToSelf();
    return ret;
}

/* the answer to every request of a caller that is refused */
static void refuse( const struct request *req, enum caller_check check, struct reply *reply )
{
    static const WCHAR *messages[] =
    {
        NULL, NULL,
        L"Wine's token broker does not serve programs with a restricted token.",
        L"Wine's token broker does not serve programs below medium integrity.",
        L"Wine's token broker does not serve AppContainer programs.",
    };

    if (req->kind == TOKENBROKER_FIND_ACCOUNTS)
        reply_error( reply, FIND_PROVIDER_ERROR, E_ACCESSDENIED, messages[check] );
    else if (req->kind == TOKENBROKER_FIND_ACCOUNT)
    {
        reply_error( reply, 1, E_ACCESSDENIED, messages[check] );
    }
    else reply_error( reply, STATUS_PROVIDER_ERROR, E_ACCESSDENIED, messages[check] );
}

/* the request's fields as NUL-terminated strings, its properties as pairs */
static BOOL parse_request( const char *buffer, DWORD size, struct request *req )
{
    const struct tokenbroker_request *msg = (const struct tokenbroker_request *)buffer;
    DWORD offset = sizeof(*msg);
    const WCHAR *p, *end, *key;
    unsigned int i, j, n;

    memset( req, 0, sizeof(*req) );
    if (size < sizeof(msg->hdr) || msg->hdr.magic != TOKENBROKER_MAGIC || msg->hdr.version != TOKENBROKER_VERSION ||
        msg->hdr.size != size)
        return FALSE;
    req->kind = msg->hdr.kind;
    if (req->kind == TOKENBROKER_HELLO) return TRUE;
    if (size < sizeof(*msg)) return FALSE;
    req->prompt = msg->prompt_type;
    req->window = msg->window;
    for (i = 0; i < TOKENBROKER_FIELD_COUNT; i++)
    {
        if (msg->field_len[i] % sizeof(WCHAR) || msg->field_len[i] > size - offset) return FALSE;
        p = (const WCHAR *)(buffer + offset);
        n = msg->field_len[i] / sizeof(WCHAR);
        offset += msg->field_len[i];
        if (i == TOKENBROKER_FIELD_PROPERTIES)
        {
            /* "key\0value\0...": count, then copy */
            unsigned int count = 0;
            for (j = 0; j < n; j++) if (!p[j]) count++;
            if (count % 2 || (n && p[n - 1])) return FALSE;
            req->prop_count = count / 2;
            if (req->prop_count && (!(req->prop_key = calloc( req->prop_count, sizeof(WCHAR *) )) ||
                                    !(req->prop_value = calloc( req->prop_count, sizeof(WCHAR *) ))))
                return FALSE;
            end = p + n;
            for (j = 0; j < req->prop_count; j++)
            {
                key = p;
                p += wcslen( p ) + 1;
                if (!(req->prop_key[j] = strdupW( key )) || !(req->prop_value[j] = strdupW( p ))) return FALSE;
                p += wcslen( p ) + 1;
                if (p > end) return FALSE;
            }
            if (!(req->field[i] = strdupW( L"" ))) return FALSE;
            continue;
        }
        for (j = 0; j < n; j++) if (!p[j]) return FALSE;
        if (!(req->field[i] = strndupW( p, n ))) return FALSE;
    }
    return offset == size;
}

static void free_request( struct request *req )
{
    unsigned int i;

    for (i = 0; i < TOKENBROKER_FIELD_COUNT; i++) free( req->field[i] );
    free( req->program_path );
    for (i = 0; i < req->prop_count; i++)
    {
        if (req->prop_key) free( req->prop_key[i] );
        if (req->prop_value) free( req->prop_value[i] );
    }
    free( req->prop_key );
    free( req->prop_value );
    memset( req, 0, sizeof(*req) );
}

static void free_reply( struct reply *reply )
{
    unsigned int i;

    for (i = 0; i < TOKENBROKER_REPLY_COUNT; i++)
    {
        if (reply->field[i].data) SecureZeroMemory( reply->field[i].data, reply->field[i].len * sizeof(WCHAR) );
        text_free( &reply->field[i] );
    }
}

/* the reply message for the request (FALSE when it does not fit) */
static BOOL build_reply( const struct tokenbroker_request *msg, const struct reply *reply, char *out, DWORD *size )
{
    struct tokenbroker_reply *hdr = (struct tokenbroker_reply *)out;
    DWORD offset = sizeof(*hdr);
    unsigned int i;

    memset( hdr, 0, sizeof(*hdr) );
    hdr->hdr.magic = TOKENBROKER_MAGIC;
    hdr->hdr.version = TOKENBROKER_VERSION;
    hdr->hdr.kind = msg->hdr.kind;
    hdr->hdr.request_id = msg->hdr.request_id;
    hdr->status = reply->status;
    hdr->error_code = reply->code;
    for (i = 0; i < TOKENBROKER_REPLY_COUNT; i++)
    {
        hdr->field_len[i] = reply->field[i].len * sizeof(WCHAR);
        if (hdr->field_len[i] > TOKENBROKER_MAX_MESSAGE - offset) return FALSE;
        if (hdr->field_len[i]) memcpy( out + offset, reply->field[i].data, hdr->field_len[i] );
        offset += hdr->field_len[i];
    }
    hdr->hdr.size = offset;
    *size = offset;
    return TRUE;
}

static void answer( const struct request *req, struct reply *reply )
{
    if (req->kind == TOKENBROKER_HELLO)
    {
        reply->status = 0;
        return;
    }
    if (req->kind < TOKENBROKER_GET_TOKEN_SILENT || req->kind > TOKENBROKER_SIGN_OUT)
    {
        reply_error( reply, STATUS_PROVIDER_ERROR, 0x80004001, L"Unknown request." );
        return;
    }
    provider_answer( req, reply );
}

/* the executable of the pipe's client (allocated), or NULL */
static WCHAR *client_program( HANDLE pipe )
{
    WCHAR path[MAX_PATH];
    DWORD len = ARRAY_SIZE(path);
    HANDLE process;
    ULONG pid;
    BOOL ok;

    if (!GetNamedPipeClientProcessId( pipe, &pid )) return NULL;
    if (!(process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid ))) return NULL;
    ok = QueryFullProcessImageNameW( process, 0, path, &len );
    CloseHandle( process );
    return ok ? strdupW( path ) : NULL;
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
    struct request req;
    struct reply reply;
    char *buffer, *out;
    DWORD size, done, out_size;
    HANDLE event;
    enum caller_check check = CALLER_OK;
    BOOL checked = FALSE, ok;

    buffer = malloc( TOKENBROKER_MAX_MESSAGE );
    out = malloc( TOKENBROKER_MAX_MESSAGE );
    event = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (!buffer || !out || !event) goto done;
    while (pipe_io( pipe, FALSE, buffer, TOKENBROKER_MAX_MESSAGE, &size, event ))
    {
        /* the client can be impersonated once something was read from it */
        if (!checked)
        {
            checked = TRUE;
            if ((check = check_caller( pipe )) == CALLER_OTHER_USER)
            {
                WARN( "a client of another user was refused\n" );
                break;
            }
            if (check != CALLER_OK) WARN( "a sandboxed client is refused (%u)\n", check );
        }
        if (!parse_request( buffer, size, &req ))
        {
            WARN( "a malformed request was refused\n" );
            free_request( &req );
            break;
        }
        TRACE( "request %u kind %u, prompt type %u, window %#x\n", ((struct tokenbroker_request *)buffer)->hdr.request_id,
               req.kind, req.prompt, req.window );
        if (req.kind == TOKENBROKER_REQUEST_TOKEN) req.program_path = client_program( pipe );
        memset( &reply, 0, sizeof(reply) );
        if (check != CALLER_OK && req.kind != TOKENBROKER_HELLO) refuse( &req, check, &reply );
        else answer( &req, &reply );
        ok = build_reply( (struct tokenbroker_request *)buffer, &reply, out, &out_size );
        if (!ok)
        {
            /* too large for a message: an error instead */
            free_reply( &reply );
            memset( &reply, 0, sizeof(reply) );
            reply_error( &reply, STATUS_PROVIDER_ERROR, 0x8007007a, L"The answer is too large." );
            ok = build_reply( (struct tokenbroker_request *)buffer, &reply, out, &out_size );
        }
        free_reply( &reply );
        free_request( &req );
        ok = ok && pipe_io( pipe, TRUE, out, out_size, &done, event ) && done == out_size;
        SecureZeroMemory( out, out_size );
        if (!ok) break;
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
    cache_load();

    for (;;)
    {
        if ((pipe = create_instance( name, &sa, first )) == INVALID_HANDLE_VALUE)
        {
            /* the first instance could not be ours: another broker of the user has the pipe, or another process
             * took its name; this broker is not the one clients reach */
            if (first)
            {
                ERR( "the token broker's pipe %s is held by another process (error %lu): this broker exits\n",
                     debugstr_w( name ), GetLastError() );
                return 0;
            }
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
