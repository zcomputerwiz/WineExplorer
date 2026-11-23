/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token answer's shape, as WAM gives it for a Microsoft account (docs/WAM_BROKER_DESIGN.md, "The shape of
 * Windows' answer for a Microsoft account"): the account's id and names from the id token, the response's properties
 * (MSAL's wamcompat_* set, or the batch form body with oauth2_batch=1), the telemetry blob (MATS) and the account's
 * properties. Only what the id token tells is sent; nothing is made up.
 */

#include "broker.h"

/* A Microsoft account's object id is "00000000-0000-0000-" and the 16 hex digits of its customer id. WAM names such an
 * account by 16 upper-case hex digits (its WebAccount.Id), and programs that parse the id expect that form: the
 * cache, the answers and FindAllAccounts use it. Any other id is returned unchanged. Allocated. */
static WCHAR *public_account_id( const WCHAR *oid )
{
    static const WCHAR zeros[] = L"00000000-0000-0000-";
    WCHAR *id;
    unsigned int i, n = 0;

    if (wcslen( oid ) != 36 || wcsncmp( oid, zeros, ARRAY_SIZE(zeros) - 1 )) return strdupW( oid );
    if (!(id = calloc( 17, sizeof(WCHAR) ))) return NULL;
    for (i = ARRAY_SIZE(zeros) - 1; i < 36; i++)
    {
        if (oid[i] == '-') continue;
        if (!iswxdigit( oid[i] ) || n >= 16) break;
        id[n++] = towupper( oid[i] );
    }
    if (i < 36 || n != 16)
    {
        free( id );
        return strdupW( oid );
    }
    return id;
}

/* whether ID is a public account id: 16 hex digits */
static BOOL is_public_account_id( const WCHAR *id )
{
    unsigned int i;

    if (!id || wcslen( id ) != 16) return FALSE;
    for (i = 0; i < 16; i++) if (!iswxdigit( id[i] )) return FALSE;
    return TRUE;
}

/* the object id form of a public account id (for the wamcompat_account_id property), or a copy of ID */
static WCHAR *object_account_id( const WCHAR *id )
{
    WCHAR *out;
    unsigned int i;

    if (!is_public_account_id( id )) return strdupW( id );
    if (!(out = calloc( 37, sizeof(WCHAR) ))) return NULL;
    swprintf( out, 37, L"00000000-0000-0000-%.4s-%.12s", id, id + 4 );
    for (i = 0; out[i]; i++) out[i] = towlower( out[i] );
    return out;
}

/* the account's id and sign-in name: the id token's object id (else its subject), its preferred_username (else email,
 * else the login hint) */
void account_names( const struct token_result *r, const WCHAR *hint, WCHAR **id, WCHAR **name )
{
    char *claims = jwt_payload( r->id_token );
    WCHAR *oid = json_string( claims, "oid" );

    if (!oid) oid = json_string( claims, "sub" );
    *id = oid ? public_account_id( oid ) : NULL;
    free( oid );
    *name = json_string( claims, "preferred_username" );
    if (!*name) *name = json_string( claims, "email" );
    if (!*name) *name = strdupW( hint ? hint : L"" );
    free( claims );
}

static void append_pair_if( struct text *props, const WCHAR *key, const WCHAR *value )
{
    if (value && *value) text_append_pair( props, key, value );
}

/* the answer's account properties, as WAM's answer for a Microsoft account has them (names, a customer id) */
static void account_properties( struct reply *reply, const WCHAR *account_id, const WCHAR *user_name,
                                const WCHAR *id_token )
{
    struct text *props = &reply->field[TOKENBROKER_REPLY_ACCOUNT_PROPERTIES];
    char *claims = jwt_payload( id_token );
    WCHAR *name = json_string( claims, "name" ), *given = json_string( claims, "given_name" );
    WCHAR *family = json_string( claims, "family_name" ), *country = json_string( claims, "ctry" );
    WCHAR *space;
    unsigned int i;

    /* no given name: the display name's words before the last, and the last as the family name */
    if (!given && name && (space = wcsrchr( name, ' ' )) && space > name && (given = strndupW( name, space - name )) &&
        !family)
        family = strdupW( space + 1 );

    text_append_pair( props, L"IsRoamingEncryptionKeyAvailable", L"false" );
    append_pair_if( props, L"DisplayName", name );
    append_pair_if( props, L"FirstName", given );
    append_pair_if( props, L"CountryOrRegion", country );
    append_pair_if( props, L"UserName", user_name );
    append_pair_if( props, L"UID", account_id );
    append_pair_if( props, L"LastName", family );
    if (is_public_account_id( account_id ))
    {
        WCHAR cid[17];

        for (i = 0; i < 16; i++) cid[i] = towlower( account_id[i] );
        cid[16] = 0;
        text_append_pair( props, L"SafeCustomerId", cid );
    }
    free( name ); free( given ); free( family ); free( country ); free( claims );
}

/* the telemetry blob WAM returns in the answer's "MATS" property when the request asks for it (telemetry=MATS) */
static void telemetry_property( struct reply *reply, const struct request *req, const WCHAR *account_id,
                                BOOL interactive, BOOL cached )
{
    struct text json = {0};
    BOOL ok;

    ok = text_append_str( &json, L"{\"ui_visible\":" ) &&
         text_append_str( &json, interactive ? L"true" : L"false" ) &&
         text_append_str( &json, L",\"correlation_id\":" ) &&
         text_append_json_string( &json, req->field[TOKENBROKER_FIELD_CORRELATION_ID] ) &&
         text_append_str( &json, L",\"account_id\":" ) &&
         text_append_json_string( &json, account_id ) &&
         text_append_str( &json, L",\"web_flow_result\":" ) &&
         text_append_str( &json, interactive ? L"\"success\"" : L"\"none\"" ) &&
         text_append_str( &json, L",\"silent_code\":0,\"silent_bi_sub_code\":0,\"silent_message\":\"\","
                                 L"\"silent_status\":0,\"is_cached\":" ) &&
         text_append_str( &json, cached ? L"true}" : L"false}" );
    if (ok) text_append_pair( &reply->field[TOKENBROKER_REPLY_PROPERTIES], L"MATS", json.data );
    text_free( &json );
}

/* With the request property oauth2_batch=1 (the Microsoft account library sends it with api-version 2.0), the answer's
 * Token is not the bare access token but the token endpoint's answer as a form body, in this order: access_token,
 * token_type, expires_in (seconds left), scope, id_token, client_info. Allocated, or NULL. */
static WCHAR *batch_token( const WCHAR *token, const WCHAR *scope, const WCHAR *id_token, const WCHAR *client_info,
                           LONGLONG expires_on )
{
    struct text body = {0};
    WCHAR expires_in[32];
    LONGLONG left = expires_on - unix_time();

    swprintf( expires_in, ARRAY_SIZE(expires_in), L"%I64d", left > 0 ? left : 0 );
    if (!url_append_param( &body, L"access_token", token ) || !url_append_param( &body, L"token_type", L"Bearer" ) ||
        !url_append_param( &body, L"expires_in", expires_in ) ||
        !url_append_param( &body, L"scope", scope ? scope : L"" ) ||
        (id_token && *id_token && !url_append_param( &body, L"id_token", id_token )) ||
        (client_info && *client_info && !url_append_param( &body, L"client_info", client_info )))
    {
        text_wipe( &body );
        return NULL;
    }
    return body.data;
}

/* the answer for a token: INTERACTIVE when the user signed in, CACHED when it came from the cache */
void reply_success( struct reply *reply, const struct request *req, BOOL interactive, BOOL cached,
                    const WCHAR *token, const WCHAR *account_id, const WCHAR *user_name, const WCHAR *id_token,
                    const WCHAR *client_info, const WCHAR *scope, LONGLONG expires_on )
{
    struct text *props = &reply->field[TOKENBROKER_REPLY_PROPERTIES];
    WCHAR expires[32], *object_id, *batch = NULL;

    reply->status = STATUS_SUCCESS_;
    reply->code = 0;
    if (request_property_is( req, L"oauth2_batch", L"1" ))
        batch = batch_token( token, scope, id_token, client_info, expires_on );
    reply_set( reply, TOKENBROKER_REPLY_TOKEN, batch ? batch : token );
    reply_set( reply, TOKENBROKER_REPLY_ACCOUNT_ID, account_id );
    reply_set( reply, TOKENBROKER_REPLY_USER_NAME, user_name );
    if (!batch)
    {
        /* the properties MSAL reads from a WAM answer */
        swprintf( expires, ARRAY_SIZE(expires), L"%I64d", expires_on );
        append_pair_if( props, L"wamcompat_id_token", id_token );
        append_pair_if( props, L"wamcompat_client_info", client_info );
        append_pair_if( props, L"wamcompat_scopes", scope );
        text_append_pair( props, L"wamcompat_expires_on", expires );
        append_pair_if( props, L"wamcompat_user_displayable_id", user_name );
        if (account_id && *account_id && (object_id = object_account_id( account_id )))
        {
            text_append_pair( props, L"wamcompat_account_id", object_id );
            free( object_id );
        }
    }
    wipe_free( batch );
    if (request_property_is( req, L"telemetry", L"MATS" ))
        telemetry_property( reply, req, account_id, interactive, cached );
    account_properties( reply, account_id, user_name, id_token );
}
