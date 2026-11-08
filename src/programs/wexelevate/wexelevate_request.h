/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Elevation Broker Request Structure definition.
 * Shared between main.c and e1s18_broker.h/.c.
 */
#ifndef WEXELEVATE_REQUEST_H
#define WEXELEVATE_REQUEST_H

#ifdef _WIN32
#include <windows.h>
#include "wexelevate.h"
#else
#include <stdint.h>
#ifndef WINAPI
#define WINAPI
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
typedef int BOOL;
typedef uint32_t DWORD;
typedef uint64_t ULONGLONG;
typedef uint64_t UINT64;
typedef uint8_t  BYTE;
typedef uint16_t WCHAR;
typedef void *HANDLE;

typedef union _LARGE_INTEGER {
    struct {
        uint32_t LowPart;
        int32_t  HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER;

typedef struct _LUID {
    uint32_t LowPart;
    int32_t  HighPart;
} LUID;

typedef struct _TOKEN_STATISTICS {
    LUID TokenId;
    LUID AuthenticationId;
    int64_t ExpirationTime;
    uint32_t TokenType;
    uint32_t ImpersonationLevel;
    uint32_t DynamicCharged;
    uint32_t DynamicAvailable;
    uint32_t GroupCount;
    uint32_t PrivilegeCount;
    LUID ModifiedId;
} TOKEN_STATISTICS;

typedef struct _BY_HANDLE_FILE_INFORMATION {
    uint32_t dwFileAttributes;
    uint64_t ftCreationTime;
    uint64_t ftLastAccessTime;
    uint64_t ftLastWriteTime;
    uint32_t dwVolumeSerialNumber;
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t nNumberOfLinks;
    uint32_t nFileIndexHigh;
    uint32_t nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;
#endif

#ifndef SECURITY_MAX_SID_SIZE
#define SECURITY_MAX_SID_SIZE 68
#endif

struct request
{
    ULONGLONG id;
    HANDLE pipe;
    DWORD caller_pid;
    HANDLE caller;        /* process handle of the caller */
    HANDLE caller_token;  /* its primary token */
    TOKEN_STATISTICS caller_stats; /* frozen H2/S3 security version and caller logon */
    LARGE_INTEGER caller_start;
    DWORD caller_session, caller_sid_len;
    BYTE caller_sid[SECURITY_MAX_SID_SIZE];
    DWORD flags, show;
    WCHAR *image, *cmdline, *cwd, *env;
    DWORD env_len;
    HANDLE image_file;    /* held open with FILE_SHARE_READ from the freeze to the launch */
    UINT64 lease;         /* read lease on the image (0: none needed), see lease_* below */
    HANDLE lease_event;   /* set when the lease broke */
    BY_HANDLE_FILE_INFORMATION info;
    BYTE hash[32];
    int live;             /* in the table: the id can be used once */
    DWORD admitted_pid;   /* R12 per-caller admission, client PID of the pipe at accept */
#ifdef WEXELEVATE_E1S18
    BOOL is_standard_user;
    HANDLE s10_context;
    HANDLE issued_token;
    BYTE alice_sid[SECURITY_MAX_SID_SIZE];
    DWORD alice_sid_len;
    char account_name[64];
    BYTE account_key[16];
    ULONGLONG map_generation;
    ULONGLONG storage_generation;
#endif
};

#endif /* WEXELEVATE_REQUEST_H */
