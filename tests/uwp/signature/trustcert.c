/*
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
 * version. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adds a certificate file to the Root store of the current user, so that a package signed with it is trusted in a test
 * prefix. Only for a lab prefix; the test certificate is never added to a Windows host. usage: trustcert FILE.cer
 */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

int wmain( int argc, WCHAR **argv )
{
    HANDLE file;
    BYTE data[8192];
    DWORD size = 0;
    HCERTSTORE store;
    BOOL ok;

    if (argc != 2) return 2;
    file = CreateFileW( argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE || !ReadFile( file, data, sizeof(data), &size, NULL ) || !size)
    {
        fprintf( stderr, "cannot read the certificate\n" );
        return 1;
    }
    CloseHandle( file );
    if (!(store = CertOpenStore( CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"Root" )))
    {
        fprintf( stderr, "cannot open the store (%lu)\n", GetLastError() );
        return 1;
    }
    ok = CertAddEncodedCertificateToStore( store, X509_ASN_ENCODING, data, size, CERT_STORE_ADD_REPLACE_EXISTING, NULL );
    CertCloseStore( store, 0 );
    if (!ok) fprintf( stderr, "cannot add the certificate (%lu)\n", GetLastError() );
    return ok ? 0 : 1;
}
