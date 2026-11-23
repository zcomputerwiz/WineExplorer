/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token broker's helpers: strings, URLs, base64url, PKCE's random and hash, a small JSON reader for the token
 * endpoint's answers and ID token claims, and HTTPS through winhttp (certificates checked, redirects not followed).
 */

#include "broker.h"

#include <wincrypt.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <sddl.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

WCHAR *strndupW( const WCHAR *str, unsigned int len )
{
    WCHAR *ret;

    if (!str) return NULL;
    if (!(ret = malloc( (len + 1) * sizeof(WCHAR) ))) return NULL;
    memcpy( ret, str, len * sizeof(WCHAR) );
    ret[len] = 0;
    return ret;
}

WCHAR *strdupW( const WCHAR *str )
{
    return str ? strndupW( str, wcslen( str ) ) : NULL;
}

char *strdup_utf8( const WCHAR *str )
{
    int len;
    char *ret;

    if (!str) return NULL;
    len = WideCharToMultiByte( CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL );
    if (!(ret = malloc( len ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, str, -1, ret, len, NULL, NULL );
    return ret;
}

/* len in bytes, or -1 for a NUL-terminated string */
WCHAR *strdupW_utf8( const char *str, int len )
{
    int count;
    WCHAR *ret;

    if (!str) return NULL;
    if (len < 0) len = strlen( str );
    count = MultiByteToWideChar( CP_UTF8, 0, str, len, NULL, 0 );
    if (!(ret = malloc( (count + 1) * sizeof(WCHAR) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, str, len, ret, count );
    ret[count] = 0;
    return ret;
}

BOOL text_append( struct text *text, const WCHAR *str, unsigned int len )
{
    if (text->len + len + 1 > text->size)
    {
        unsigned int size = max( 64, max( text->size * 2, text->len + len + 1 ) );
        WCHAR *data = realloc( text->data, size * sizeof(WCHAR) );
        if (!data) return FALSE;
        text->data = data;
        text->size = size;
    }
    memcpy( text->data + text->len, str, len * sizeof(WCHAR) );
    text->len += len;
    text->data[text->len] = 0;
    return TRUE;
}

BOOL text_append_str( struct text *text, const WCHAR *str )
{
    return text_append( text, str, wcslen( str ) );
}

/* "key\0value\0", as the protocol's pairs carry them */
BOOL text_append_pair( struct text *text, const WCHAR *key, const WCHAR *value )
{
    return text_append( text, key, wcslen( key ) + 1 ) && text_append( text, value, wcslen( value ) + 1 );
}

/* a JSON string: the quotes, and the escapes RFC 8259 requires */
BOOL text_append_json_string( struct text *text, const WCHAR *str )
{
    static const WCHAR hex[] = L"0123456789abcdef";
    const WCHAR *p;
    WCHAR buf[6];

    if (!text_append( text, L"\"", 1 )) return FALSE;
    for (p = str ? str : L""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            buf[0] = '\\';
            buf[1] = *p;
            if (!text_append( text, buf, 2 )) return FALSE;
        }
        else if (*p < 0x20)
        {
            buf[0] = '\\'; buf[1] = 'u'; buf[2] = '0'; buf[3] = '0';
            buf[4] = hex[*p >> 4];
            buf[5] = hex[*p & 15];
            if (!text_append( text, buf, 6 )) return FALSE;
        }
        else if (!text_append( text, p, 1 )) return FALSE;
    }
    return text_append( text, L"\"", 1 );
}

void text_free( struct text *text )
{
    free( text->data );
    memset( text, 0, sizeof(*text) );
}

/* text_free for text that held a secret */
void text_wipe( struct text *text )
{
    if (text->data) SecureZeroMemory( text->data, text->size * sizeof(WCHAR) );
    text_free( text );
}

/* free for a string that held a secret */
void wipe_free( WCHAR *str )
{
    if (str) SecureZeroMemory( str, wcslen( str ) * sizeof(WCHAR) );
    free( str );
}

/* a request property by name (case-insensitive, as programs differ in case), or NULL */
const WCHAR *request_property( const struct request *req, const WCHAR *key )
{
    unsigned int i;

    for (i = 0; req && i < req->prop_count; i++)
        if (!wcsicmp( req->prop_key[i], key )) return req->prop_value[i];
    return NULL;
}

/* whether the request carries the property KEY with the value VALUE */
BOOL request_property_is( const struct request *req, const WCHAR *key, const WCHAR *value )
{
    const WCHAR *v = request_property( req, key );
    return v && !wcscmp( v, value );
}

/* the account the request names by sign-in name, or NULL (MSAL sends LoginHint, others login_hint) */
const WCHAR *request_login_hint( const struct request *req )
{
    const WCHAR *hint = request_property( req, L"LoginHint" );
    return hint ? hint : request_property( req, L"login_hint" );
}

/* whether a space-separated scope set holds WORD */
BOOL scope_has( const WCHAR *scope, const WCHAR *word )
{
    size_t len = wcslen( word );
    const WCHAR *p = scope;

    if (!len) return FALSE;
    while (p && (p = wcsstr( p, word )))
    {
        if ((p == scope || p[-1] == ' ') && (!p[len] || p[len] == ' ')) return TRUE;
        p += len;
    }
    return FALSE;
}

/* the token's user, allocated */
SID *token_user_sid( HANDLE token )
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

/* A descriptor that admits this process's user only, with RIGHTS (SDDL) and the ACE flags given; Wine's server turns
 * it into the owner's mode bits for files and folders. sa->lpSecurityDescriptor is LocalFree'd by the caller. */
BOOL user_only_descriptor( const WCHAR *ace_flags, const WCHAR *rights, SECURITY_ATTRIBUTES *sa )
{
    WCHAR sddl[256], *sid_string = NULL;
    HANDLE token;
    SID *sid = NULL;
    BOOL ret;

    sa->nLength = sizeof(*sa);
    sa->bInheritHandle = FALSE;
    sa->lpSecurityDescriptor = NULL;
    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        sid = token_user_sid( token );
        CloseHandle( token );
    }
    if (!sid || !ConvertSidToStringSidW( sid, &sid_string ))
    {
        free( sid );
        return FALSE;
    }
    free( sid );
    swprintf( sddl, ARRAY_SIZE(sddl), L"D:P(A;%s;%s;;;%s)", ace_flags, rights, sid_string );
    LocalFree( sid_string );
    ret = ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa->lpSecurityDescriptor,
                                                                NULL );
    return ret;
}

BOOL reply_set( struct reply *reply, enum tokenbroker_reply_field field, const WCHAR *str )
{
    reply->field[field].len = 0;
    return text_append_str( &reply->field[field], str ? str : L"" );
}

void reply_error( struct reply *reply, int status, unsigned int code, const WCHAR *message )
{
    reply->status = status;
    reply->code = code;
    reply_set( reply, TOKENBROKER_REPLY_MESSAGE, message );
}

/* a failed request of any kind, with the status its kind uses for a provider error */
void reply_failure( struct reply *reply, unsigned int kind, unsigned int code, const WCHAR *message )
{
    int status;

    switch (kind)
    {
    case TOKENBROKER_FIND_ACCOUNTS: status = FIND_PROVIDER_ERROR; break;
    case TOKENBROKER_FIND_ACCOUNT:  status = TOKENBROKER_ACCOUNT_NONE; break;
    default:                        status = STATUS_PROVIDER_ERROR; break;
    }
    reply_error( reply, status, code, message );
}

/* RFC 3986 unreserved characters stay, everything else is percent-encoded UTF-8 */
static BOOL url_append_encoded( struct text *url, const WCHAR *value )
{
    static const char hex[] = "0123456789ABCDEF";
    char *utf8 = strdup_utf8( value ), *p;
    WCHAR buf[3];
    BOOL ret = TRUE;

    if (!utf8) return FALSE;
    for (p = utf8; *p && ret; p++)
    {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr( "-._~", c ))
        {
            buf[0] = c;
            ret = text_append( url, buf, 1 );
        }
        else
        {
            buf[0] = '%';
            buf[1] = hex[c >> 4];
            buf[2] = hex[c & 15];
            ret = text_append( url, buf, 3 );
        }
    }
    free( utf8 );
    return ret;
}

/* name=value, after '?' or '&' as the URL needs (a form body starts empty and gets '&' between pairs) */
BOOL url_append_param( struct text *url, const WCHAR *name, const WCHAR *value )
{
    WCHAR sep = 0;

    if (url->len) sep = wcschr( url->data, '?' ) || !wcschr( url->data, '/' ) ? '&' : '?';
    return (!sep || text_append( url, &sep, 1 )) && url_append_encoded( url, name ) && text_append( url, L"=", 1 ) &&
           url_append_encoded( url, value );
}

static int hex_value( WCHAR c )
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* a parameter of a URL's query, decoded, or NULL */
WCHAR *url_query_param( const WCHAR *url, const WCHAR *name )
{
    const WCHAR *p = wcschr( url, '?' ), *end, *value;
    unsigned int name_len = wcslen( name );
    char *bytes, *out;
    WCHAR *ret;

    if (!p) return NULL;
    for (p++; *p; p = *end ? end + 1 : end)
    {
        if (!(end = wcspbrk( p, L"&#" ))) end = p + wcslen( p );
        if (end - p > name_len && !wcsncmp( p, name, name_len ) && p[name_len] == '=')
        {
            value = p + name_len + 1;
            if (!(bytes = malloc( end - value + 1 ))) return NULL;
            for (out = bytes; value < end; value++)
            {
                if (*value == '%' && value + 2 < end && hex_value( value[1] ) >= 0 && hex_value( value[2] ) >= 0)
                {
                    *out++ = hex_value( value[1] ) * 16 + hex_value( value[2] );
                    value += 2;
                }
                else if (*value == '+') *out++ = ' ';
                else if (*value < 0x80) *out++ = *value;
                else { free( bytes ); return NULL; }
            }
            ret = strdupW_utf8( bytes, out - bytes );
            free( bytes );
            return ret;
        }
        if (*end == '#') break;
    }
    return NULL;
}

char *base64url_encode( const BYTE *data, DWORD len )
{
    DWORD size = 0, i, j;
    char *ret;

    if (!CryptBinaryToStringA( data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &size )) return NULL;
    if (!(ret = malloc( size ))) return NULL;
    if (!CryptBinaryToStringA( data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, ret, &size ))
    {
        free( ret );
        return NULL;
    }
    for (i = j = 0; ret[i]; i++)
    {
        if (ret[i] == '=') break;
        ret[j++] = ret[i] == '+' ? '-' : ret[i] == '/' ? '_' : ret[i];
    }
    ret[j] = 0;
    return ret;
}

BYTE *base64url_decode( const char *str, DWORD *len )
{
    size_t n = strlen( str ), i;
    char *std;
    BYTE *ret = NULL;
    DWORD size = 0;

    if (!(std = malloc( n + 4 ))) return NULL;
    for (i = 0; i < n; i++) std[i] = str[i] == '-' ? '+' : str[i] == '_' ? '/' : str[i];
    while (i % 4) std[i++] = '=';
    std[i] = 0;
    if (CryptStringToBinaryA( std, 0, CRYPT_STRING_BASE64, NULL, &size, NULL, NULL ) && (ret = malloc( size + 1 )) &&
        CryptStringToBinaryA( std, 0, CRYPT_STRING_BASE64, ret, &size, NULL, NULL ))
    {
        ret[size] = 0;
        *len = size;
    }
    else
    {
        free( ret );
        ret = NULL;
    }
    free( std );
    return ret;
}

BOOL random_bytes( BYTE *buffer, ULONG len )
{
    return !BCryptGenRandom( NULL, buffer, len, BCRYPT_USE_SYSTEM_PREFERRED_RNG );
}

BOOL sha256( const BYTE *data, ULONG len, BYTE hash[32] )
{
    BCRYPT_ALG_HANDLE alg;
    BOOL ret;

    if (BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) return FALSE;
    ret = !BCryptHash( alg, NULL, 0, (UCHAR *)data, len, hash, 32 );
    BCryptCloseAlgorithmProvider( alg, 0 );
    return ret;
}

/*
 * A small JSON reader: the value of a member of the top-level object, string or number. Enough for the token
 * endpoint's answers (RFC 6749 section 5) and an ID token's claims; nested values are skipped, not read.
 */

static const char *json_skip_ws( const char *p )
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* past a string starting at its quote; the decoded text in *out (UTF-8, allocated) when out is given */
static const char *json_parse_string( const char *p, char **out )
{
    char *buf = NULL, *q = NULL;
    const char *start = ++p;

    if (out)
    {
        if (!(buf = malloc( strlen( start ) * 3 + 1 ))) return NULL;
        q = buf;
    }
    while (*p && *p != '"')
    {
        if (*p == '\\')
        {
            unsigned int c;
            p++;
            switch (*p)
            {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u':
                if (!p[1] || !p[2] || !p[3] || !p[4]) goto fail;
                c = 0;
                {
                    int i;
                    for (i = 1; i <= 4; i++)
                    {
                        int v = hex_value( p[i] );
                        if (v < 0) goto fail;
                        c = c * 16 + v;
                    }
                }
                p += 4;
                break;
            case 0: goto fail;
            default: c = (unsigned char)*p; break;
            }
            if (q)
            {
                /* a surrogate half on its own is written as U+FFFD; no claim we read uses pairs */
                if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
                if (c < 0x80) *q++ = c;
                else if (c < 0x800) { *q++ = 0xc0 | (c >> 6); *q++ = 0x80 | (c & 0x3f); }
                else { *q++ = 0xe0 | (c >> 12); *q++ = 0x80 | ((c >> 6) & 0x3f); *q++ = 0x80 | (c & 0x3f); }
            }
            p++;
        }
        else
        {
            if (q) *q++ = *p;
            p++;
        }
    }
    if (*p != '"') goto fail;
    if (out)
    {
        *q = 0;
        *out = buf;
    }
    return p + 1;
fail:
    free( buf );
    return NULL;
}

/* past any value */
static const char *json_skip_value( const char *p, int depth )
{
    p = json_skip_ws( p );
    if (depth > 32) return NULL;
    if (*p == '"') return json_parse_string( p, NULL );
    if (*p == '{' || *p == '[')
    {
        char close = *p == '{' ? '}' : ']';
        p = json_skip_ws( p + 1 );
        if (*p == close) return p + 1;
        for (;;)
        {
            if (close == '}')
            {
                if (*p != '"' || !(p = json_parse_string( p, NULL ))) return NULL;
                p = json_skip_ws( p );
                if (*p++ != ':') return NULL;
            }
            if (!(p = json_skip_value( p, depth + 1 ))) return NULL;
            p = json_skip_ws( p );
            if (*p == close) return p + 1;
            if (*p++ != ',') return NULL;
            p = json_skip_ws( p );
        }
    }
    while (*p && !strchr( ",}] \t\r\n", *p )) p++;
    return p;
}

/* the start of a member's value in the top-level object, or NULL */
static const char *json_member( const char *json, const char *key )
{
    const char *p = json_skip_ws( json );
    char *name;

    if (!p || *p != '{') return NULL;
    p = json_skip_ws( p + 1 );
    while (*p == '"')
    {
        BOOL match;
        if (!(p = json_parse_string( p, &name ))) return NULL;
        match = !strcmp( name, key );
        free( name );
        p = json_skip_ws( p );
        if (*p++ != ':') return NULL;
        p = json_skip_ws( p );
        if (match) return p;
        if (!(p = json_skip_value( p, 0 ))) return NULL;
        p = json_skip_ws( p );
        if (*p != ',') return NULL;
        p = json_skip_ws( p + 1 );
    }
    return NULL;
}

WCHAR *json_string( const char *json, const char *key )
{
    const char *p = json ? json_member( json, key ) : NULL;
    char *value;
    WCHAR *ret;

    if (!p || *p != '"' || !json_parse_string( p, &value )) return NULL;
    ret = strdupW_utf8( value, -1 );
    free( value );
    return ret;
}

BOOL json_number( const char *json, const char *key, LONGLONG *value )
{
    const char *p = json ? json_member( json, key ) : NULL;
    char *s;

    if (!p) return FALSE;
    if (*p == '"')
    {
        /* some servers send expires_in as a string */
        if (!json_parse_string( p, &s )) return FALSE;
        *value = _atoi64( s );
        free( s );
        return TRUE;
    }
    if (*p != '-' && (*p < '0' || *p > '9')) return FALSE;
    *value = _atoi64( p );
    return TRUE;
}

/* the claims of a JWT (its middle part), as JSON text; the signature is not checked: the token came over TLS from
 * the authority and is only read for the account's names */
char *jwt_payload( const WCHAR *jwt )
{
    const WCHAR *dot1, *dot2;
    char *part;
    BYTE *json;
    DWORD len;

    if (!jwt || !(dot1 = wcschr( jwt, '.' )) || !(dot2 = wcschr( dot1 + 1, '.' ))) return NULL;
    if (!(part = malloc( dot2 - dot1 ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, dot1 + 1, dot2 - dot1 - 1, part, dot2 - dot1 - 1, NULL, NULL );
    part[dot2 - dot1 - 1] = 0;
    json = base64url_decode( part, &len );
    free( part );
    return (char *)json;
}

LONGLONG unix_time( void )
{
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimeAsFileTime( &ft );
    t.u.LowPart = ft.dwLowDateTime;
    t.u.HighPart = ft.dwHighDateTime;
    return (LONGLONG)(t.QuadPart / 10000000) - 11644473600ll;
}

/* GET (form NULL) or POST of an application/x-www-form-urlencoded body; redirects are reported, not followed */
BOOL http_request( const WCHAR *url, const char *form, struct http_response *response )
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    URL_COMPONENTS uc = { sizeof(uc) };
    WCHAR host[256], *path = NULL, *headers = NULL;
    DWORD size, read, total = 0, flags = 0, opt;
    char *body = NULL, *grown;
    BOOL ret = FALSE;

    memset( response, 0, sizeof(*response) );
    uc.lpszHostName = host;
    uc.dwHostNameLength = ARRAY_SIZE(host);
    uc.dwUrlPathLength = 1;
    uc.dwExtraInfoLength = 1;
    if (!WinHttpCrackUrl( url, 0, 0, &uc )) return FALSE;
    if (!(path = strndupW( uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength ))) return FALSE;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags = WINHTTP_FLAG_SECURE;

    if (!(session = WinHttpOpen( L"WineTokenBroker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0 )))
        goto done;
    WinHttpSetTimeouts( session, 10000, 10000, 30000, 30000 );
    if (!(connection = WinHttpConnect( session, host, uc.nPort, 0 ))) goto done;
    if (!(request = WinHttpOpenRequest( connection, form ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags )))
        goto done;
    opt = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &opt, sizeof(opt) );
    opt = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption( request, WINHTTP_OPTION_DISABLE_FEATURE, &opt, sizeof(opt) );
    if (!WinHttpSendRequest( request, form ? L"Content-Type: application/x-www-form-urlencoded\r\n" : NULL,
                             form ? (DWORD)-1 : 0, (void *)form, form ? strlen( form ) : 0, form ? strlen( form ) : 0, 0 ))
        goto done;
    if (!WinHttpReceiveResponse( request, NULL )) goto done;
    size = sizeof(response->status);
    if (!WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &response->status,
                              &size, NULL ))
        goto done;
    size = 0;
    if (!WinHttpQueryHeaders( request, WINHTTP_QUERY_LOCATION, NULL, NULL, &size, NULL ) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && (headers = malloc( size + sizeof(WCHAR) )) &&
        WinHttpQueryHeaders( request, WINHTTP_QUERY_LOCATION, NULL, headers, &size, NULL ))
    {
        response->location = headers;
        headers = NULL;
    }
    for (;;)
    {
        if (!WinHttpQueryDataAvailable( request, &size )) goto done;
        if (!size) break;
        if (total + size > 1024 * 1024) goto done;
        if (!(grown = realloc( body, total + size + 1 ))) goto done;
        body = grown;
        if (!WinHttpReadData( request, body + total, size, &read )) goto done;
        total += read;
    }
    if (!body && !(body = malloc( 1 ))) goto done;
    body[total] = 0;
    response->body = body;
    body = NULL;
    ret = TRUE;

done:
    if (!ret) WARN( "HTTP request to %s failed: %lu\n", debugstr_w( host ), GetLastError() );
    if (request) WinHttpCloseHandle( request );
    if (connection) WinHttpCloseHandle( connection );
    if (session) WinHttpCloseHandle( session );
    free( path );
    free( headers );
    free( body );
    if (!ret)
    {
        free( response->location );
        response->location = NULL;
    }
    return ret;
}

void http_response_free( struct http_response *response )
{
    free( response->body );
    free( response->location );
    memset( response, 0, sizeof(*response) );
}
