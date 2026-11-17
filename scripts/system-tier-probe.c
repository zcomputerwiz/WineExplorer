/*
 * WineExplorer Desktop Environment - probe for the system tier's machine registry (session 0)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Usage (KEY is relative to HKEY_LOCAL_MACHINE):
 *   probe hold KEY VALUE MARKER   reads VALUE, waits until the file MARKER exists, reads VALUE again through the
 *                                 same handle and creates the subkey "Held" under it; prints first=, second=, sub=
 *   probe dup KEY VALUE           reads VALUE through a duplicate of the key handle; prints dup=
 *   probe name KEY                prints name= (NtQueryObject) and sd= (size of the key's security descriptor)
 *   probe notify KEY MS           waits up to MS ms for a change below KEY; prints notify=changed or notify=timeout
 *   probe setdacl KEY SDDL        sets the key's DACL from an SDDL string; prints setdacl=<error code>
 *   probe getdacl KEY             prints dacl=<the key's DACL as SDDL>
 *   probe whoami                  prints user=<the token user's SID> and session=<the process's session id>
 *   probe join                    prints join=<NetGetJoinInformation's type> name=<name> and role=<the machine role
 *                                 of DsRoleGetPrimaryDomainInformation> flat=<name> dns=<name>
 *   probe gpnotify machine|user MS  registers for the policy notification (RegisterGPNotification), prints
 *                                 gp=ready, waits up to MS ms and prints gp=signalled or gp=timeout
 *   probe token                   the process token: user=, primary=, elevation=, one group= line per group (attrs=); for a
 *                                 split token also linkedelevation= and linkedgroup= lines of the linked token
 *   probe env NAME                prints env=<the variable's value> or env=error<n>
 *   probe names                   lsa=DOMAIN\name (LookupAccountSidW of the caller) and samcompat= (GetUserNameExW)
 *   probe gpolist machine|user {EXT}  the applied objects for the extension (GetAppliedGPOListW; user: the
 *                                 caller's SID); prints gpolist=<error> and one gpo= line per object
 *   probe rpcbind PIPE            opens \\.\pipe\PIPE in message mode, sends a DCE/RPC bind for the service control
 *                                 manager's interface and prints pipe=open (or pipe=error<n>) and bind=<the answer's
 *                                 packet type: 12 bind_ack, 13 bind_nak>
 *   probe pipecreate PIPE         tries to create \\.\pipe\PIPE; prints create=ok or create=error<n>
 *   probe overwrite FILE TEXT     CreateFileW(CREATE_ALWAYS) and writes TEXT; prints overwrite=ok or overwrite=error<n>
 *   probe readfile FILE           prints read=<the file's first bytes> or read=error<n>
 *   probe enumsvc NAME            lists the Win32 services (EnumServicesStatusExW); prints count=<entries named NAME>
 *                                 and total=<entries>
 *   probe svcstate NAME           opens the service for status only; prints state=<dwCurrentState> or state=error<n>
 *   probe svcstop NAME            opens the service for SERVICE_STOP and stops it; prints stop=0 or stop=error<n>
 *   probe gpset ID STATE N        sets a policy of the editor (gpedit.dll, GpEdit_SetPolicyW) as the editor saves it;
 *                                 prints gpset=0 or gpset=<win32 error>
 *   probe gpsetuser OBJECT ID STATE N
 *                                 the same for a local object of the users (GpEdit_SetUserPolicyW; OBJECT is
 *                                 Administrators, Non-Administrators or a user); prints gpset=0 or gpset=<win32 error>

 *   probe spoolsteps              the file steps of the policy store client in the spool, with each error (diagnostics)
 *   probe runas FILE PARAMS       ShellExecuteEx "runas" FILE with PARAMS, waits; prints runas=0 or runas=error<n>
 *   probe svcctl NAME CODE      sends the user-defined control code CODE (128 to 255) to the service; prints svcctl=0
 *                                 or svcctl=error<n>
 *   probe pipeserve PIPE          serves \\.\pipe\PIPE for one client (message mode, 30 s at most); prints
 *                                 clientsession=<GetNamedPipeClientSessionId> and message=<what the client wrote>
 *   probe pipesay PIPE TEXT       opens \\.\pipe\PIPE and writes TEXT; prints say=ok or say=error<n>
 *   probe pipehold PIPE SECONDS   opens \\.\pipe\PIPE for read/write, holds for SECONDS; prints hold=ok or hold=error<n>
 *   probe pipewait PIPE           WaitNamedPipe(\\.\pipe\PIPE, 2 s); prints wait=ok or wait=error<n>, and wait_ms
 * Build: x86_64-w64-mingw32-gcc / i686-w64-mingw32-gcc -municode -o probe.exe system-tier-probe.c -ladvapi32 -lntdll */

#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <shellapi.h>
#include <stdio.h>

static void print_value( const char *label, HKEY key, const WCHAR *name )
{
    WCHAR data[256];
    DWORD size = sizeof(data), type;
    LSTATUS ret = RegQueryValueExW( key, name, NULL, &type, (BYTE *)data, &size );

    if (ret) printf( "%s=error%ld\n", label, ret );
    else printf( "%s=%ls\n", label, data );
    fflush( stdout );
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    HKEY key, sub;
    LSTATUS ret;

    if (argc == 2 && !lstrcmpW( argv[1], L"whoami" ))
    {
        char buffer[256];
        TOKEN_USER *user = (TOKEN_USER *)buffer;
        HANDLE token;
        DWORD size;
        WCHAR *str;

        if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ||
            !GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ) ||
            !ConvertSidToStringSidW( user->User.Sid, &str ))
        {
            printf( "user=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "user=%ls\n", str );
        LocalFree( str );
        if (ProcessIdToSessionId( GetCurrentProcessId(), &size )) printf( "session=%lu\n", size );
        return 0;
    }
    if (argc == 2 && !lstrcmpW( argv[1], L"join" ))
    {
        /* DSROLE_PRIMARY_DOMAIN_INFO_BASIC, written out: the probe builds without the header */
        struct basic { int role; ULONG flags; WCHAR *flat, *dns, *forest; GUID guid; } *basic = NULL;
        DWORD (WINAPI *get_join)( const WCHAR *, WCHAR **, int * );
        DWORD (WINAPI *get_role)( const WCHAR *, int, BYTE ** );
        HMODULE netapi = LoadLibraryW( L"netapi32.dll" ), dsrole = LoadLibraryW( L"dsrole.dll" );
        WCHAR *name = NULL;
        int type = -1;
        DWORD err;

        get_join = netapi ? (void *)GetProcAddress( netapi, "NetGetJoinInformation" ) : NULL;
        get_role = dsrole ? (void *)GetProcAddress( dsrole, "DsRoleGetPrimaryDomainInformation" ) : NULL;
        if (!get_join || !get_role) { printf( "join=noentry\n" ); return 1; }
        err = get_join( NULL, &name, &type );
        printf( "join=%d name=%ls error=%lu\n", type, name ? name : L"", err );
        err = get_role( NULL, 1, (BYTE **)&basic );
        if (err || !basic) printf( "role=error%lu\n", err );
        else printf( "role=%d flat=%ls dns=%ls\n", basic->role, basic->flat ? basic->flat : L"",
                     basic->dns ? basic->dns : L"" );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"gpnotify" ))
    {
        BOOL (WINAPI *reg_notify)( HANDLE, BOOL );
        HMODULE userenv = LoadLibraryW( L"userenv.dll" );
        HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
        DWORD wait;

        reg_notify = userenv ? (void *)GetProcAddress( userenv, "RegisterGPNotification" ) : NULL;
        if (!reg_notify || !reg_notify( event, !lstrcmpW( argv[2], L"machine" ) ))
        {
            printf( "gp=error%lu\n", GetLastError() );
            return 1;
        }
        Sleep( 1000 );  /* the notification thread has its watches by now */
        printf( "gp=ready\n" );
        fflush( stdout );
        wait = WaitForSingleObject( event, wcstoul( argv[3], NULL, 10 ) );
        printf( wait == WAIT_OBJECT_0 ? "gp=signalled\n" : "gp=timeout\n" );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"env" ))
    {
        /* one variable of the process environment (USERDOMAIN comes from the user's Volatile Environment) */
        WCHAR value[1024];
        if (GetEnvironmentVariableW( argv[2], value, sizeof(value) / sizeof(value[0]) )) printf( "env=%ls\n", value );
        else printf( "env=error%lu\n", GetLastError() );
        return 0;
    }
    if (argc == 2 && !lstrcmpW( argv[1], L"token" ))
    {
        /* the process token: user, primary group, elevation type, and every group with its attributes */
        static BYTE buffer[65536];
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)buffer;
        HANDLE token;
        DWORD size, i, type;
        WCHAR *str;

        if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) { printf( "token=error%lu\n", GetLastError() ); return 1; }
        if (GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ) &&
            ConvertSidToStringSidW( ((TOKEN_USER *)buffer)->User.Sid, &str )) { printf( "user=%ls\n", str ); LocalFree( str ); }
        if (GetTokenInformation( token, TokenPrimaryGroup, buffer, sizeof(buffer), &size ) &&
            ConvertSidToStringSidW( ((TOKEN_PRIMARY_GROUP *)buffer)->PrimaryGroup, &str )) { printf( "primary=%ls\n", str ); LocalFree( str ); }
        if (GetTokenInformation( token, TokenElevationType, &type, sizeof(type), &size )) printf( "elevation=%lu\n", type );
        if (GetTokenInformation( token, TokenGroups, buffer, sizeof(buffer), &size ))
            for (i = 0; i < groups->GroupCount; i++)
                if (ConvertSidToStringSidW( groups->Groups[i].Sid, &str ))
                {
                    printf( "group=%ls attrs=%lx\n", str, groups->Groups[i].Attributes );
                    LocalFree( str );
                }
        /* a split token's other half (the full token of a limited one): linkedelevation= and linkedgroup= lines */
        if (GetTokenInformation( token, TokenLinkedToken, buffer, sizeof(buffer), &size ))
        {
            HANDLE linked = ((TOKEN_LINKED_TOKEN *)buffer)->LinkedToken;

            if (GetTokenInformation( linked, TokenElevationType, &type, sizeof(type), &size )) printf( "linkedelevation=%lu\n", type );
            if (GetTokenInformation( linked, TokenGroups, buffer, sizeof(buffer), &size ))
                for (i = 0; i < groups->GroupCount; i++)
                    if (ConvertSidToStringSidW( groups->Groups[i].Sid, &str ))
                    {
                        printf( "linkedgroup=%ls attrs=%lx\n", str, groups->Groups[i].Attributes );
                        LocalFree( str );
                    }
            CloseHandle( linked );
        }
        CloseHandle( token );
        return 0;
    }
    if ((argc == 2 || argc == 3) && !lstrcmpW( argv[1], L"names" ))
    {
        /* the caller as LookupAccountSidW names it, and GetUserNameExW(NameSamCompatible) (secur32) */
        BOOLEAN (WINAPI *get_name_ex)( int, WCHAR *, ULONG * );
        char buf[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
        WCHAR account[256], domain[256], sam[512];
        DWORD alen = 256, dlen = 256, size;
        ULONG slen = 512;
        SID_NAME_USE use;
        HANDLE token;
        HMODULE secur32;

        if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ||
            !GetTokenInformation( token, TokenUser, buf, sizeof(buf), &size )) { printf( "names=error%lu\n", GetLastError() ); return 1; }
        if (LookupAccountSidW( NULL, ((TOKEN_USER *)buf)->User.Sid, account, &alen, domain, &dlen, &use ))
            printf( "lsa=%ls\\%ls use=%d\n", domain, account, use );
        else printf( "lsa=error%lu\n", GetLastError() );
        fflush( stdout );   /* what came before secur32 shows even if loading it does not return */
        /* "names lsa": LookupAccountSidW only (loading secur32 hangs in a session 0 setup, reported 2026-11-15) */
        if (argc == 3 && !lstrcmpW( argv[2], L"lsa" )) return 0;
        secur32 = LoadLibraryW( L"secur32.dll" );
        printf( "secur32=%s\n", secur32 ? "loaded" : "missing" );
        fflush( stdout );
        get_name_ex = secur32 ? (void *)GetProcAddress( secur32, "GetUserNameExW" ) : NULL;
        if (get_name_ex && get_name_ex( 2 /* NameSamCompatible */, sam, &slen )) printf( "samcompat=%ls\n", sam );
        else printf( "samcompat=error%lu\n", GetLastError() );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"gpolist" ))
    {
        /* GROUP_POLICY_OBJECTW, written out: the probe builds without the header */
        struct gpo { DWORD options, version; WCHAR *ds, *fs, *display, name[50]; int link; LPARAM lparam;
                     struct gpo *next, *prev; WCHAR *extensions; LPARAM lparam2; WCHAR *som; } *list = NULL, *g;
        DWORD (WINAPI *get_list)( DWORD, const WCHAR *, PSID, GUID *, struct gpo ** );
        BOOL (WINAPI *free_list)( struct gpo * );
        HMODULE userenv = LoadLibraryW( L"userenv.dll" );
        char buffer[256];
        TOKEN_USER *user = (TOKEN_USER *)buffer;
        HANDLE token;
        DWORD size, err;
        GUID ext;

        get_list = userenv ? (void *)GetProcAddress( userenv, "GetAppliedGPOListW" ) : NULL;
        free_list = userenv ? (void *)GetProcAddress( userenv, "FreeGPOListW" ) : NULL;
        unsigned int b[8], i;

        if (!get_list || !free_list ||
            swscanf( argv[3], L"{%8lx-%4hx-%4hx-%2x%2x-%2x%2x%2x%2x%2x%2x}", &ext.Data1, &ext.Data2, &ext.Data3,
                     &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7] ) != 11)
        {
            printf( "gpolist=noentry\n" );
            return 1;
        }
        for (i = 0; i < 8; i++) ext.Data4[i] = b[i];
        OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token );
        GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size );
        err = get_list( lstrcmpW( argv[2], L"machine" ) ? 0 : 1, NULL, user->User.Sid, &ext, &list );
        printf( "gpolist=%lu\n", err );
        for (g = list; g; g = g->next)
            printf( "gpo=%ls version=%lu link=%d name=%ls som=%ls\n", g->name, g->version, g->link, g->display, g->som );
        free_list( list );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"rpcbind" ))
    {
        static const GUID svcctl = { 0x367abb81, 0x9844, 0x35f1, { 0xad, 0x32, 0x98, 0xf0, 0x38, 0x00, 0x10, 0x03 } };
        static const GUID ndr = { 0x8a885d04, 0x1ceb, 0x11c9, { 0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60 } };
        unsigned char bind[72] = { 5, 0, 11, 3, 0x10, 0, 0, 0, 72, 0, 0, 0, 1, 0, 0, 0,
                                   0xb8, 0x10, 0xb8, 0x10, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0 };
        unsigned char answer[1024];
        WCHAR path[MAX_PATH];
        DWORD mode = PIPE_READMODE_MESSAGE, done;
        HANDLE pipe;

        memcpy( bind + 32, &svcctl, 16 );
        bind[48] = 2;                       /* interface version 2.0 */
        memcpy( bind + 52, &ndr, 16 );
        bind[68] = 2;                       /* NDR version 2 */
        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateFileW( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE)
        {
            printf( "pipe=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "pipe=open\n" );
        if (!SetNamedPipeHandleState( pipe, &mode, NULL, NULL )) printf( "mode=error%lu\n", GetLastError() );
        if (!WriteFile( pipe, bind, sizeof(bind), &done, NULL ) || done != sizeof(bind))
            printf( "bind=writeerror%lu\n", GetLastError() );
        else if (!ReadFile( pipe, answer, sizeof(answer), &done, NULL ) || done < 16)
            printf( "bind=readerror%lu\n", GetLastError() );
        else
            printf( "bind=%u size=%lu\n", answer[2], done );
        CloseHandle( pipe );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"enumsvc" ))
    {
        SC_HANDLE manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE );
        ENUM_SERVICE_STATUS_PROCESSW *services;
        DWORD needed = 0, count = 0, i, named = 0;
        BYTE *buffer;

        if (!manager)
        {
            printf( "enum=error%lu\n", GetLastError() );
            return 1;
        }
        EnumServicesStatusExW( manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &needed,
                               &count, NULL, NULL );
        buffer = HeapAlloc( GetProcessHeap(), 0, needed + 1 );
        if (!EnumServicesStatusExW( manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, buffer,
                                    needed + 1, &needed, &count, NULL, NULL ))
        {
            printf( "enum=error%lu\n", GetLastError() );
            return 1;
        }
        services = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
        for (i = 0; i < count; i++) if (!lstrcmpiW( services[i].lpServiceName, argv[2] )) named++;
        printf( "count=%lu\ntotal=%lu\n", named, count );
        CloseServiceHandle( manager );
        return 0;
    }
    if (argc == 5 && !lstrcmpW( argv[1], L"gpset" ))
    {
        /* a setting of the policy editor without its window: gpset ID STATE NUMBER (0 not configured, 1 enabled,
         * 2 disabled); prints gpset=0 or gpset=error<n> */
        DWORD (WINAPI *set_policy)( const WCHAR *, DWORD, DWORD );
        HMODULE module = LoadLibraryW( L"gpedit.dll" );

        set_policy = module ? (void *)GetProcAddress( module, "GpEdit_SetPolicyW" ) : NULL;
        if (!set_policy)
        {
            printf( "gpset=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "gpset=%lu\n", set_policy( argv[2], wcstoul( argv[3], NULL, 10 ), wcstoul( argv[4], NULL, 10 ) ) );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"secpolset" ))
    {
        /* a security setting of the local security editor without its window: secpolset NAME NUMBER; prints gpset=N */
        DWORD (WINAPI *set_setting)( const WCHAR *, DWORD );
        HMODULE module = LoadLibraryW( L"secpol.dll" );

        set_setting = module ? (void *)GetProcAddress( module, "SecPol_SetW" ) : NULL;
        if (!set_setting)
        {
            printf( "gpset=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "gpset=%lu\n", set_setting( argv[2], wcstoul( argv[3], NULL, 10 ) ) );
        return 0;
    }
    if (argc == 6 && !lstrcmpW( argv[1], L"gpsetuser" ))
    {
        DWORD (WINAPI *set_user_policy)( const WCHAR *, const WCHAR *, DWORD, DWORD );
        HMODULE module = LoadLibraryW( L"gpedit.dll" );

        set_user_policy = module ? (void *)GetProcAddress( module, "GpEdit_SetUserPolicyW" ) : NULL;
        if (!set_user_policy)
        {
            printf( "gpset=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "gpset=%lu\n", set_user_policy( argv[2], argv[3], wcstoul( argv[4], NULL, 10 ), wcstoul( argv[5], NULL, 10 ) ) );
        return 0;
    }
    if ((argc == 2 || argc == 3) && !lstrcmpW( argv[1], L"spoolsteps" ))
    {
        /* the file steps of GpStoreWrite in the spool (or in the directory DIR), one by one, with the error of each
         * (diagnostics) */
        WCHAR dir[MAX_PATH], temp[MAX_PATH + 32], final[MAX_PATH + 32];
        HANDLE file;
        DWORD done = 0, werr;

        if (argc == 3) lstrcpynW( dir, argv[2], MAX_PATH - 20 );
        else
        {
            GetSystemDirectoryW( dir, MAX_PATH - 20 );
            lstrcatW( dir, L"\\GroupPolicy\\Spool" );
        }
        /* one line: c create, w write, x close, o open for DELETE, r rename by handle (SetFileInformationByHandle), m
         * MoveFileEx (replace), n MoveFileEx without replace to another name, d delete of the new name, t delete of
         * the temporary name */
        DWORD c, w, x, o = 0, r = 0, m, n, d, t;
        char buffer[sizeof(FILE_RENAME_INFO) + 2 * MAX_PATH * sizeof(WCHAR)];
        FILE_RENAME_INFO *info = (FILE_RENAME_INFO *)buffer;
        HANDLE del;

        wsprintfW( temp, L"%s\\diag.tmp", dir );
        wsprintfW( final, L"%s\\diag", dir );
        file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
        c = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (file == INVALID_HANDLE_VALUE) { printf( "%ls c=%lu\n", dir, c ); return 1; }
        w = WriteFile( file, "x\n", 2, &done, NULL ) ? 0 : GetLastError();
        x = CloseHandle( file ) ? 0 : GetLastError();
        del = CreateFileW( temp, DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL );
        if (del == INVALID_HANDLE_VALUE) o = GetLastError();
        else
        {
            memset( buffer, 0, sizeof(buffer) );
            info->ReplaceIfExists = TRUE;
            info->FileNameLength = lstrlenW( final ) * sizeof(WCHAR);
            memcpy( info->FileName, final, info->FileNameLength );
            r = SetFileInformationByHandle( del, FileRenameInfo, info, sizeof(buffer) ) ? 0 : GetLastError();
            CloseHandle( del );
        }
        d = 0;
        if (!r) d = DeleteFileW( final ) ? 0 : GetLastError();
        file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
        if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
        m =MoveFileExW( temp, final, MOVEFILE_REPLACE_EXISTING ) ? 0 : GetLastError();
        if (!m) DeleteFileW( final );
        n = MoveFileExW( temp, final, 0 ) ? 0 : GetLastError();
        d = DeleteFileW( final ) ? 0 : GetLastError();
        t = DeleteFileW( temp ) ? 0 : GetLastError();
        {
            /* q: the rename by handle with the directory as RootDirectory and a relative name; k: CopyFile and the
             * delete of the temporary name; s: the status of a plain NtSetInformationFile by handle (rename) */
            DWORD q = 0, k, s = 0;
            HANDLE dirh;

            file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
            if (file != INVALID_HANDLE_VALUE) { WriteFile( file, "x\n", 2, &done, NULL ); CloseHandle( file ); }
            dirh = CreateFileW( dir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL );
            del = CreateFileW( temp, DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL );
            if (dirh == INVALID_HANDLE_VALUE || del == INVALID_HANDLE_VALUE) q = 1000000 + GetLastError();
            else
            {
                memset( buffer, 0, sizeof(buffer) );
                info->ReplaceIfExists = TRUE;
                info->RootDirectory = dirh;
                info->FileNameLength = 4 * sizeof(WCHAR);
                memcpy( info->FileName, L"diag", 4 * sizeof(WCHAR) );
                q = SetFileInformationByHandle( del, FileRenameInfo, info, sizeof(buffer) ) ? 0 : GetLastError();
            }
            if (del != INVALID_HANDLE_VALUE) CloseHandle( del );
            if (dirh != INVALID_HANDLE_VALUE) CloseHandle( dirh );
            if (!q) DeleteFileW( final );
            file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
            if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
            k = CopyFileW( temp, final, FALSE ) ? 0 : GetLastError();
            if (!k) DeleteFileW( final );
            DeleteFileW( temp );
            (void)s;
            printf( "%ls c=%lu w=%lu x=%lu o=%lu r=%lu m=%lu n=%lu d=%lu t=%lu q=%lu k=%lu\n", dir, c, w, x, o, r, m, n, d, t, q, k );
        }
        (void)werr;
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"runas" ))
    {
        /* runas FILE PARAMETERS: ShellExecuteEx with the verb runas, as a program asks for elevation; waits for the
         * program. Prints runas=0 (the program ran and ended) or runas=error<n> (a refusal is 1223, cancelled).
         * shell32 is loaded by name so that the probe's link line stays as it is. */
        BOOL (WINAPI *execute)( SHELLEXECUTEINFOW * );
        HMODULE shell = LoadLibraryW( L"shell32.dll" );
        SHELLEXECUTEINFOW info = { sizeof(info) };

        execute = shell ? (void *)GetProcAddress( shell, "ShellExecuteExW" ) : NULL;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"runas";
        info.lpFile = argv[2];
        info.lpParameters = argv[3];
        info.nShow = SW_HIDE;
        if (!execute || !execute( &info ))
        {
            printf( "runas=error%lu\n", GetLastError() );
            return 1;
        }
        if (info.hProcess) WaitForSingleObject( info.hProcess, 180000 );
        printf( "runas=0\n" );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"svcctl" ))
    {
        /* a user-defined control code (128 to 255) to a service, as a user program sends it */
        SC_HANDLE manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), service;
        SERVICE_STATUS status;
        DWORD code = wcstoul( argv[3], NULL, 10 );

        if (!manager || !(service = OpenServiceW( manager, argv[2], SERVICE_USER_DEFINED_CONTROL )))
        {
            printf( "svcctl=error%lu\n", GetLastError() );
            return 1;
        }
        if (ControlService( service, code, &status )) printf( "svcctl=0\n" );
        else printf( "svcctl=error%lu\n", GetLastError() );
        CloseServiceHandle( service );
        CloseServiceHandle( manager );
        return 0;
    }
    if (argc == 3 && (!lstrcmpW( argv[1], L"svcstate" ) || !lstrcmpW( argv[1], L"svcstop" )))
    {
        BOOL stop = !lstrcmpW( argv[1], L"svcstop" );
        SC_HANDLE manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ), service;
        SERVICE_STATUS status;

        if (!manager || !(service = OpenServiceW( manager, argv[2], stop ? SERVICE_STOP : SERVICE_QUERY_STATUS )))
        {
            printf( "%s=error%lu\n", stop ? "stop" : "state", GetLastError() );
            return 1;
        }
        if (stop)
        {
            if (ControlService( service, SERVICE_CONTROL_STOP, &status )) printf( "stop=0\n" );
            else printf( "stop=error%lu\n", GetLastError() );
        }
        else if (QueryServiceStatus( service, &status )) printf( "state=%lu\n", status.dwCurrentState );
        else printf( "state=error%lu\n", GetLastError() );
        CloseServiceHandle( service );
        CloseServiceHandle( manager );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"pipeserve" ))
    {
        WCHAR path[MAX_PATH];
        char text[256];
        OVERLAPPED ov = { 0 };
        ULONG session = ~0u;
        DWORD done = 0;
        HANDLE pipe;

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateNamedPipeW( path, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1, 4096, 4096, 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE)
        {
            printf( "serve=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "listening\n" );
        fflush( stdout );
        ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
        if (!ConnectNamedPipe( pipe, &ov ) && GetLastError() == ERROR_IO_PENDING &&
            WaitForSingleObject( ov.hEvent, 30000 ))
        {
            printf( "serve=timeout\n" );
            return 1;
        }
        if (!GetNamedPipeClientSessionId( pipe, &session )) printf( "clientsession=error%lu\n", GetLastError() );
        else printf( "clientsession=%lu\n", session );
        ResetEvent( ov.hEvent );
        if (!ReadFile( pipe, text, sizeof(text) - 1, NULL, &ov ) && GetLastError() == ERROR_IO_PENDING)
            WaitForSingleObject( ov.hEvent, 10000 );
        GetOverlappedResult( pipe, &ov, &done, FALSE );
        text[done < sizeof(text) ? done : sizeof(text) - 1] = 0;
        printf( "message=%s\n", text );
        CloseHandle( pipe );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"pipehold" ))
    {
        WCHAR path[MAX_PATH];
        HANDLE pipe;
        int seconds = _wtoi( argv[3] );

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateFileW( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE)
        {
            printf( "hold=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "hold=ok\n" );
        fflush( stdout );
        if (seconds > 0) Sleep( seconds * 1000 );
        CloseHandle( pipe );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"pipesay" ))
    {
        WCHAR path[MAX_PATH];
        char text[256];
        DWORD mode = PIPE_READMODE_MESSAGE, done;
        HANDLE pipe;
        int len = WideCharToMultiByte( CP_UTF8, 0, argv[3], -1, text, sizeof(text), NULL, NULL );

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateFileW( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE)
        {
            printf( "say=error%lu\n", GetLastError() );
            return 1;
        }
        SetNamedPipeHandleState( pipe, &mode, NULL, NULL );
        if (len > 1 && WriteFile( pipe, text, len - 1, &done, NULL )) printf( "say=ok\n" );
        else printf( "say=error%lu\n", GetLastError() );
        Sleep( 500 );
        CloseHandle( pipe );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"pipewait" ))
    {
        /* WaitNamedPipe with a timeout of 2 s: a missing pipe is not found at once (error 2), as on Windows; an RPC
         * client's connect loop spins for ever when a wait succeeds for a pipe that cannot be opened */
        WCHAR path[MAX_PATH];
        DWORD start = GetTickCount();

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        if (WaitNamedPipeW( path, 2000 )) printf( "wait=ok\n" );
        else printf( "wait=error%lu\n", GetLastError() );
        printf( "wait_ms=%lu\n", GetTickCount() - start );
        return 0;
    }
    if (argc == 4 && !lstrcmpW( argv[1], L"overwrite" ))
    {
        /* B20: CREATE_ALWAYS over an existing file, as a setup replacing a font */
        HANDLE file = CreateFileW( argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
        char text[256];
        DWORD written = 0;
        int len;

        if (file == INVALID_HANDLE_VALUE)
        {
            printf( "overwrite=error%lu\n", GetLastError() );
            return 0;
        }
        len = WideCharToMultiByte( CP_UTF8, 0, argv[3], -1, text, sizeof(text), NULL, NULL );
        printf( len > 0 && WriteFile( file, text, len - 1, &written, NULL ) ? "overwrite=ok\n" : "overwrite=writefailed\n" );
        CloseHandle( file );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"readfile" ))
    {
        HANDLE file = CreateFileW( argv[2], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        char text[256];
        DWORD got = 0;

        if (file == INVALID_HANDLE_VALUE)
        {
            printf( "read=error%lu\n", GetLastError() );
            return 0;
        }
        if (!ReadFile( file, text, sizeof(text) - 1, &got, NULL )) got = 0;
        text[got] = 0;
        printf( "read=%s\n", text );
        CloseHandle( file );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"pipecreate" ))
    {
        WCHAR path[MAX_PATH];
        HANDLE pipe;

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateNamedPipeW( path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1, 4096, 4096,
                                 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE) printf( "create=error%lu\n", GetLastError() );
        else
        {
            printf( "create=ok\n" );
            CloseHandle( pipe );
        }
        return 0;
    }
    if (argc < 3) return 2;
    if ((ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, argv[2], 0, KEY_ALL_ACCESS & ~WRITE_DAC & ~WRITE_OWNER, &key )) &&
        (ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, argv[2], 0, KEY_READ | KEY_NOTIFY, &key )))
    {
        printf( "open=error%ld\n", ret );
        return 1;
    }

    if (!lstrcmpW( argv[1], L"hold" ) && argc == 5)
    {
        int i;
        print_value( "first", key, argv[3] );
        for (i = 0; i < 1200 && GetFileAttributesW( argv[4] ) == INVALID_FILE_ATTRIBUTES; i++) Sleep( 100 );
        print_value( "second", key, argv[3] );
        ret = RegCreateKeyExW( key, L"Held", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL );
        printf( "sub=%ld\n", ret );
        if (!ret) RegCloseKey( sub );
    }
    else if (!lstrcmpW( argv[1], L"dup" ) && argc == 4)
    {
        HANDLE copy;
        if (!DuplicateHandle( GetCurrentProcess(), key, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS ))
            printf( "dup=error%lu\n", GetLastError() );
        else
        {
            print_value( "dup", (HKEY)copy, argv[3] );
            RegCloseKey( (HKEY)copy );
        }
    }
    else if (!lstrcmpW( argv[1], L"name" ))
    {
        char buffer[1024];
        OBJECT_NAME_INFORMATION *info = (OBJECT_NAME_INFORMATION *)buffer;
        ULONG len;
        DWORD size = 0;
        NTSTATUS status = NtQueryObject( key, ObjectNameInformation, info, sizeof(buffer) - sizeof(WCHAR), &len );

        if (status) printf( "name=error%08lx\n", status );
        else printf( "name=%.*ls\n", (int)(info->Name.Length / sizeof(WCHAR)), info->Name.Buffer );
        ret = RegGetKeySecurity( key, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, &size );
        printf( "sd=%lu ret=%ld\n", size, ret );
    }
    else if (!lstrcmpW( argv[1], L"setdacl" ) && argc == 4)
    {
        PSECURITY_DESCRIPTOR sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( argv[3], SDDL_REVISION_1, &sd, NULL ))
            printf( "setdacl=error%lu\n", GetLastError() );
        else
        {
            printf( "setdacl=%ld\n", RegSetKeySecurity( key, DACL_SECURITY_INFORMATION, sd ));
            LocalFree( sd );
        }
    }
    else if (!lstrcmpW( argv[1], L"getdacl" ))
    {
        char buffer[4096];
        DWORD size = sizeof(buffer);
        WCHAR *sddl;
        ret = RegGetKeySecurity( key, DACL_SECURITY_INFORMATION, buffer, &size );
        if (ret) printf( "dacl=error%ld\n", ret );
        else if (!ConvertSecurityDescriptorToStringSecurityDescriptorW( buffer, SDDL_REVISION_1, DACL_SECURITY_INFORMATION,
                                                                       &sddl, NULL ))
            printf( "dacl=error%lu\n", GetLastError() );
        else
        {
            printf( "dacl=%ls\n", sddl );
            LocalFree( sddl );
        }
    }
    else if (!lstrcmpW( argv[1], L"notify" ) && argc == 4)
    {
        HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
        ret = RegNotifyChangeKeyValue( key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, event, TRUE );
        if (ret) printf( "notify=error%ld\n", ret );
        else
        {
            printf( "armed\n" );
            fflush( stdout );
            printf( "notify=%s\n", WaitForSingleObject( event, _wtoi( argv[3] )) == WAIT_OBJECT_0 ? "changed" : "timeout" );
        }
    }
    else return 2;
    RegCloseKey( key );
    return 0;
}
