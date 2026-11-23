/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token broker's parts (docs/WAM_BROKER_PLAN.md, docs/WAM_BROKER_DESIGN.md): main.c (process, pipe, protocol),
 * provider.c (the provider interface and its registry, the sign-in state machine), oauth.c (the authorization code
 * flow with PKCE and refresh tokens, public OAuth 2.0), cache.c (accounts and tokens), util.c (strings, JSON, HTTP).
 */

#ifndef __WEX_TOKENBROKER_H
#define __WEX_TOKENBROKER_H

#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

#include "wine/tokenbroker.h"

/* WebTokenRequestStatus (STATUS_SUCCESS_: windows.h has an NTSTATUS of that name) */
#define STATUS_SUCCESS_                  0
#define STATUS_USER_CANCEL               1
#define STATUS_USER_INTERACTION_REQUIRED 3
#define STATUS_PROVIDER_NOT_AVAILABLE    4
#define STATUS_PROVIDER_ERROR            5

/* FindAllWebAccountsStatus */
#define FIND_SUCCESS                     0
#define FIND_NOT_SUPPORTED               2
#define FIND_PROVIDER_ERROR              3

/* WebTokenRequestPromptType */
#define PROMPT_DEFAULT                   0
#define PROMPT_FORCE_AUTHENTICATION      1

/* provider error codes (HRESULTs) */
#define E_PROVIDER_FAILED   E_FAIL
#define E_NOT_FOUND         HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#define E_CANCELLED         HRESULT_FROM_WIN32(ERROR_CANCELLED)
#define E_NO_NETWORK        0x80072efd  /* HRESULT_FROM_WIN32(ERROR_WINHTTP_CANNOT_CONNECT) */
#define E_TOO_LARGE         HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)

/* a request as the client sent it; the strings are allocated and NUL-terminated */
struct request
{
    unsigned int kind;
    unsigned int prompt;
    unsigned int window;
    unsigned int client_pid;    /* the calling process, from the pipe (for the client ids it has used) */
    WCHAR *field[TOKENBROKER_FIELD_COUNT];
    unsigned int prop_count;
    WCHAR **prop_key;
    WCHAR **prop_value;
    WCHAR *program_path;    /* the calling program's executable, from the pipe's client process, or NULL */
};

/* growable UTF-16 text (pairs and lists keep their NUL separators) */
struct text
{
    WCHAR *data;
    unsigned int len;       /* characters */
    unsigned int size;
};

/* the answer, built by a provider */
struct reply
{
    int status;
    unsigned int code;
    struct text field[TOKENBROKER_REPLY_COUNT];
};

/* util.c */
WCHAR *strdupW( const WCHAR *str );
WCHAR *strndupW( const WCHAR *str, unsigned int len );
char *strdup_utf8( const WCHAR *str );
WCHAR *strdupW_utf8( const char *str, int len );
BOOL text_append( struct text *text, const WCHAR *str, unsigned int len );
BOOL text_append_str( struct text *text, const WCHAR *str );
BOOL text_append_pair( struct text *text, const WCHAR *key, const WCHAR *value );
void text_free( struct text *text );
BOOL text_append_json_string( struct text *text, const WCHAR *str );
void text_wipe( struct text *text );
void wipe_free( WCHAR *str );
const WCHAR *request_property( const struct request *req, const WCHAR *key );
BOOL request_property_is( const struct request *req, const WCHAR *key, const WCHAR *value );
const WCHAR *request_login_hint( const struct request *req );
void reply_error( struct reply *reply, int status, unsigned int code, const WCHAR *message );
void reply_failure( struct reply *reply, unsigned int kind, unsigned int code, const WCHAR *message );
BOOL scope_has( const WCHAR *scope, const WCHAR *word );
SID *token_user_sid( HANDLE token );
BOOL user_only_descriptor( const WCHAR *ace_flags, const WCHAR *rights, SECURITY_ATTRIBUTES *sa );
BOOL reply_set( struct reply *reply, enum tokenbroker_reply_field field, const WCHAR *str );
BOOL url_append_param( struct text *url, const WCHAR *name, const WCHAR *value );
WCHAR *url_query_param( const WCHAR *url, const WCHAR *name );
char *base64url_encode( const BYTE *data, DWORD len );
BYTE *base64url_decode( const char *str, DWORD *len );
BOOL random_bytes( BYTE *buffer, ULONG len );
BOOL sha256( const BYTE *data, ULONG len, BYTE hash[32] );
WCHAR *json_string( const char *json, const char *key );
BOOL json_number( const char *json, const char *key, LONGLONG *value );
char *jwt_payload( const WCHAR *jwt );
LONGLONG unix_time( void );

struct http_response
{
    DWORD status;
    char *body;             /* NUL-terminated */
    WCHAR *location;        /* the Location header of a redirect, or NULL */
};
BOOL http_request( const WCHAR *url, const char *form, struct http_response *response );
void http_response_free( struct http_response *response );

/* cache.c */
struct account
{
    WCHAR *provider_id;
    WCHAR *authority;
    WCHAR *id;
    WCHAR *user_name;
    WCHAR *client_id;
    WCHAR *refresh_token;   /* the secret: never logged */
};

struct access_token
{
    WCHAR *provider_id;
    WCHAR *authority;
    WCHAR *account_id;
    WCHAR *client_id;
    WCHAR *scope;           /* as the request named it */
    LONGLONG expires_on;    /* Unix time */
    WCHAR *token;
    WCHAR *id_token;
    WCHAR *client_info;
    WCHAR *granted_scope;
};

void cache_lock( void );
void cache_unlock( void );
void cache_load( void );
BOOL cache_save( void );
struct account *cache_find_account( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *id,
                                    const WCHAR *user_name, const WCHAR *client_id );
unsigned int cache_list_accounts( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *client_id,
                                  struct account ***out );
BOOL cache_store_account( const struct account *account );
void cache_remove_account( const WCHAR *provider_id, const WCHAR *id, const WCHAR *client_id );
struct access_token *cache_find_token( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *account_id,
                                       const WCHAR *client_id, const WCHAR *scope, LONGLONG valid_until );
BOOL cache_store_token( const struct access_token *token );

/* oauth.c */
struct oauth_flow
{
    WCHAR *authorize_url;   /* with every parameter */
    WCHAR *redirect_uri;
    WCHAR *state;
    WCHAR *verifier;
    WCHAR *scope;
};

struct token_result
{
    WCHAR *access_token;
    WCHAR *refresh_token;
    WCHAR *id_token;
    WCHAR *client_info;
    WCHAR *scope;
    LONGLONG expires_on;
    WCHAR *error;           /* the server's error and description, when it refused */
    WCHAR *error_code;      /* its error code alone ("invalid_grant", ...) */
};

BOOL oauth_begin( const WCHAR *base, const struct request *req, const WCHAR *redirect_uri, struct oauth_flow *flow );
void oauth_flow_free( struct oauth_flow *flow );
BOOL oauth_redeem( const WCHAR *base, const struct request *req, const struct oauth_flow *flow, const WCHAR *code,
                   struct token_result *result );
/* a refresh for another client through the broker's own (nested apps, brokered client ids) */
struct oauth_broker
{
    const WCHAR *client_id;     /* the broker client, whose refresh token is used */
    const WCHAR *redirect_uri;  /* its redirect URI */
    const WCHAR *child_redirect_uri;
};

BOOL oauth_refresh( const WCHAR *base, const WCHAR *authority, const WCHAR *client_id, const WCHAR *scope,
                    const WCHAR *refresh_token, const struct oauth_broker *broker, struct token_result *result );
void token_result_free( struct token_result *result );
WCHAR *oauth_scope( const WCHAR *requested );
WCHAR *oauth_redirect_uri( const WCHAR *client_id );

/* answer.c: the answer's shape, as WAM gives it for a Microsoft account */
void account_names( const struct token_result *r, const WCHAR *hint, WCHAR **id, WCHAR **name );
void reply_success( struct reply *reply, const struct request *req, BOOL interactive, BOOL cached,
                    const WCHAR *token, const WCHAR *account_id, const WCHAR *user_name, const WCHAR *id_token,
                    const WCHAR *client_info, const WCHAR *scope, LONGLONG expires_on );

/* provider.c */
void provider_answer( const struct request *req, struct reply *reply );

/* signin.c: the sign-in window */
enum signin_state
{
    SIGNIN_CREATED,
    SIGNIN_WINDOW_OPEN,
    SIGNIN_CODE_RECEIVED,
    SIGNIN_REDEEMED,
    SIGNIN_DONE,
    SIGNIN_CANCELLED,
    SIGNIN_FAILED,
};

struct signin
{
    enum signin_state state;
    const struct oauth_flow *flow;
    HWND owner;
    const WCHAR *program_path;  /* the calling program, named in the window's title */
    const WCHAR *program_name;
    WCHAR *code;            /* the authorization code (secret: never logged) */
    WCHAR *error;           /* the error the redirect or the window gave */
    WCHAR *error_code;
};

BOOL signin_transition( struct signin *signin, enum signin_state to );
void signin_fail( struct signin *signin, enum signin_state to, const WCHAR *code, const WCHAR *message );
BOOL signin_redirect( struct signin *signin, const WCHAR *url );
void signin_run_test( struct signin *signin );
void signin_run_webview( struct signin *signin );

#endif
