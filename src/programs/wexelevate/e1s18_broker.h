/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * E1-S18: Broker Wine-Only Integration Header.
 *
 * Implements the separate standard-user token integration branch for
 * wexelevate, orchestrating S17 prompt queue, S8 evidence redemption,
 * S10 server token issuance, S13/S14 profile lease, S15 launch prep,
 * suspended child creation, limited handle duplication, and guarded resume.
 */
#ifndef WEXELEVATE_E1S18_BROKER_H
#define WEXELEVATE_E1S18_BROKER_H

#include <stddef.h>
#include <stdint.h>

#include "wexelevate_request.h"

#ifndef _WIN32
#include <sys/types.h>

typedef struct _PROCESS_INFORMATION
{
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} PROCESS_INFORMATION;

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

/* Win32 Error codes used in broker integration */
#define ERROR_SUCCESS                     0
#define ERROR_FILE_NOT_FOUND              2
#define ERROR_ACCESS_DENIED               5
#define ERROR_GEN_FAILURE                 31
#define ERROR_NOT_SUPPORTED               50
#define ERROR_INVALID_PARAMETER           87
#define ERROR_BUSY                        170
#define ERROR_OPERATION_ABORTED           995
#define ERROR_CANCELLED                   1223
#define ERROR_FILE_CORRUPT                1392
#define ERROR_TIMEOUT                     1460

#define SYNCHRONIZE                       0x00100000
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define DUPLICATE_CLOSE_SOURCE            0x00000001

#define WEXELEVATE_WANT_HANDLE            0x1
#define WEXELEVATE_ENV_INHERIT            0x2
#define WEXELEVATE_FLAGS_MASK             0x3

struct wexelevate_reply
{
    DWORD magic;
    DWORD error;
    DWORD pid;
    DWORD tid;
    ULONGLONG handle;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * S1 Contract Negotiated Capability Bits (E1_S1_CONTRACTS.md)
 */
#define E1S1_CAP_H2_OPENER          0x0001ULL /* Bit 0: Retained opener process/token */
#define E1S1_CAP_MESSAGE_PROVENANCE 0x0002ULL /* Bit 1: Consumed-message writer provenance */
#define E1S1_CAP_BINDING_V1         0x0004ULL /* Bit 2: Caller-to-answer binding */
#define E1S1_CAP_DIALOG_V1          0x0008ULL /* Bit 3: Credential dialog child & channel */
#define E1S1_CAP_VERIFIER_V1        0x0010ULL /* Bit 4: Native PAM verifier */
#define E1S1_CAP_EVIDENCE_V1        0x0020ULL /* Bit 5: Single-use connection-owned evidence */
#define E1S1_CAP_ACCOUNT_MAP_V1     0x0040ULL /* Bit 6: S4 accounts map authority */
#define E1S1_CAP_TOKEN_V1           0x0080ULL /* Bit 7: S10 account token issuance */
#define E1S1_CAP_PROFILE_V1         0x0100ULL /* Bit 8: S13/S14 profile lease & HKU */
#define E1S1_CAP_COMMIT_GUARD_V1    0x0200ULL /* Bit 9: Guarded resume and commit */

#define E1S1_ALL_REQUIRED_CAPS      0x03ffULL /* All 10 capabilities required */

/*
 * Pluggable Operations Table for Test Injection / Mocking.
 * In production/Wine, default ops invoke the actual server/subsystem routines.
 * In unit testing, injected fakes test every stage and failure rollback.
 */
struct e1s18_ops
{
    /* Capability query */
    uint64_t (*get_capabilities)( const struct request *req );

    /* Runtime policy query */
    BOOL (*is_policy_enabled)( void );

    /* S17 Prompt Queue & Dialog */
    DWORD (*prompt_dialog)( struct request *req, const uint8_t nonce[32],
                            const uint8_t binding_digest[32], const uint8_t request_digest[32],
                            char *out_account, size_t account_max );

    /* S8 Evidence Redemption */
    DWORD (*redeem_evidence)( struct request *req, const char *account_name,
                              const uint8_t nonce[32], const uint8_t binding_digest[32],
                              const uint8_t request_digest[32],
                              uint8_t out_account_key[16], uint8_t out_evidence_ref[16] );

    /* S10 Token Issuance */
    DWORD (*issue_token)( struct request *req, const uint8_t account_key[16],
                          const uint8_t evidence_ref[16], const uint8_t nonce[32],
                          const uint8_t binding_digest[32], const uint8_t request_digest[32],
                          HANDLE *out_token );

    /* S13/S14 Profile Load & Lease Acquisition */
    DWORD (*load_profile)( struct request *req, HANDLE token, const uint8_t account_key[16],
                           uint64_t *out_lease_id, HANDLE *out_hkey );

    /* S15 Launch Preparation */
    DWORD (*prep_launch)( struct request *req, HANDLE token,
                          char **out_win_env, size_t *out_win_env_sz,
                          char ***out_unix_env, size_t *out_unix_env_cnt,
                          char **out_cwd );

    /* Image Re-verification before create */
    DWORD (*verify_image)( struct request *req );

    /* Process Creation Suspended */
    DWORD (*create_process_suspended)( struct request *req, HANDLE token,
                                       const char *win_env, size_t win_env_sz,
                                       const char *cwd,
                                       PROCESS_INFORMATION *out_pi );

    /* Installed Child Process & Token Identity Verification */
    DWORD (*verify_child)( struct request *req, HANDLE token, const PROCESS_INFORMATION *pi );

    /* Lease Arming on child PID */
    DWORD (*arm_lease)( UINT64 lease, DWORD pid );

    /* Handle Duplication */
    DWORD (*duplicate_handle)( struct request *req, HANDLE process, HANDLE *out_remote );

    /* Lease Transfer to committed process tracking */
    DWORD (*transfer_lease)( struct request *req, uint64_t lease_id, HANDLE process );

    /* Pre-resume Checks (binding, stop_event, lease_event) */
    DWORD (*check_pre_resume)( struct request *req );

    /* Guarded Resume */
    DWORD (*resume_thread)( struct request *req, HANDLE thread );

    /* Cleanup / Rollback Hooks */
    void (*terminate_child)( HANDLE process );
    void (*close_remote_handle)( HANDLE caller, HANDLE remote );
    void (*unload_profile)( uint64_t lease_id, HANDLE hkey );
    void (*close_handle)( HANDLE handle );
    void (*free_buffer)( void *ptr );
};

/* Core Broker Integration APIs */
BOOL e1s18_policy_enabled( void );
BOOL e1s18_check_capabilities( const struct request *req );
DWORD e1s18_handle_standard_request( struct request *req, struct wexelevate_reply *reply );

/* Operations override for testing */
void e1s18_set_ops( const struct e1s18_ops *ops );
void e1s18_reset_ops( void );
const struct e1s18_ops *e1s18_get_ops( void );

#ifdef __cplusplus
}
#endif

#endif /* WEXELEVATE_E1S18_BROKER_H */
