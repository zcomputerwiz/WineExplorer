/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The token broker's cache (docs/WAM_BROKER_DESIGN.md, "The cache"): per user, in the user's own home outside every
 * prefix, ~/.local/share/wineexplorer/tokenbroker/cache. The folder and the file are created with a descriptor that
 * admits the user only, which Wine's server turns into modes 0700 and 0600; the file is written whole to a temporary
 * file and renamed. One line per record, fields separated by tabs (a value with a tab or line break is not stored):
 *
 *   account  provider  authority  id  user name  client id  refresh token
 *   token    provider  authority  account id  client id  scope  expires on  access token  id token  client info  scope granted
 *
 * Accounts are keyed by provider, account and client (a refresh token is the client's), access tokens by provider,
 * authority, account, client and scope set. Refresh tokens are the secret: nothing here logs them.
 * Not done yet: the Secret Service on the session bus for the refresh tokens (docs/KNOWN_BUGS.md, B72).
 */

#include "broker.h"

#include <sddl.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(tokenbroker);

static CRITICAL_SECTION cache_cs;
static CRITICAL_SECTION_DEBUG cache_cs_debug =
{
    0, 0, &cache_cs,
    { &cache_cs_debug.ProcessLocksList, &cache_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": cache_cs") }
};
static CRITICAL_SECTION cache_cs = { &cache_cs_debug, -1, 0, 0, 0, 0 };

static struct account **accounts;
static unsigned int account_count;
static struct access_token **tokens;
static unsigned int token_count;

void cache_lock( void )
{
    EnterCriticalSection( &cache_cs );
}

void cache_unlock( void )
{
    LeaveCriticalSection( &cache_cs );
}

static BOOL same( const WCHAR *a, const WCHAR *b )
{
    return !wcscmp( a ? a : L"", b ? b : L"" );
}

static BOOL storable( const WCHAR *value )
{
    return !value || !wcspbrk( value, L"\t\r\n" );
}

static void account_free( struct account *a )
{
    if (!a) return;
    free( a->provider_id );
    free( a->authority );
    free( a->id );
    free( a->user_name );
    free( a->client_id );
    if (a->refresh_token) SecureZeroMemory( a->refresh_token, wcslen( a->refresh_token ) * sizeof(WCHAR) );
    free( a->refresh_token );
    free( a );
}

static void token_free( struct access_token *t )
{
    if (!t) return;
    free( t->provider_id );
    free( t->authority );
    free( t->account_id );
    free( t->client_id );
    free( t->scope );
    free( t->token );
    free( t->id_token );
    free( t->client_info );
    free( t->granted_scope );
    free( t );
}

/* the same scope set: the same words in any order */
static BOOL same_scope( const WCHAR *a, const WCHAR *b )
{
    WCHAR *copy, *word, *ctx;
    unsigned int na = 0, nb = 0;
    BOOL ret = TRUE;

    if (!(copy = strdupW( a ? a : L"" ))) return FALSE;
    for (word = wcstok_s( copy, L" ", &ctx ); word && ret; word = wcstok_s( NULL, L" ", &ctx ))
    {
        const WCHAR *p = b ? b : L"";
        size_t len = wcslen( word );
        BOOL found = FALSE;
        na++;
        while ((p = wcsstr( p, word )))
        {
            if ((p == b || p[-1] == ' ') && (!p[len] || p[len] == ' ')) { found = TRUE; break; }
            p += len;
        }
        ret = found;
    }
    free( copy );
    if (!ret) return FALSE;
    if (!(copy = strdupW( b ? b : L"" ))) return FALSE;
    for (word = wcstok_s( copy, L" ", &ctx ); word; word = wcstok_s( NULL, L" ", &ctx )) nb++;
    free( copy );
    return na == nb;
}

/* ~/.local/share/wineexplorer/tokenbroker, as a DOS path (allocated), its folders made */
static WCHAR *cache_dir( void )
{
    static WCHAR *(CDECL *get_dos_name)( const char * );
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    WCHAR *dir = NULL, *path = NULL, *sid_string = NULL;
    char home[MAX_PATH * 2], unix_path[MAX_PATH * 2];
    WCHAR sddl[256];
    HANDLE token;
    char buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    DWORD len;

    if (!get_dos_name)
        get_dos_name = (void *)GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "wine_get_dos_file_name" );
    if (!get_dos_name) return NULL;
    /* Wine keeps the Unix HOME as WINE_HOST_HOME (ntdll's is_special_env_var) */
    if (!GetEnvironmentVariableA( "WINE_HOST_HOME", home, sizeof(home) ) &&
        !GetEnvironmentVariableA( "HOME", home, sizeof(home) ))
        return NULL;
    if (home[0] != '/') return NULL;
    snprintf( unix_path, sizeof(unix_path), "%s/.local/share/wineexplorer/tokenbroker", home );
    if (!(dir = get_dos_name( unix_path ))) return NULL;

    /* the user only: the folder becomes 0700 */
    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len ))
            ConvertSidToStringSidW( user->User.Sid, &sid_string );
        CloseHandle( token );
    }
    if (!sid_string) goto fail;
    swprintf( sddl, ARRAY_SIZE(sddl), L"D:P(A;OICI;FA;;;%s)", sid_string );
    LocalFree( sid_string );
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL ))
        goto fail;
    /* the parents as usual, the last two folders with the descriptor */
    if ((path = strdupW( dir )))
    {
        WCHAR *p, *wex = NULL;
        for (p = path + 3; *p; p++)
        {
            if (*p != '\\') continue;
            *p = 0;
            if (!wcscmp( wcsrchr( path, '\\' ) + 1, L"wineexplorer" )) wex = p;
            CreateDirectoryW( path, wex ? &sa : NULL );
            *p = '\\';
        }
        /* the cache's own folder gets the descriptor also when it is already there (its mode follows it; the shared
         * wineexplorer folder above is left as it is) */
        if (!CreateDirectoryW( path, &sa ))
            SetFileSecurityW( path, DACL_SECURITY_INFORMATION, sa.lpSecurityDescriptor );
        free( path );
    }
    LocalFree( sa.lpSecurityDescriptor );
    return dir;

fail:
    HeapFree( GetProcessHeap(), 0, dir );
    return NULL;
}

static WCHAR *cache_file( const WCHAR *name )
{
    WCHAR *dir = cache_dir(), *ret;
    size_t len;

    if (!dir) return NULL;
    len = wcslen( dir ) + wcslen( name ) + 2;
    if ((ret = malloc( len * sizeof(WCHAR) ))) swprintf( ret, len, L"%s\\%s", dir, name );
    HeapFree( GetProcessHeap(), 0, dir );
    return ret;
}

/* the fields of a line (tabs become NULs) */
static unsigned int split( WCHAR *line, WCHAR **fields, unsigned int max )
{
    unsigned int n = 0;
    WCHAR *p = line;

    while (n < max)
    {
        fields[n++] = p;
        if (!(p = wcschr( p, '\t' ))) break;
        *p++ = 0;
    }
    return n;
}

void cache_load( void )
{
    WCHAR *path, *text, *line, *next, *f[12];
    HANDLE file;
    DWORD size, read;
    char *bytes;
    unsigned int n;

    if (!(path = cache_file( L"cache" ))) return;
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    free( path );
    if (file == INVALID_HANDLE_VALUE) return;
    size = GetFileSize( file, NULL );
    if (size == INVALID_FILE_SIZE || size > 16 * 1024 * 1024 || !(bytes = malloc( size + 1 )))
    {
        CloseHandle( file );
        return;
    }
    if (!ReadFile( file, bytes, size, &read, NULL )) read = 0;
    CloseHandle( file );
    text = strdupW_utf8( bytes, read );
    SecureZeroMemory( bytes, size );
    free( bytes );
    if (!text) return;

    cache_lock();
    for (line = text; line && *line; line = next)
    {
        if ((next = wcschr( line, '\n' ))) *next++ = 0;
        n = split( line, f, ARRAY_SIZE(f) );
        if (n == 7 && !wcscmp( f[0], L"account" ))
        {
            struct account a = { f[1], f[2], f[3], f[4], f[5], f[6] };
            cache_store_account( &a );
        }
        else if (n == 11 && !wcscmp( f[0], L"token" ))
        {
            struct access_token t = { f[1], f[2], f[3], f[4], f[5], _wtoi64( f[6] ), f[7], f[8], f[9], f[10] };
            if (t.expires_on > unix_time()) cache_store_token( &t );
        }
    }
    cache_unlock();
    SecureZeroMemory( text, wcslen( text ) * sizeof(WCHAR) );
    free( text );
    TRACE( "%u accounts, %u access tokens\n", account_count, token_count );
}

static BOOL write_fields( struct text *out, const WCHAR **fields, unsigned int count )
{
    unsigned int i;

    for (i = 0; i < count; i++)
        if ((i && !text_append( out, L"\t", 1 )) || !text_append_str( out, fields[i] ? fields[i] : L"" )) return FALSE;
    return text_append( out, L"\n", 1 );
}

BOOL cache_save( void )
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    WCHAR *path = NULL, *temp = NULL, expires[32], *sid_string = NULL, sddl[256];
    struct text out = {0};
    char *bytes = NULL;
    HANDLE file = INVALID_HANDLE_VALUE, token;
    char buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    DWORD len, written;
    BOOL ret = FALSE;
    unsigned int i;
    LONGLONG now = unix_time();

    cache_lock();
    for (i = 0; i < account_count; i++)
    {
        const struct account *a = accounts[i];
        const WCHAR *f[] = { L"account", a->provider_id, a->authority, a->id, a->user_name, a->client_id,
                             a->refresh_token };
        if (!write_fields( &out, f, ARRAY_SIZE(f) )) goto done;
    }
    for (i = 0; i < token_count; i++)
    {
        const struct access_token *t = tokens[i];
        const WCHAR *f[] = { L"token", t->provider_id, t->authority, t->account_id, t->client_id, t->scope, expires,
                             t->token, t->id_token, t->client_info, t->granted_scope };
        if (t->expires_on <= now) continue;
        swprintf( expires, ARRAY_SIZE(expires), L"%I64d", t->expires_on );
        if (!write_fields( &out, f, ARRAY_SIZE(f) )) goto done;
    }
    if (!(bytes = strdup_utf8( out.data ? out.data : L"" ))) goto done;

    if (!(path = cache_file( L"cache" )) || !(temp = cache_file( L"cache.new" ))) goto done;
    /* the user only: the file becomes 0600 */
    if (OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
    {
        if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len ))
            ConvertSidToStringSidW( user->User.Sid, &sid_string );
        CloseHandle( token );
    }
    if (!sid_string) goto done;
    swprintf( sddl, ARRAY_SIZE(sddl), L"D:P(A;;FRFW;;;%s)", sid_string );
    LocalFree( sid_string );
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL ))
        goto done;
    /* a leftover temporary file would keep its own mode: it goes first, and the new one is created anew */
    DeleteFileW( temp );
    file = CreateFileW( temp, GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
    LocalFree( sa.lpSecurityDescriptor );
    if (file == INVALID_HANDLE_VALUE) goto done;
    ret = WriteFile( file, bytes, strlen( bytes ), &written, NULL ) && written == strlen( bytes );
    CloseHandle( file );
    if (ret) ret = MoveFileExW( temp, path, MOVEFILE_REPLACE_EXISTING );
    if (!ret) DeleteFileW( temp );

done:
    cache_unlock();
    if (!ret) WARN( "the cache was not written: %lu\n", GetLastError() );
    if (bytes) SecureZeroMemory( bytes, strlen( bytes ) );
    free( bytes );
    if (out.data) SecureZeroMemory( out.data, out.len * sizeof(WCHAR) );
    text_free( &out );
    free( path );
    free( temp );
    return ret;
}

/* an account by id, or else by user name (case-insensitive), for the provider and authority, of exactly this client
 * (an empty client id matches only an empty one, never every client); the caller holds the lock */
struct account *cache_find_account( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *id,
                                    const WCHAR *user_name, const WCHAR *client_id )
{
    unsigned int i;

    for (i = 0; i < account_count; i++)
    {
        struct account *a = accounts[i];
        if (!same( a->provider_id, provider_id )) continue;
        if (authority && *authority && !same( a->authority, authority )) continue;
        if (!same( a->client_id, client_id )) continue;
        if (id && *id && same( a->id, id )) return a;
        if ((!id || !*id) && user_name && *user_name && !wcsicmp( a->user_name, user_name )) return a;
    }
    return NULL;
}

/* the accounts of a provider and authority for exactly this client (an empty client id matches only an empty one,
 * never every client), each once; the array is the caller's to free, the accounts stay the cache's; the caller holds the lock */
unsigned int cache_list_accounts( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *client_id,
                                  struct account ***out )
{
    unsigned int i, j, n = 0;

    *out = NULL;
    if (!account_count || !(*out = calloc( account_count, sizeof(**out) ))) return 0;
    for (i = 0; i < account_count; i++)
    {
        struct account *a = accounts[i];
        if (!same( a->provider_id, provider_id )) continue;
        if (authority && *authority && !same( a->authority, authority )) continue;
        if (!same( a->client_id, client_id )) continue;
        for (j = 0; j < n; j++) if (same( (*out)[j]->id, a->id )) break;
        if (j == n) (*out)[n++] = a;
    }
    return n;
}

static struct account *account_copy( const struct account *src )
{
    struct account *a = calloc( 1, sizeof(*a) );

    if (!a) return NULL;
    a->provider_id = strdupW( src->provider_id ? src->provider_id : L"" );
    a->authority = strdupW( src->authority ? src->authority : L"" );
    a->id = strdupW( src->id ? src->id : L"" );
    a->user_name = strdupW( src->user_name ? src->user_name : L"" );
    a->client_id = strdupW( src->client_id ? src->client_id : L"" );
    a->refresh_token = strdupW( src->refresh_token ? src->refresh_token : L"" );
    if (!a->provider_id || !a->authority || !a->id || !a->user_name || !a->client_id || !a->refresh_token)
    {
        account_free( a );
        return NULL;
    }
    return a;
}

/* adds or replaces the account of (provider, id, client); the caller holds the lock */
BOOL cache_store_account( const struct account *src )
{
    struct account *a, **grown;
    unsigned int i;

    if (!storable( src->provider_id ) || !storable( src->authority ) || !storable( src->id ) ||
        !storable( src->user_name ) || !storable( src->client_id ) || !storable( src->refresh_token ) ||
        !src->id || !*src->id)
        return FALSE;
    if (!(a = account_copy( src ))) return FALSE;
    for (i = 0; i < account_count; i++)
    {
        if (same( accounts[i]->provider_id, a->provider_id ) && same( accounts[i]->id, a->id ) &&
            same( accounts[i]->client_id, a->client_id ))
        {
            account_free( accounts[i] );
            accounts[i] = a;
            return TRUE;
        }
    }
    if (!(grown = realloc( accounts, (account_count + 1) * sizeof(*accounts) )))
    {
        account_free( a );
        return FALSE;
    }
    accounts = grown;
    accounts[account_count++] = a;
    return TRUE;
}

/* removes the account's refresh tokens and access tokens of exactly this client; the caller holds the lock */
void cache_remove_account( const WCHAR *provider_id, const WCHAR *id, const WCHAR *client_id )
{
    unsigned int i, j;

    for (i = j = 0; i < account_count; i++)
    {
        struct account *a = accounts[i];
        if (same( a->provider_id, provider_id ) && same( a->id, id ) &&
            same( a->client_id, client_id ))
            account_free( a );
        else accounts[j++] = a;
    }
    account_count = j;
    for (i = j = 0; i < token_count; i++)
    {
        struct access_token *t = tokens[i];
        if (same( t->provider_id, provider_id ) && same( t->account_id, id ) &&
            same( t->client_id, client_id ))
            token_free( t );
        else tokens[j++] = t;
    }
    token_count = j;
}

/* an access token of the account for the client and scope set that is valid after valid_until; the caller holds the
 * lock */
struct access_token *cache_find_token( const WCHAR *provider_id, const WCHAR *authority, const WCHAR *account_id,
                                       const WCHAR *client_id, const WCHAR *scope, LONGLONG valid_until )
{
    unsigned int i;

    for (i = 0; i < token_count; i++)
    {
        struct access_token *t = tokens[i];
        if (same( t->provider_id, provider_id ) && same( t->authority, authority ) &&
            same( t->account_id, account_id ) && same( t->client_id, client_id ) && same_scope( t->scope, scope ) &&
            t->expires_on > valid_until)
            return t;
    }
    return NULL;
}

/* adds or replaces the token of (provider, authority, account, client, scope set); the caller holds the lock */
BOOL cache_store_token( const struct access_token *src )
{
    struct access_token *t, **grown;
    unsigned int i;

    if (!storable( src->provider_id ) || !storable( src->authority ) || !storable( src->account_id ) ||
        !storable( src->client_id ) || !storable( src->scope ) || !storable( src->token ) ||
        !storable( src->id_token ) || !storable( src->client_info ) || !storable( src->granted_scope ))
        return FALSE;
    if (!(t = calloc( 1, sizeof(*t) ))) return FALSE;
    t->provider_id = strdupW( src->provider_id ? src->provider_id : L"" );
    t->authority = strdupW( src->authority ? src->authority : L"" );
    t->account_id = strdupW( src->account_id ? src->account_id : L"" );
    t->client_id = strdupW( src->client_id ? src->client_id : L"" );
    t->scope = strdupW( src->scope ? src->scope : L"" );
    t->expires_on = src->expires_on;
    t->token = strdupW( src->token ? src->token : L"" );
    t->id_token = strdupW( src->id_token ? src->id_token : L"" );
    t->client_info = strdupW( src->client_info ? src->client_info : L"" );
    t->granted_scope = strdupW( src->granted_scope ? src->granted_scope : L"" );
    if (!t->provider_id || !t->authority || !t->account_id || !t->client_id || !t->scope || !t->token ||
        !t->id_token || !t->client_info || !t->granted_scope)
    {
        token_free( t );
        return FALSE;
    }
    for (i = 0; i < token_count; i++)
    {
        struct access_token *o = tokens[i];
        if (same( o->provider_id, t->provider_id ) && same( o->authority, t->authority ) &&
            same( o->account_id, t->account_id ) && same( o->client_id, t->client_id ) && same_scope( o->scope, t->scope ))
        {
            token_free( o );
            tokens[i] = t;
            return TRUE;
        }
    }
    if (!(grown = realloc( tokens, (token_count + 1) * sizeof(*tokens) )))
    {
        token_free( t );
        return FALSE;
    }
    tokens = grown;
    tokens[token_count++] = t;
    return TRUE;
}
