/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The interactive sign-in as an explicit state machine (docs/WAM_BROKER_PLAN.md, pattern 4):
 *
 *   created -> window_open -> code_received -> redeemed -> done
 *
 * with cancelled and failed reachable from every state that is not final. A surface shows the authorize URL and
 * reports each navigation to signin_redirect(); the one to the redirect URI is caught there and never loaded, and the
 * code is taken from it only with the state the broker sent. Surfaces: the test hook (signin_run_test, for the test
 * provider only: it follows the test authority's redirects over HTTP, as a window would, with no user) and the
 * WebView2 window (webview.c).
 */

#include "broker.h"

#include <winhttp.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

static const char *state_name( enum signin_state state )
{
    static const char *names[] = { "created", "window_open", "code_received", "redeemed", "done", "cancelled", "failed" };
    return state < ARRAY_SIZE(names) ? names[state] : "?";
}

/* a transition the machine allows; any other is refused (and logged) */
BOOL signin_transition( struct signin *signin, enum signin_state to )
{
    enum signin_state from = signin->state;
    BOOL ok;

    switch (from)
    {
    case SIGNIN_CREATED:       ok = to == SIGNIN_WINDOW_OPEN; break;
    case SIGNIN_WINDOW_OPEN:   ok = to == SIGNIN_CODE_RECEIVED; break;
    case SIGNIN_CODE_RECEIVED: ok = to == SIGNIN_REDEEMED; break;
    case SIGNIN_REDEEMED:      ok = to == SIGNIN_DONE; break;
    default:                   ok = FALSE; break;  /* done, cancelled, failed are final */
    }
    if (from < SIGNIN_DONE && (to == SIGNIN_CANCELLED || to == SIGNIN_FAILED)) ok = TRUE;
    if (!ok)
    {
        WARN( "sign-in: no transition from %s to %s\n", state_name( from ), state_name( to ) );
        return FALSE;
    }
    TRACE( "sign-in: %s -> %s\n", state_name( from ), state_name( to ) );
    signin->state = to;
    return TRUE;
}

/* to cancelled or failed, with the error */
void signin_fail( struct signin *signin, enum signin_state to, const WCHAR *code, const WCHAR *message )
{
    if (!signin_transition( signin, to )) return;
    free( signin->error );
    free( signin->error_code );
    signin->error = strdupW( message );
    signin->error_code = strdupW( code );
}

/* the length of a URL's scheme, host and path (up to its query or fragment) */
static size_t url_base_len( const WCHAR *url )
{
    return wcscspn( url, L"?#" );
}

/* Is this navigation the redirect? Then it is caught (TRUE: the surface must not load it) and the machine moves on:
 * code_received with the code, cancelled for access_denied, failed for another error or a wrong state. Only an exact
 * match of scheme, host (case-insensitive) and path counts; another host with the same path is not the redirect. */
BOOL signin_redirect( struct signin *signin, const WCHAR *url )
{
    const WCHAR *redirect = signin->flow->redirect_uri, *host_end;
    size_t len = url_base_len( url ), redirect_len = wcslen( redirect ), host_len;
    WCHAR *state, *code, *error, *description;

    if (len != redirect_len) return FALSE;
    /* scheme and host compare without case, the path exactly */
    if (!(host_end = wcsstr( redirect, L"://" ))) return FALSE;
    host_end = wcschr( host_end + 3, '/' );
    host_len = host_end ? host_end - redirect : redirect_len;
    if (wcsnicmp( url, redirect, host_len ) || wcsncmp( url + host_len, redirect + host_len, redirect_len - host_len ))
        return FALSE;
    if (signin->state != SIGNIN_WINDOW_OPEN) return TRUE;

    state = url_query_param( url, L"state" );
    code = url_query_param( url, L"code" );
    error = url_query_param( url, L"error" );
    description = url_query_param( url, L"error_description" );
    if (!state || wcscmp( state, signin->flow->state ))
    {
        WARN( "sign-in: the redirect's state is not the one sent\n" );
        signin_fail( signin, SIGNIN_FAILED, L"invalid_state", L"The sign-in answer does not belong to this request." );
    }
    else if (error)
    {
        TRACE( "sign-in: the authority answered %s\n", debugstr_w( error ) );
        signin_fail( signin, !wcscmp( error, L"access_denied" ) ? SIGNIN_CANCELLED : SIGNIN_FAILED, error,
                     description ? description : error );
    }
    else if (code && *code)
    {
        signin->code = code;
        code = NULL;
        signin_transition( signin, SIGNIN_CODE_RECEIVED );
    }
    else signin_fail( signin, SIGNIN_FAILED, L"invalid_response", L"The sign-in answer has no code." );
    free( state );
    wipe_free( code );
    free( error );
    free( description );
    return TRUE;
}

/* the length of a URL's scheme and host (its origin), 0 for no scheme */
static size_t url_origin_len( const WCHAR *url )
{
    const WCHAR *scheme_end = wcsstr( url, L"://" );

    return scheme_end ? (scheme_end + 3 - url) + wcscspn( scheme_end + 3, L"/?#" ) : 0;
}

static BOOL same_origin( const WCHAR *a, const WCHAR *b )
{
    size_t la = url_origin_len( a ), lb = url_origin_len( b );

    return la && la == lb && !wcsnicmp( a, b, la );
}

/* The test hook: the "window" is a chain of HTTP requests that follows the test authority's redirects. A page that
 * answers without a redirect is a page waiting for a user, who here closes the window: cancelled. A redirect to
 * another origin that is not the redirect URI leaves the authority: failed. */
void signin_run_test( struct signin *signin )
{
    struct http_response response;
    WCHAR *url;
    unsigned int hops;

    if (!signin_transition( signin, SIGNIN_WINDOW_OPEN )) return;
    if (!(url = strdupW( signin->flow->authorize_url ))) goto fail;
    for (hops = 0; hops < 8 && signin->state == SIGNIN_WINDOW_OPEN; hops++)
    {
        if (signin_redirect( signin, url )) break;
        if (!http_request( url, NULL, &response ))
        {
            signin_fail( signin, SIGNIN_FAILED, L"network", L"The sign-in page could not be loaded." );
            break;
        }
        if (response.status >= 300 && response.status < 400 && response.location && response.location[0] == '/')
        {
            /* a path on the same origin (RFC 9110 allows a relative Location) */
            size_t origin = url_origin_len( url );
            struct text full = {0};

            if (!origin || !text_append( &full, url, origin ) || !text_append_str( &full, response.location ))
            {
                text_free( &full );
                http_response_free( &response );
                signin_fail( signin, SIGNIN_FAILED, L"invalid_redirect", L"The sign-in left the authority." );
                break;
            }
            free( response.location );
            response.location = full.data;
        }
        if (response.status >= 300 && response.status < 400 && response.location)
        {
            if (!signin_redirect( signin, response.location ) && !same_origin( response.location, url ))
                signin_fail( signin, SIGNIN_FAILED, L"invalid_redirect", L"The sign-in left the authority." );
            free( url );
            url = response.location;
            response.location = NULL;
        }
        else
        {
            TRACE( "sign-in (test hook): a page for the user, HTTP %lu: the window is closed\n", response.status );
            signin_fail( signin, SIGNIN_CANCELLED, L"user_cancel", L"The sign-in window was closed." );
        }
        http_response_free( &response );
    }
    free( url );
    if (signin->state == SIGNIN_WINDOW_OPEN)
        signin_fail( signin, SIGNIN_FAILED, L"invalid_redirect", L"The sign-in did not finish." );
    return;
fail:
    signin_fail( signin, SIGNIN_FAILED, L"out_of_memory", L"Out of memory." );
}
