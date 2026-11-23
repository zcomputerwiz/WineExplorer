/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The broker's account providers (docs/WAM_BROKER_PLAN.md, patterns 2, 6, 7 and 9). A provider is found by its id and
 * authority, as on Windows, and implements Windows' operation kinds: get a token silently, request a token (may show
 * the sign-in window), list accounts, find an account, sign out; a kind it does not know answers "not supported".
 *
 *  - Microsoft accounts: provider https://login.microsoft.com, authorities consumers, organizations, common or a
 *    tenant, at https://login.microsoftonline.com. Its sign-in window is WebView2 (webview.c).
 *  - The test provider: https://login.wine.test, only while HKCU\Software\Wine\WebAuthentication "TestAuthority"
 *    names a test authority on this machine (http://127.0.0.1:PORT/ or http://localhost:PORT/; any other value is
 *    refused). Its sign-in is the test hook (signin_run_test), or with "TestWindow" = 1 the WebView2 window, so that
 *    the window is tested against the test authority. The Microsoft provider has no test switch.
 *
 * Both use the same OAuth code (oauth.c), redirect URI (oauth_redirect_uri) and answer shape (answer.c). Statuses and
 * error codes come from one table (map_error); anything unmapped is ProviderError with the server's text.
 */

#include "broker.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

#define MSA_PROVIDER_ID     L"https://login.microsoft.com"
#define MSA_AUTHORITY_BASE  L"https://login.microsoftonline.com/"
#define TEST_PROVIDER_ID    L"https://login.wine.test"
#define SETTINGS_KEY        L"Software\\Wine\\WebAuthentication"

/* leave a cached access token alone when it expires within this time */
#define TOKEN_MARGIN        300

/* the errors of a request that needs the user, as Windows 11's Microsoft account provider gives them (Wine's
 * diagnostic provider has the same) */
#define E_MSA_INTERACTION_REQUIRED  0x80860006  /* consumers: "Error" */
#define E_AAD_INTERACTION_REQUIRED  0xcaa10001  /* other authorities: "Need user interaction to continue." */

struct provider
{
    const WCHAR *id;
    BOOL test;
    BOOL test_window;       /* the test provider signs in through the WebView2 window ("TestWindow" = 1) */
    WCHAR base[256];        /* the authority's URL, up to the authority itself */
};

/* the test authority, when the switch names one on this machine */
static BOOL test_authority( WCHAR *base, DWORD size )
{
    static const WCHAR *allowed[] = { L"http://127.0.0.1:", L"http://localhost:" };
    DWORD bytes = size * sizeof(WCHAR);
    unsigned int i;
    const WCHAR *p;
    size_t len;

    if (RegGetValueW( HKEY_CURRENT_USER, SETTINGS_KEY, L"TestAuthority", RRF_RT_REG_SZ, NULL, base, &bytes ))
        return FALSE;
    for (i = 0; i < ARRAY_SIZE(allowed); i++)
        if (!wcsnicmp( base, allowed[i], wcslen( allowed[i] ) )) break;
    if (i == ARRAY_SIZE(allowed))
    {
        WARN( "TestAuthority %s refused: only a test authority on this machine\n", debugstr_w( base ) );
        return FALSE;
    }
    /* a port, then an optional path of plain characters */
    for (p = base + wcslen( allowed[i] ); *p >= '0' && *p <= '9'; p++);
    if (p == base + wcslen( allowed[i] )) return FALSE;
    for (; *p; p++) if (!iswalnum( *p ) && !wcschr( L"/-._", *p )) return FALSE;
    len = wcslen( base );
    if (base[len - 1] != '/')
    {
        if (len + 2 > size) return FALSE;
        base[len] = '/';
        base[len + 1] = 0;
    }
    return TRUE;
}

/* an authority goes into the endpoint's path: plain names only (consumers, organizations, common, a tenant) */
static BOOL valid_authority( const WCHAR *authority )
{
    const WCHAR *p;

    if (!authority || !*authority || wcslen( authority ) > 128) return FALSE;
    for (p = authority; *p; p++) if (!iswalnum( *p ) && !wcschr( L"-._", *p )) return FALSE;
    return wcscmp( authority, L"." ) && wcscmp( authority, L".." );
}

/* the provider for the request's id and authority, or FALSE */
static BOOL find_provider( const struct request *req, struct provider *provider )
{
    const WCHAR *id = req->field[TOKENBROKER_FIELD_PROVIDER_ID];

    memset( provider, 0, sizeof(*provider) );
    if (!wcscmp( id, MSA_PROVIDER_ID ))
    {
        provider->id = MSA_PROVIDER_ID;
        wcscpy( provider->base, MSA_AUTHORITY_BASE );
        return TRUE;
    }
    if (!wcscmp( id, TEST_PROVIDER_ID ) && test_authority( provider->base, ARRAY_SIZE(provider->base) ))
    {
        DWORD value = 0, size = sizeof(value);

        provider->id = TEST_PROVIDER_ID;
        provider->test = TRUE;
        provider->test_window = !RegGetValueW( HKEY_CURRENT_USER, SETTINGS_KEY, L"TestWindow", RRF_RT_REG_DWORD, NULL,
                                               &value, &size ) && value == 1;
        return TRUE;
    }
    return FALSE;
}

/*
 * The status table (pattern 7)
 */

static void interaction_required( const struct request *req, struct reply *reply )
{
    if (!wcscmp( req->field[TOKENBROKER_FIELD_AUTHORITY], L"consumers" ))
        reply_error( reply, STATUS_USER_INTERACTION_REQUIRED, E_MSA_INTERACTION_REQUIRED, L"Error" );
    else
        reply_error( reply, STATUS_USER_INTERACTION_REQUIRED, E_AAD_INTERACTION_REQUIRED,
                     L"Need user interaction to continue." );
}

/* a server's OAuth error code: the status and code programs expect */
static void map_error( const struct request *req, const WCHAR *error_code, const WCHAR *text, struct reply *reply )
{
    static const WCHAR *interaction[] = { L"invalid_grant", L"interaction_required", L"login_required",
                                          L"consent_required" };
    unsigned int i;

    if (error_code)
    {
        for (i = 0; i < ARRAY_SIZE(interaction); i++)
        {
            if (wcscmp( error_code, interaction[i] )) continue;
            interaction_required( req, reply );
            return;
        }
        if (!wcscmp( error_code, L"access_denied" ) || !wcscmp( error_code, L"user_cancel" ))
        {
            reply_error( reply, STATUS_USER_CANCEL, E_CANCELLED, text ? text : L"The user cancelled the sign-in." );
            return;
        }
        if (!wcscmp( error_code, L"network" ))
        {
            reply_error( reply, STATUS_PROVIDER_ERROR, E_NO_NETWORK, text );
            return;
        }
    }
    reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, text ? text : L"The sign-in failed." );
}

/* keeps a token answer: the account with its refresh token (unless the token is for another client, TOKEN_CLIENT,
 * through the account's), and the access token */
static void store_result( const struct provider *provider, const struct request *req, const struct token_result *r,
                          const WCHAR *account_id, const WCHAR *user_name, const WCHAR *token_client )
{
    struct access_token t = {0};
    struct account a = {0};

    cache_lock();
    if (r->refresh_token && !token_client)
    {
        a.provider_id = (WCHAR *)provider->id;
        a.authority = req->field[TOKENBROKER_FIELD_AUTHORITY];
        a.id = (WCHAR *)account_id;
        a.user_name = (WCHAR *)user_name;
        a.client_id = req->field[TOKENBROKER_FIELD_CLIENT_ID];
        a.refresh_token = r->refresh_token;
        if (!cache_store_account( &a )) WARN( "the account was not stored\n" );
    }
    t.provider_id = (WCHAR *)provider->id;
    t.authority = req->field[TOKENBROKER_FIELD_AUTHORITY];
    t.account_id = (WCHAR *)account_id;
    t.client_id = token_client ? (WCHAR *)token_client : req->field[TOKENBROKER_FIELD_CLIENT_ID];
    t.scope = req->field[TOKENBROKER_FIELD_SCOPE];
    t.expires_on = r->expires_on;
    t.token = r->access_token;
    t.id_token = r->id_token;
    t.client_info = r->client_info;
    t.granted_scope = r->scope;
    cache_store_token( &t );
    cache_unlock();
    cache_save();
}

/*
 * The client ids each calling process has used, in memory only: FindAccountAsync names no client, and an empty client
 * id is never "every client", so the accounts such a call may see are those of the client ids the same process has
 * itself used in its other requests (a program finds the account it signed in, and no other program's). Guarded by
 * the cache lock.
 */

#define USED_PROCESSES 16
#define USED_CLIENTS   4

struct used_clients
{
    unsigned int pid;
    unsigned int count;
    unsigned int next;
    WCHAR *client[USED_CLIENTS];
};

static struct used_clients used[USED_PROCESSES];
static unsigned int used_next;

/* the process's entry, or NULL; the caller holds the cache lock */
static struct used_clients *used_clients( unsigned int pid )
{
    unsigned int i;

    for (i = 0; pid && i < USED_PROCESSES; i++) if (used[i].pid == pid) return &used[i];
    return NULL;
}

/* the caller holds the cache lock */
static void remember_client( unsigned int pid, const WCHAR *client_id )
{
    struct used_clients *entry;
    unsigned int j;

    if (!pid || !client_id || !*client_id) return;
    if (!(entry = used_clients( pid )))
    {
        entry = &used[used_next++ % USED_PROCESSES];    /* the oldest process's entry is replaced */
        for (j = 0; j < USED_CLIENTS; j++)
        {
            free( entry->client[j] );
            entry->client[j] = NULL;
        }
        entry->pid = pid;
        entry->count = entry->next = 0;
    }
    for (j = 0; j < entry->count; j++) if (!wcscmp( entry->client[j], client_id )) return;
    j = entry->next++ % USED_CLIENTS;
    free( entry->client[j] );
    entry->client[j] = strdupW( client_id );
    if (entry->count < USED_CLIENTS) entry->count++;
}

/* The account a token request means for CLIENT: the one it names by id or sign-in name, else, when it names none, the
 * client's only account; the caller holds the cache lock */
static struct account *request_account( const struct provider *provider, const struct request *req,
                                        const WCHAR *hint, const WCHAR *client )
{
    const WCHAR *authority = req->field[TOKENBROKER_FIELD_AUTHORITY], *id = req->field[TOKENBROKER_FIELD_ACCOUNT_ID];
    struct account *account, **list;

    if ((account = cache_find_account( provider->id, authority, id, hint, client ))) return account;
    if (*id || (hint && *hint)) return NULL;
    if (cache_list_accounts( provider->id, authority, client, &list ) == 1) account = list[0];
    free( list );
    return account;
}

/* An account that the request's process holds under another client id it has used, and that client id; the caller
 * holds the cache lock */
static struct account *account_of_used_client( const struct provider *provider, const struct request *req,
                                               const WCHAR *hint, const WCHAR **client )
{
    struct used_clients *entry = used_clients( req->client_pid );
    struct account *account;
    unsigned int j;

    *client = NULL;
    for (j = 0; entry && j < entry->count; j++)
    {
        const WCHAR *other = entry->client[j];

        if (!other || !wcscmp( other, req->field[TOKENBROKER_FIELD_CLIENT_ID] )) continue;
        if ((account = request_account( provider, req, hint, other )))
        {
            *client = other;
            return account;
        }
    }
    return NULL;
}

/*
 * The operations
 */

static void get_token_silent( const struct provider *provider, const struct request *req, struct reply *reply )
{
    const WCHAR *client = req->field[TOKENBROKER_FIELD_CLIENT_ID], *authority = req->field[TOKENBROKER_FIELD_AUTHORITY];
    const WCHAR *scope = req->field[TOKENBROKER_FIELD_SCOPE], *hint = request_login_hint( req );
    /* Nested App Authentication: the token is for the child app, through the parent (the request's client) */
    const WCHAR *child = request_property( req, L"child_client_id" );
    const WCHAR *child_redirect = request_property( req, L"child_redirect_uri" );
    /* the client whose account and refresh token are used: the request's own, or another one of the same process */
    const WCHAR *parent = client, *other;
    WCHAR *account_id = NULL, *user_name = NULL, *refresh = NULL, *other_redirect = NULL;
    WCHAR *broker_client = NULL, *broker_redirect = NULL;
    struct oauth_broker broker;
    struct account *account;
    struct access_token *token;
    struct token_result r;
    BOOL ok;

    if (!child || !*child || !child_redirect || !*child_redirect) child = child_redirect = NULL;

    cache_lock();
    account = request_account( provider, req, hint, client );
    /* Windows keeps a Microsoft account for the user, not per client id: any client id gets its token silently for it.
     * Here a request whose client id has no account uses the account of another client id the same process has used,
     * and the token is asked for as the request's client through that one (a brokered refresh, as for a nested app). */
    if (!account && !child && (account = account_of_used_client( provider, req, hint, &other )))
    {
        if ((other_redirect = oauth_redirect_uri( client )))
        {
            parent = other;
            child = client;
            child_redirect = other_redirect;
        }
        else account = NULL;
    }
    if (!account)
    {
        cache_unlock();
        TRACE( "silent: no account (client %s, account id %s, hint %s)\n", debugstr_w( client ),
               debugstr_w( req->field[TOKENBROKER_FIELD_ACCOUNT_ID] ), hint && *hint ? "given" : "none" );
        interaction_required( req, reply );
        goto done;
    }
    if ((token = cache_find_token( provider->id, authority, account->id, child ? child : client, scope,
                                   unix_time() + TOKEN_MARGIN )))
    {
        TRACE( "silent: from the cache\n" );
        reply_success( reply, req, FALSE, TRUE, token->token, account->id, account->user_name, token->id_token,
                       token->client_info, token->granted_scope, token->expires_on );
        cache_unlock();
        goto done;
    }
    account_id = strdupW( account->id );
    user_name = strdupW( account->user_name );
    refresh = strdupW( account->refresh_token );
    /* PARENT may point into the used-clients table: copied while the lock is held */
    if (child)
    {
        broker_client = strdupW( parent );
        broker_redirect = oauth_redirect_uri( parent );
    }
    cache_unlock();

    if (!refresh || !*refresh)
    {
        interaction_required( req, reply );
        goto done;
    }
    if (child)
    {
        if (!broker_client || !broker_redirect)
        {
            reply_error( reply, STATUS_PROVIDER_ERROR, E_OUTOFMEMORY, L"Out of memory." );
            goto done;
        }
        broker.client_id = broker_client;
        broker.redirect_uri = broker_redirect;
        broker.child_redirect_uri = child_redirect;
    }
    ok = oauth_refresh( provider->base, authority, child ? child : client, scope, refresh, child ? &broker : NULL, &r );
    if (!ok)
        map_error( req, L"network", L"The authority could not be reached.", reply );
    else if (r.access_token)
    {
        TRACE( "silent: by refresh token%s\n", child ? " (through another client)" : "" );
        if (!r.refresh_token) r.refresh_token = strdupW( refresh );
        store_result( provider, req, &r, account_id, user_name, child );
        reply_success( reply, req, FALSE, FALSE, r.access_token, account_id, user_name, r.id_token, r.client_info,
                       r.scope, r.expires_on );
    }
    else
    {
        WARN( "silent: refresh refused (%s)%s for scope %s\n", debugstr_w( r.error_code ),
              child ? " (through another client)" : "", debugstr_w( scope ) );
        /* the refresh token is no good any more: the account needs the user again (a refusal for another client
         * through it says nothing about the refresh token itself) */
        if (!child && r.error_code && !wcscmp( r.error_code, L"invalid_grant" ))
        {
            cache_lock();
            cache_remove_account( provider->id, account_id, client );
            cache_unlock();
            cache_save();
        }
        map_error( req, r.error_code, r.error, reply );
    }
    if (ok) token_result_free( &r );

done:
    wipe_free( refresh );
    free( broker_client );
    free( broker_redirect );
    free( other_redirect );
    free( account_id );
    free( user_name );
}

/* one interactive request per calling window at a time */
static CRITICAL_SECTION windows_cs;
static CRITICAL_SECTION_DEBUG windows_cs_debug =
{
    0, 0, &windows_cs,
    { &windows_cs_debug.ProcessLocksList, &windows_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": windows_cs") }
};
static CRITICAL_SECTION windows_cs = { &windows_cs_debug, -1, 0, 0, 0, 0 };
static unsigned int busy_windows[16];

static BOOL claim_window( unsigned int window, BOOL claim )
{
    unsigned int i, free_slot = ARRAY_SIZE(busy_windows);
    BOOL ret = TRUE;

    if (!window) return TRUE;
    EnterCriticalSection( &windows_cs );
    for (i = 0; i < ARRAY_SIZE(busy_windows); i++)
    {
        if (busy_windows[i] == window) break;
        if (!busy_windows[i] && free_slot == ARRAY_SIZE(busy_windows)) free_slot = i;
    }
    if (claim)
    {
        if (i < ARRAY_SIZE(busy_windows) || free_slot == ARRAY_SIZE(busy_windows)) ret = FALSE;
        else busy_windows[free_slot] = window;
    }
    else if (i < ARRAY_SIZE(busy_windows)) busy_windows[i] = 0;
    LeaveCriticalSection( &windows_cs );
    return ret;
}

/* the sign-in, from the authorize URL to the redeemed code; the account is stored when it succeeds */
static void sign_in( const struct provider *provider, const struct request *req, struct signin *signin,
                     struct token_result *r, WCHAR **account_id, WCHAR **user_name )
{
    if (provider->test && !provider->test_window) signin_run_test( signin );
    else signin_run_webview( signin );
    if (signin->state != SIGNIN_CODE_RECEIVED) return;

    if (!oauth_redeem( provider->base, req, signin->flow, signin->code, r ))
        signin_fail( signin, SIGNIN_FAILED, L"network", L"The authority could not be reached." );
    else if (!r->access_token)
        signin_fail( signin, SIGNIN_FAILED, r->error_code, r->error );
    else if (signin_transition( signin, SIGNIN_REDEEMED ))
    {
        account_names( r, request_login_hint( req ), account_id, user_name );
        if (!*account_id || !**account_id)
            signin_fail( signin, SIGNIN_FAILED, L"invalid_response", L"The sign-in answer names no account." );
        else
        {
            store_result( provider, req, r, *account_id, *user_name, NULL );
            signin_transition( signin, SIGNIN_DONE );
        }
    }
}

static void request_token( const struct provider *provider, const struct request *req, struct reply *reply )
{
    WCHAR *redirect, *account_id = NULL, *user_name = NULL;
    struct signin signin = {0};
    struct oauth_flow flow;
    struct token_result r = {0};
    BOOL begun;

    if (!*req->field[TOKENBROKER_FIELD_CLIENT_ID])
    {
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"The request has no client id." );
        return;
    }
    if (!claim_window( req->window, TRUE ))
    {
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"A sign-in is already open for this window." );
        return;
    }
    redirect = oauth_redirect_uri( req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    begun = redirect && oauth_begin( provider->base, req, redirect, &flow );
    free( redirect );
    if (!begun)
    {
        claim_window( req->window, FALSE );
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"The sign-in could not be prepared." );
        return;
    }

    signin.state = SIGNIN_CREATED;
    signin.flow = &flow;
    signin.owner = ULongToHandle( req->window );
    signin.program_path = req->program_path;
    if (req->program_path)
    {
        const WCHAR *name = wcsrchr( req->program_path, '\\' );
        signin.program_name = name ? name + 1 : req->program_path;
    }
    sign_in( provider, req, &signin, &r, &account_id, &user_name );

    switch (signin.state)
    {
    case SIGNIN_DONE:
        TRACE( "interactive: signed in\n" );
        reply_success( reply, req, TRUE, FALSE, r.access_token, account_id, user_name, r.id_token, r.client_info,
                       r.scope, r.expires_on );
        break;
    case SIGNIN_CANCELLED:
        reply_error( reply, STATUS_USER_CANCEL, E_CANCELLED, signin.error ? signin.error : L"The user cancelled the sign-in." );
        break;
    default:
        TRACE( "interactive: failed (%s)\n", debugstr_w( signin.error_code ) );
        map_error( req, signin.error_code, signin.error, reply );
        /* an interactive request that the server wants the user for again is still an error to the program */
        if (reply->status == STATUS_USER_INTERACTION_REQUIRED) reply->status = STATUS_PROVIDER_ERROR;
        break;
    }

    claim_window( req->window, FALSE );
    token_result_free( &r );
    oauth_flow_free( &flow );
    wipe_free( signin.code );
    free( signin.error );
    free( signin.error_code );
    free( account_id );
    free( user_name );
}

static void find_accounts( const struct provider *provider, const struct request *req, struct reply *reply )
{
    struct account **list;
    unsigned int count, i;

    cache_lock();
    count = cache_list_accounts( provider->id, req->field[TOKENBROKER_FIELD_AUTHORITY],
                                 req->field[TOKENBROKER_FIELD_CLIENT_ID], &list );
    for (i = 0; i < count; i++)
        text_append_pair( &reply->field[TOKENBROKER_REPLY_ACCOUNTS], list[i]->id, list[i]->user_name );
    cache_unlock();
    free( list );
    reply->status = FIND_SUCCESS;
}

/* by id, for the request's client; with no client id, for the clients the calling process has used */
static void find_account( const struct provider *provider, const struct request *req, struct reply *reply )
{
    const WCHAR *authority = req->field[TOKENBROKER_FIELD_AUTHORITY], *id = req->field[TOKENBROKER_FIELD_ACCOUNT_ID];
    struct account *account = NULL;
    struct used_clients *entry;
    unsigned int j;

    cache_lock();
    if (*req->field[TOKENBROKER_FIELD_CLIENT_ID] || !req->client_pid)
        account = cache_find_account( provider->id, authority, id, NULL, req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    else if ((entry = used_clients( req->client_pid )))
    {
        for (j = 0; j < entry->count && !account; j++)
            account = cache_find_account( provider->id, authority, id, NULL, entry->client[j] );
    }
    if (account)
    {
        reply_set( reply, TOKENBROKER_REPLY_ACCOUNT_ID, account->id );
        reply_set( reply, TOKENBROKER_REPLY_USER_NAME, account->user_name );
        reply->status = TOKENBROKER_ACCOUNT_FOUND;
    }
    else reply->status = TOKENBROKER_ACCOUNT_NONE;
    cache_unlock();
}

static void sign_out( const struct provider *provider, const struct request *req, struct reply *reply )
{
    /* only one client's tokens, never every client's */
    if (!*req->field[TOKENBROKER_FIELD_CLIENT_ID])
    {
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"A sign-out names a client id." );
        return;
    }
    cache_lock();
    cache_remove_account( provider->id, req->field[TOKENBROKER_FIELD_ACCOUNT_ID], req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    cache_unlock();
    cache_save();
    reply->status = STATUS_SUCCESS_;
}

/* the registry: the request's provider, then its operation */
void provider_answer( const struct request *req, struct reply *reply )
{
    struct provider provider;

    cache_lock();
    remember_client( req->client_pid, req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    cache_unlock();
    if (!find_provider( req, &provider ))
    {
        TRACE( "no provider for %s\n", debugstr_w( req->field[TOKENBROKER_FIELD_PROVIDER_ID] ) );
        if (req->kind == TOKENBROKER_FIND_ACCOUNTS)
            reply_error( reply, FIND_NOT_SUPPORTED, E_NOT_FOUND, L"No account provider is available." );
        else if (req->kind == TOKENBROKER_FIND_ACCOUNT) reply->status = TOKENBROKER_ACCOUNT_NONE;
        else reply_error( reply, STATUS_PROVIDER_NOT_AVAILABLE, E_NOT_FOUND, L"No account provider is available." );
        return;
    }
    if (req->kind != TOKENBROKER_FIND_ACCOUNT && req->kind != TOKENBROKER_SIGN_OUT &&
        !valid_authority( req->field[TOKENBROKER_FIELD_AUTHORITY] ))
    {
        WARN( "authority %s refused\n", debugstr_w( req->field[TOKENBROKER_FIELD_AUTHORITY] ) );
        reply_failure( reply, req->kind, E_PROVIDER_FAILED, L"The authority is not one this provider knows." );
        return;
    }
    switch (req->kind)
    {
    case TOKENBROKER_GET_TOKEN_SILENT: get_token_silent( &provider, req, reply ); break;
    case TOKENBROKER_REQUEST_TOKEN:    request_token( &provider, req, reply ); break;
    case TOKENBROKER_FIND_ACCOUNTS:    find_accounts( &provider, req, reply ); break;
    case TOKENBROKER_FIND_ACCOUNT:     find_account( &provider, req, reply ); break;
    case TOKENBROKER_SIGN_OUT:         sign_out( &provider, req, reply ); break;
    default:
        reply_error( reply, STATUS_PROVIDER_ERROR, E_NOTIMPL, L"The provider does not support this operation." );
        break;
    }
}
