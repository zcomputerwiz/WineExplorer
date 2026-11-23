/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * rootstore-probe: opens the trusted root stores as the calling user and prints, for each,
 *   NAME=open count=N      or      NAME=error E
 * machine_root: LocalMachine\Root read-write; machine_root_ro: the same read-only; user_root: the user's Root
 * (CertOpenSystemStore), which programs such as winhttp use. Windows, as a non-elevated user (2026-11-22, x64 and
 * x86): machine_root=error 5, machine_root_ro=open count=62, user_root=open count=62: the user's Root includes the
 * machine's roots read-only. Wine's user Root once lost them for a limited user, so no root was trusted and HTTPS
 * failed in the machine prefix.
 */

#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

static void report( const char *name, HCERTSTORE store )
{
    const CERT_CONTEXT *cert = NULL;
    unsigned int count = 0;

    if (!store)
    {
        printf( "%s=error %lu\n", name, GetLastError() );
        return;
    }
    while ((cert = CertEnumCertificatesInStore( store, cert ))) count++;
    printf( "%s=open count=%u\n", name, count );
    CertCloseStore( store, 0 );
}

/* the registry calls crypt32 makes, one by one: NAME=status (0 is success) */
static void reg_open( const char *name, const WCHAR *path, REGSAM sam )
{
    HKEY key;
    LONG rc = RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0, sam, &key );

    printf( "%s=%ld\n", name, rc );
    if (!rc) RegCloseKey( key );
}

static void reg_create( const char *name, const WCHAR *path, REGSAM sam )
{
    HKEY key;
    LONG rc = RegCreateKeyExW( HKEY_LOCAL_MACHINE, path, 0, NULL, 0, sam, NULL, &key, NULL );

    printf( "%s=%ld\n", name, rc );
    if (!rc) RegCloseKey( key );
}

int wmain( int argc, WCHAR **argv )
{
    setvbuf( stdout, NULL, _IONBF, 0 );   /* every line out at once, so a crash still shows how far it got */
    if (argc > 1 && !wcscmp( argv[1], L"reg" ))
    {
        reg_open( "reg_software", L"Software", KEY_READ );
        reg_open( "reg_microsoft", L"Software\\Microsoft", KEY_READ );
        reg_open( "reg_root_view", L"Software\\Microsoft\\SystemCertificates\\Root", KEY_READ );
        reg_open( "reg_root_64", L"Software\\Microsoft\\SystemCertificates\\Root", KEY_READ | KEY_WOW64_64KEY );
        reg_open( "reg_root_32", L"Software\\Microsoft\\SystemCertificates\\Root", KEY_READ | KEY_WOW64_32KEY );
        reg_create( "reg_create_root_64", L"Software\\Microsoft\\SystemCertificates\\Root", KEY_READ | KEY_WOW64_64KEY );
        reg_open( "reg_policies_64", L"Software\\Policies\\Microsoft\\SystemCertificates", KEY_READ | KEY_WOW64_64KEY );
        {
            /* what CRYPT_RegOpenStore does next with the key: duplicate it, then watch it for changes */
            HKEY key, dup;
            HANDLE event;
            LONG rc = RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\SystemCertificates\\Root", 0,
                                     KEY_READ | KEY_WOW64_64KEY, &key );

            printf( "reg_open_for_dup=%ld\n", rc );
            if (!rc)
            {
                BOOL ok = DuplicateHandle( GetCurrentProcess(), key, GetCurrentProcess(), (HANDLE *)&dup, KEY_READ, TRUE, 0 );
                printf( "reg_duplicate=%s %lu\n", ok ? "ok" : "error", ok ? 0 : GetLastError() );
                if (ok)
                {
                    event = CreateEventW( NULL, FALSE, FALSE, NULL );
                    rc = RegNotifyChangeKeyValue( dup, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, event, TRUE );
                    printf( "reg_notify=%ld\n", rc );
                    CloseHandle( event );
                    RegCloseKey( dup );
                }
                RegCloseKey( key );
            }
        }
        return 0;
    }
    report( "machine_root", CertOpenStore( CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root" ) );
    report( "machine_root_ro", CertOpenStore( CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                              CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG, L"Root" ) );
    report( "user_root", CertOpenSystemStoreW( 0, L"Root" ) );
    return 0;
}
