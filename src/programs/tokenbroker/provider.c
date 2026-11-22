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
 *    refused). Its sign-in is the test hook (signin_run_test). The Microsoft provider has no test switch.
 *
 * Both use the same OAuth code (oauth.c) and redirect URI, the broker's (ms-appx-web://Microsoft.AAD.BrokerPlugin/
 * {client id}, the owner's decision of 2026-11-16 in docs/WAM_PLAN.md). Statuses and error codes come from one table
 * (map_error); anything unmapped is ProviderError with the server's text.
 */

#include "broker.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

#define MSA_PROVIDER_ID     L"https://login.microsoft.com"
#define MSA_AUTHORITY_BASE  L"https://login.microsoftonline.com/"
#define TEST_PROVIDER_ID    L"https://login.wine.test"
#define REDIRECT_PREFIX     L"ms-appx-web://Microsoft.AAD.BrokerPlugin/"

/* leave a cached access token alone when it expires within this time */
#define TOKEN_MARGIN        300

#define E_PROVIDER_FAILED   0x80004005  /* E_FAIL */
#define E_NOT_FOUND         0x80070490  /* HRESULT_FROM_WIN32(ERROR_NOT_FOUND) */
#define E_CANCELLED         0x800704c7  /* HRESULT_FROM_WIN32(ERROR_CANCELLED) */
#define E_NO_NETWORK        0x80072efd  /* HRESULT_FROM_WIN32(ERROR_WINHTTP_CANNOT_CONNECT) */

struct provider
{
    const WCHAR *id;
    BOOL test;
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

    if (RegGetValueW( HKEY_CURRENT_USER, L"Software\\Wine\\WebAuthentication", L"TestAuthority", RRF_RT_REG_SZ,
                      NULL, base, &bytes ))
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
        provider->id = TEST_PROVIDER_ID;
        provider->test = TRUE;
        return TRUE;
    }
    return FALSE;
}

/*
 * The status table (pattern 7)
 */

/* the error of a request that needs the user, as Windows 11's Microsoft account provider gives it (Wine's diagnostic
 * provider has the same): 0x80860006 "Error" for consumers, 0xcaa10001 otherwise */
static void interaction_required( const struct request *req, struct reply *reply )
{
    if (!wcscmp( req->field[TOKENBROKER_FIELD_AUTHORITY], L"consumers" ))
        reply_error( reply, STATUS_USER_INTERACTION_REQUIRED, 0x80860006, L"Error" );
    else
        reply_error( reply, STATUS_USER_INTERACTION_REQUIRED, 0xcaa10001, L"Need user interaction to continue." );
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

/*
 * Accounts from a token answer
 */

/* the account's id and sign-in name: the id token's object id (else its subject), its preferred_username (else email,
 * else the login hint) */
static void account_names( const struct token_result *r, const WCHAR *hint, WCHAR **id, WCHAR **name )
{
    char *claims = jwt_payload( r->id_token );

    *id = json_string( claims, "oid" );
    if (!*id) *id = json_string( claims, "sub" );
    *name = json_string( claims, "preferred_username" );
    if (!*name) *name = json_string( claims, "email" );
    if (!*name) *name = strdupW( hint ? hint : L"" );
    free( claims );
}

static void reply_success( struct reply *reply, const WCHAR *token, const WCHAR *account_id, const WCHAR *user_name,
                           const WCHAR *id_token, const WCHAR *client_info, const WCHAR *scope, LONGLONG expires_on )
{
    struct text *props = &reply->field[TOKENBROKER_REPLY_PROPERTIES];
    WCHAR expires[32];

    reply->status = STATUS_SUCCESS_;
    reply->code = 0;
    reply_set( reply, TOKENBROKER_REPLY_TOKEN, token );
    reply_set( reply, TOKENBROKER_REPLY_ACCOUNT_ID, account_id );
    reply_set( reply, TOKENBROKER_REPLY_USER_NAME, user_name );
    swprintf( expires, ARRAY_SIZE(expires), L"%I64d", expires_on );
    /* the properties MSAL reads from a WAM answer */
    if (id_token && *id_token) text_append_pair( props, L"wamcompat_id_token", id_token );
    if (client_info && *client_info) text_append_pair( props, L"wamcompat_client_info", client_info );
    if (scope && *scope) text_append_pair( props, L"wamcompat_scopes", scope );
    text_append_pair( props, L"wamcompat_expires_on", expires );
    if (user_name && *user_name) text_append_pair( props, L"wamcompat_user_displayable_id", user_name );
    if (account_id && *account_id) text_append_pair( props, L"wamcompat_account_id", account_id );
}

/* keeps a token answer: the account with its refresh token, and the access token */
static void store_result( const struct provider *provider, const struct request *req, const struct token_result *r,
                          const WCHAR *account_id, const WCHAR *user_name )
{
    struct access_token t = {0};
    struct account a = {0};

    cache_lock();
    if (r->refresh_token)
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
    t.client_id = req->field[TOKENBROKER_FIELD_CLIENT_ID];
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
 * The operations
 */

static void get_token_silent( const struct provider *provider, const struct request *req, struct reply *reply )
{
    const WCHAR *hint = request_property( req, L"LoginHint" );
    WCHAR *account_id = NULL, *user_name = NULL, *refresh = NULL;
    struct account *account, **list;
    struct access_token *token;
    struct token_result r;
    unsigned int count;

    if (!hint) hint = request_property( req, L"login_hint" );
    cache_lock();
    account = cache_find_account( provider->id, req->field[TOKENBROKER_FIELD_AUTHORITY],
                                  req->field[TOKENBROKER_FIELD_ACCOUNT_ID], hint,
                                  req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    /* no account named: the only one the client has, if it has exactly one */
    if (!account && !*req->field[TOKENBROKER_FIELD_ACCOUNT_ID] && (!hint || !*hint))
    {
        count = cache_list_accounts( provider->id, req->field[TOKENBROKER_FIELD_AUTHORITY],
                                     req->field[TOKENBROKER_FIELD_CLIENT_ID], &list );
        if (count == 1) account = list[0];
        free( list );
    }
    if (!account)
    {
        cache_unlock();
        TRACE( "silent: no account\n" );
        interaction_required( req, reply );
        return;
    }
    if ((token = cache_find_token( provider->id, req->field[TOKENBROKER_FIELD_AUTHORITY], account->id,
                                   req->field[TOKENBROKER_FIELD_CLIENT_ID], req->field[TOKENBROKER_FIELD_SCOPE],
                                   unix_time() + TOKEN_MARGIN )))
    {
        TRACE( "silent: from the cache\n" );
        reply_success( reply, token->token, account->id, account->user_name, token->id_token, token->client_info,
                       token->granted_scope, token->expires_on );
        cache_unlock();
        return;
    }
    account_id = strdupW( account->id );
    user_name = strdupW( account->user_name );
    refresh = strdupW( account->refresh_token );
    cache_unlock();

    if (!refresh || !*refresh) interaction_required( req, reply );
    else if (!oauth_refresh( provider->base, req->field[TOKENBROKER_FIELD_AUTHORITY],
                             req->field[TOKENBROKER_FIELD_CLIENT_ID], req->field[TOKENBROKER_FIELD_SCOPE], refresh, &r ))
        map_error( req, L"network", L"The authority could not be reached.", reply );
    else
    {
        if (r.access_token)
        {
            TRACE( "silent: by refresh token\n" );
            if (!r.refresh_token) r.refresh_token = strdupW( refresh );
            store_result( provider, req, &r, account_id, user_name );
            reply_success( reply, r.access_token, account_id, user_name, r.id_token, r.client_info, r.scope,
                           r.expires_on );
        }
        else
        {
            TRACE( "silent: refresh refused (%s)\n", debugstr_w( r.error_code ) );
            if (r.error_code && !wcscmp( r.error_code, L"invalid_grant" ))
            {
                /* the refresh token is no good any more: the account needs the user again */
                cache_lock();
                cache_remove_account( provider->id, account_id, req->field[TOKENBROKER_FIELD_CLIENT_ID] );
                cache_unlock();
                cache_save();
            }
            map_error( req, r.error_code, r.error, reply );
        }
        token_result_free( &r );
    }
    if (refresh) SecureZeroMemory( refresh, wcslen( refresh ) * sizeof(WCHAR) );
    free( refresh );
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

static void request_token( const struct provider *provider, const struct request *req, struct reply *reply )
{
    const WCHAR *hint = request_property( req, L"LoginHint" );
    WCHAR *redirect, *account_id = NULL, *user_name = NULL;
    struct signin signin = {0};
    struct oauth_flow flow;
    struct token_result r = {0};
    size_t len;

    if (!hint) hint = request_property( req, L"login_hint" );
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
    len = wcslen( REDIRECT_PREFIX ) + wcslen( req->field[TOKENBROKER_FIELD_CLIENT_ID] ) + 1;
    if (!(redirect = malloc( len * sizeof(WCHAR) )))
    {
        claim_window( req->window, FALSE );
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"Out of memory." );
        return;
    }
    swprintf( redirect, len, L"%s%s", REDIRECT_PREFIX, req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    if (!oauth_begin( provider->base, req, redirect, &flow ))
    {
        free( redirect );
        claim_window( req->window, FALSE );
        reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"The sign-in could not be prepared." );
        return;
    }
    free( redirect );

    signin.state = SIGNIN_CREATED;
    signin.flow = &flow;
    signin.owner = ULongToHandle( req->window );
    signin.program_path = req->program_path;
    if (req->program_path)
    {
        const WCHAR *name = wcsrchr( req->program_path, '\\' );
        signin.program_name = name ? name + 1 : req->program_path;
    }
    if (provider->test) signin_run_test( &signin );
    else signin_run_webview( &signin );

    if (signin.state == SIGNIN_CODE_RECEIVED)
    {
        if (!oauth_redeem( provider->base, req, &flow, signin.code, &r ))
        {
            signin_transition( &signin, SIGNIN_FAILED );
            signin.error_code = strdupW( L"network" );
            signin.error = strdupW( L"The authority could not be reached." );
        }
        else if (!r.access_token)
        {
            signin_transition( &signin, SIGNIN_FAILED );
            signin.error_code = strdupW( r.error_code );
            signin.error = strdupW( r.error );
        }
        else if (signin_transition( &signin, SIGNIN_REDEEMED ))
        {
            account_names( &r, hint, &account_id, &user_name );
            if (!account_id || !*account_id)
            {
                signin_transition( &signin, SIGNIN_FAILED );
                signin.error_code = strdupW( L"invalid_response" );
                signin.error = strdupW( L"The sign-in answer names no account." );
            }
            else
            {
                store_result( provider, req, &r, account_id, user_name );
                signin_transition( &signin, SIGNIN_DONE );
            }
        }
    }
    switch (signin.state)
    {
    case SIGNIN_DONE:
        TRACE( "interactive: signed in\n" );
        reply_success( reply, r.access_token, account_id, user_name, r.id_token, r.client_info, r.scope, r.expires_on );
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
    if (signin.code) SecureZeroMemory( signin.code, wcslen( signin.code ) * sizeof(WCHAR) );
    free( signin.code );
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

static void find_account( const struct provider *provider, const struct request *req, struct reply *reply )
{
    struct account *account;

    cache_lock();
    if ((account = cache_find_account( provider->id, req->field[TOKENBROKER_FIELD_AUTHORITY],
                                       req->field[TOKENBROKER_FIELD_ACCOUNT_ID], NULL, NULL )))
    {
        reply_set( reply, TOKENBROKER_REPLY_ACCOUNT_ID, account->id );
        reply_set( reply, TOKENBROKER_REPLY_USER_NAME, account->user_name );
        reply->status = 0;
    }
    else reply->status = 1;
    cache_unlock();
}

static void sign_out( const struct provider *provider, const struct request *req, struct reply *reply )
{
    cache_lock();
    cache_remove_account( provider->id, req->field[TOKENBROKER_FIELD_ACCOUNT_ID], req->field[TOKENBROKER_FIELD_CLIENT_ID] );
    cache_unlock();
    cache_save();
    reply->status = 0;
}

/* the registry: the request's provider, then its operation */
void provider_answer( const struct request *req, struct reply *reply )
{
    struct provider provider;

    if (!find_provider( req, &provider ))
    {
        TRACE( "no provider for %s\n", debugstr_w( req->field[TOKENBROKER_FIELD_PROVIDER_ID] ) );
        if (req->kind == TOKENBROKER_FIND_ACCOUNTS)
            reply_error( reply, FIND_NOT_SUPPORTED, E_NOT_FOUND, L"No account provider is available." );
        else if (req->kind == TOKENBROKER_FIND_ACCOUNT) reply->status = 1;
        else reply_error( reply, STATUS_PROVIDER_NOT_AVAILABLE, E_NOT_FOUND, L"No account provider is available." );
        return;
    }
    if (req->kind != TOKENBROKER_FIND_ACCOUNT && req->kind != TOKENBROKER_SIGN_OUT &&
        !valid_authority( req->field[TOKENBROKER_FIELD_AUTHORITY] ))
    {
        WARN( "authority %s refused\n", debugstr_w( req->field[TOKENBROKER_FIELD_AUTHORITY] ) );
        if (req->kind == TOKENBROKER_FIND_ACCOUNTS)
            reply_error( reply, FIND_PROVIDER_ERROR, E_PROVIDER_FAILED, L"The authority is not one this provider knows." );
        else reply_error( reply, STATUS_PROVIDER_ERROR, E_PROVIDER_FAILED, L"The authority is not one this provider knows." );
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
        reply_error( reply, STATUS_PROVIDER_ERROR, 0x80004001, L"The provider does not support this operation." );
        break;
    }
}
