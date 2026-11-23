/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The sign-in protocol: OAuth 2.0 authorization code flow with PKCE (RFC 6749, RFC 7636) and refresh tokens, against
 * an authority's v2.0 endpoints ({base}{authority}/oauth2/v2.0/authorize and /token), written from the public
 * Microsoft identity platform documentation. The request's scope plus openid, profile and offline_access; state and
 * nonce are random; client_info=1 asks for the account's identifiers as MSAL does. Codes, verifiers and tokens are
 * never logged.
 */

#include "broker.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

/* the request's scope with openid, profile and offline_access added once each (allocated) */
WCHAR *oauth_scope( const WCHAR *requested )
{
    static const WCHAR *extra[] = { L"openid", L"profile", L"offline_access" };
    struct text out = {0};
    unsigned int i;

    if (!text_append_str( &out, requested ? requested : L"" )) return NULL;
    for (i = 0; i < ARRAY_SIZE(extra); i++)
    {
        if (scope_has( out.data, extra[i] )) continue;
        if ((out.len && !text_append( &out, L" ", 1 )) || !text_append_str( &out, extra[i] ))
        {
            text_free( &out );
            return NULL;
        }
    }
    return out.data;
}

/* the broker's redirect URI for a client id (the owner's decision of 2026-11-16 in docs/WAM_PLAN.md), allocated */
WCHAR *oauth_redirect_uri( const WCHAR *client_id )
{
    static const WCHAR prefix[] = L"ms-appx-web://Microsoft.AAD.BrokerPlugin/";
    size_t len = ARRAY_SIZE(prefix) + wcslen( client_id );
    WCHAR *ret = malloc( len * sizeof(WCHAR) );

    if (ret) swprintf( ret, len, L"%s%s", prefix, client_id );
    return ret;
}

static WCHAR *random_string( unsigned int bytes )
{
    BYTE buffer[64];
    char *b64;
    WCHAR *ret;

    if (bytes > sizeof(buffer) || !random_bytes( buffer, bytes )) return NULL;
    b64 = base64url_encode( buffer, bytes );
    SecureZeroMemory( buffer, sizeof(buffer) );
    if (!b64) return NULL;
    ret = strdupW_utf8( b64, -1 );
    SecureZeroMemory( b64, strlen( b64 ) );
    free( b64 );
    return ret;
}

static WCHAR *endpoint( const WCHAR *base, const WCHAR *authority, const WCHAR *name )
{
    struct text url = {0};

    if (!text_append_str( &url, base ) || !text_append_str( &url, authority && *authority ? authority : L"common" ) ||
        !text_append_str( &url, L"/oauth2/v2.0/" ) || !text_append_str( &url, name ))
    {
        text_free( &url );
        return NULL;
    }
    return url.data;
}

/* the authorize URL of an interactive request, with a new verifier, state and nonce */
BOOL oauth_begin( const WCHAR *base, const struct request *req, const WCHAR *redirect_uri, struct oauth_flow *flow )
{
    const WCHAR *hint = request_login_hint( req );
    WCHAR *nonce = NULL, *challenge = NULL;
    struct text url = {0};
    BYTE hash[32];
    char *verifier_utf8 = NULL, *b64 = NULL;
    BOOL ret = FALSE;

    memset( flow, 0, sizeof(*flow) );
    if (!(flow->redirect_uri = strdupW( redirect_uri )) || !(flow->state = random_string( 24 )) ||
        !(nonce = random_string( 24 )) || !(flow->verifier = random_string( 48 )) ||
        !(flow->scope = oauth_scope( req->field[TOKENBROKER_FIELD_SCOPE] )))
        goto done;
    /* S256: base64url(SHA-256(verifier)) */
    if (!(verifier_utf8 = strdup_utf8( flow->verifier )) ||
        !sha256( (BYTE *)verifier_utf8, strlen( verifier_utf8 ), hash ) || !(b64 = base64url_encode( hash, 32 )) ||
        !(challenge = strdupW_utf8( b64, -1 )))
        goto done;
    if (!(url.data = endpoint( base, req->field[TOKENBROKER_FIELD_AUTHORITY], L"authorize" ))) goto done;
    url.len = url.size = wcslen( url.data );
    if (!url_append_param( &url, L"client_id", req->field[TOKENBROKER_FIELD_CLIENT_ID] ) ||
        !url_append_param( &url, L"response_type", L"code" ) ||
        !url_append_param( &url, L"redirect_uri", redirect_uri ) ||
        !url_append_param( &url, L"response_mode", L"query" ) ||
        !url_append_param( &url, L"scope", flow->scope ) ||
        !url_append_param( &url, L"state", flow->state ) ||
        !url_append_param( &url, L"nonce", nonce ) ||
        !url_append_param( &url, L"code_challenge", challenge ) ||
        !url_append_param( &url, L"code_challenge_method", L"S256" ) ||
        !url_append_param( &url, L"client_info", L"1" ))
        goto done;
    if (hint && *hint && !url_append_param( &url, L"login_hint", hint )) goto done;
    if (req->prompt == PROMPT_FORCE_AUTHENTICATION && !url_append_param( &url, L"prompt", L"login" )) goto done;
    flow->authorize_url = url.data;
    url.data = NULL;
    ret = TRUE;

done:
    if (verifier_utf8) SecureZeroMemory( verifier_utf8, strlen( verifier_utf8 ) );
    free( verifier_utf8 );
    free( b64 );
    free( challenge );
    free( nonce );
    text_free( &url );
    if (!ret) oauth_flow_free( flow );
    return ret;
}

void oauth_flow_free( struct oauth_flow *flow )
{
    free( flow->authorize_url );
    free( flow->redirect_uri );
    free( flow->state );
    wipe_free( flow->verifier );
    free( flow->scope );
    memset( flow, 0, sizeof(*flow) );
}

void token_result_free( struct token_result *r )
{
    wipe_free( r->access_token );
    wipe_free( r->refresh_token );
    free( r->id_token );
    free( r->client_info );
    free( r->scope );
    free( r->error );
    free( r->error_code );
    memset( r, 0, sizeof(*r) );
}

/* POST a form to the token endpoint and read its answer; FALSE only when the server was not reached or a part of the
 * form could not be built (BUILT FALSE). The form holds secrets: it is wiped and freed here. */
static BOOL token_post( const WCHAR *base, const WCHAR *authority, struct text *form, BOOL built,
                        struct token_result *r )
{
    struct http_response response;
    WCHAR *url = NULL, *description;
    char *body = NULL;
    LONGLONG expires_in = 3600;
    BOOL ret;

    memset( r, 0, sizeof(*r) );
    if (built) body = strdup_utf8( form->data ? form->data : L"" );
    text_wipe( form );
    if (!body || !(url = endpoint( base, authority, L"token" )))
    {
        if (body) SecureZeroMemory( body, strlen( body ) );
        free( body );
        return FALSE;
    }
    ret = http_request( url, body, &response );
    SecureZeroMemory( body, strlen( body ) );
    free( body );
    free( url );
    if (!ret) return FALSE;

    if (response.status == 200 && (r->access_token = json_string( response.body, "access_token" )))
    {
        r->refresh_token = json_string( response.body, "refresh_token" );
        r->id_token = json_string( response.body, "id_token" );
        r->client_info = json_string( response.body, "client_info" );
        r->scope = json_string( response.body, "scope" );
        /* a lifetime outside a second to a day is not believed: the usual hour instead */
        if (!json_number( response.body, "expires_in", &expires_in ) || expires_in < 1 || expires_in > 86400)
        {
            WARN( "the token's lifetime is missing or out of range\n" );
            expires_in = 3600;
        }
        r->expires_on = unix_time() + expires_in;
        TRACE( "token endpoint: access token of %u characters, expires in %d s, refresh token %s\n",
               (unsigned int)wcslen( r->access_token ), (int)expires_in, r->refresh_token ? "given" : "not given" );
    }
    else
    {
        struct text error = {0};
        r->error_code = json_string( response.body, "error" );
        description = json_string( response.body, "error_description" );
        text_append_str( &error, r->error_code ? r->error_code : L"server_error" );
        if (description)
        {
            text_append_str( &error, L": " );
            text_append_str( &error, description );
        }
        if (!r->error_code)
        {
            WCHAR status[32];
            swprintf( status, ARRAY_SIZE(status), L" (HTTP %lu)", response.status );
            text_append_str( &error, status );
        }
        r->error = error.data;
        free( description );
        WARN( "token endpoint refused: HTTP %lu, %s\n", response.status, debugstr_w( r->error_code ) );
    }
    if (response.body) SecureZeroMemory( response.body, strlen( response.body ) );
    http_response_free( &response );
    return TRUE;
}

/* redeems the authorization code with the verifier */
BOOL oauth_redeem( const WCHAR *base, const struct request *req, const struct oauth_flow *flow, const WCHAR *code,
                   struct token_result *result )
{
    struct text form = {0};
    BOOL built;

    built = url_append_param( &form, L"client_id", req->field[TOKENBROKER_FIELD_CLIENT_ID] ) &&
            url_append_param( &form, L"grant_type", L"authorization_code" ) &&
            url_append_param( &form, L"code", code ) &&
            url_append_param( &form, L"redirect_uri", flow->redirect_uri ) &&
            url_append_param( &form, L"code_verifier", flow->verifier ) &&
            url_append_param( &form, L"scope", flow->scope ) &&
            url_append_param( &form, L"client_info", L"1" );
    return token_post( base, req->field[TOKENBROKER_FIELD_AUTHORITY], &form, built, result );
}

/* A new access token for CLIENT_ID from a refresh token. With BROKER, the refresh token is the broker client's and the
 * token is for CLIENT_ID through it (Nested App Authentication, or another client id of the same program): the token
 * endpoint is asked as CLIENT_ID with its redirect URI, the broker client named as brk_client_id and
 * brk_redirect_uri, as MSAL's brokered requests do. The broker client's refresh token stays the account's; a refresh
 * token in such an answer is not kept by the caller. */
BOOL oauth_refresh( const WCHAR *base, const WCHAR *authority, const WCHAR *client_id, const WCHAR *scope,
                    const WCHAR *refresh_token, const struct oauth_broker *broker, struct token_result *result )
{
    WCHAR *full_scope = oauth_scope( scope );
    struct text form = {0};
    BOOL built, ret;

    built = full_scope &&
            url_append_param( &form, L"client_id", client_id ) &&
            url_append_param( &form, L"grant_type", L"refresh_token" ) &&
            url_append_param( &form, L"refresh_token", refresh_token ) &&
            url_append_param( &form, L"scope", full_scope ) &&
            (!broker || (url_append_param( &form, L"redirect_uri", broker->child_redirect_uri ) &&
                         url_append_param( &form, L"brk_client_id", broker->client_id ) &&
                         url_append_param( &form, L"brk_redirect_uri", broker->redirect_uri ))) &&
            url_append_param( &form, L"client_info", L"1" );
    ret = token_post( base, authority, &form, built, result );
    free( full_scope );
    return ret;
}
