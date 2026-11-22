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

int wmain( void )
{
    report( "machine_root", CertOpenStore( CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root" ) );
    report( "machine_root_ro", CertOpenStore( CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                              CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG, L"Root" ) );
    report( "user_root", CertOpenSystemStoreW( 0, L"Root" ) );
    return 0;
}
