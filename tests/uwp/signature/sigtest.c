/*
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
 * version. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What WinVerifyTrust says about each package given on the command line, and the name of the signer when it found one.
 * The same program runs on Windows (the oracle) and on Wine; the lines are compared with expected-windows.txt.
 * usage: sigtest FILE...
 */
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <stdio.h>
#include <wchar.h>

static const WCHAR *base_name( const WCHAR *path )
{
    const WCHAR *slash = wcsrchr( path, '\\' ), *fwd = wcsrchr( path, '/' );
    if (fwd && (!slash || fwd > slash)) slash = fwd;
    return slash ? slash + 1 : path;
}

int wmain( int argc, WCHAR **argv )
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    BOOL revoke = argc > 1 && !wcscmp( argv[1], L"--revoke" );   /* check the whole chain for revocation */
    int i;

    for (i = revoke ? 2 : 1; i < argc; i++)
    {
        WINTRUST_FILE_INFO file = { sizeof(file) };
        WINTRUST_DATA data = { sizeof(data) };
        WCHAR signer[256] = L"";
        LONG status;

        file.pcwszFilePath = argv[i];
        data.dwUIChoice = WTD_UI_NONE;
        data.fdwRevocationChecks = revoke ? WTD_REVOKE_WHOLECHAIN : WTD_REVOKE_NONE;
        data.dwUnionChoice = WTD_CHOICE_FILE;
        data.pFile = &file;
        data.dwStateAction = WTD_STATEACTION_VERIFY;
        data.dwProvFlags = revoke ? WTD_REVOCATION_CHECK_CHAIN : WTD_REVOCATION_CHECK_NONE;
        status = WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
        if (data.hWVTStateData)
        {
            CRYPT_PROVIDER_DATA *prov = WTHelperProvDataFromStateData( data.hWVTStateData );
            CRYPT_PROVIDER_SGNR *sgnr = prov ? WTHelperGetProvSignerFromChain( prov, 0, FALSE, 0 ) : NULL;
            CRYPT_PROVIDER_CERT *cert = sgnr ? WTHelperGetProvCertFromChain( sgnr, 0 ) : NULL;

            if (cert && cert->pCert) CertGetNameStringW( cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, signer, ARRAY_SIZE(signer) );
        }
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
        wprintf( L"%ls: %08lx%ls%ls\n", base_name( argv[i] ), (ULONG)status, signer[0] ? L" signer " : L"", signer );
    }
    return 0;
}
