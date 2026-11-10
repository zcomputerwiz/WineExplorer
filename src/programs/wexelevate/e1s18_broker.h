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
#include "cred_dialog.h"
#include "e1s6_protocol.h"
#include "../../../wine/include/wine/wex_token.h"
#include "../../../wine/include/wine/wex_profile.h"

#ifndef _WIN32
#include <sys/types.h>

typedef int32_t  NTSTATUS;
typedef uint32_t ACCESS_MASK;
typedef uint32_t ULONG;

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
#define ERROR_INVALID_DATA                13
#define ERROR_GEN_FAILURE                 31
#define ERROR_NOT_SUPPORTED               50
#define ERROR_INVALID_PARAMETER           87
#define ERROR_CHILD_NOT_COMPLETE          129
#define ERROR_BUSY                        170
#define ERROR_OPERATION_ABORTED           995
#define ERROR_SERVICE_NOT_ACTIVE          1062
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
 * NTDLL Export Boundary Types & Table.
 * Allows test injection of fakes at the export boundary for NtCreateWexAccountToken
 * and NtLoadWexProfileEx to verify success, failures, wrong token shape, wrong SID,
 * wrong session, and busy unload.
 */
typedef NTSTATUS (WINAPI *e1s18_pfn_NtCreateWexAccountToken)(
    ULONG operation,
    HANDLE *context,
    HANDLE caller,
    ULONGLONG *caps,
    const void *record,
    ULONG length,
    HANDLE *token,
    void *response,
    ULONG *response_size );

typedef NTSTATUS (WINAPI *e1s18_pfn_NtLoadWexProfileEx)(
    ULONG operation,
    HANDLE context,
    HANDLE token,
    ACCESS_MASK access,
    const void *record,
    ULONG length,
    HANDLE *key,
    ULONG *managed,
    ULONGLONG *generation );

struct e1s18_export_table
{
    e1s18_pfn_NtCreateWexAccountToken pNtCreateWexAccountToken;
    e1s18_pfn_NtLoadWexProfileEx     pNtLoadWexProfileEx;
};

void e1s18_set_export_table( const struct e1s18_export_table *table );
void e1s18_reset_export_table( void );
const struct e1s18_export_table *e1s18_get_export_table( void );

/* S19 rollback tracking for busy profile unloads */
BOOL e1s18_is_unload_busy( uint64_t *out_lease_id );
void e1s18_clear_unload_busy( void );

/*
 * S1 Kind 3 Evidence Record (220 bytes)
 * Defined in docs/E1_S1_CONTRACTS.md line 29.
 */
#pragma pack(push, 1)
struct wex_s1_evidence_record
{
    uint16_t kind;              /* 3 */
    uint16_t version;           /* 1 */
    uint32_t length;            /* 220 */
    uint64_t required_caps;     /* 0x3ff */
    uint64_t request_id;
    uint8_t  request_digest[32];
    uint8_t  binding_digest[32];
    uint8_t  nonce[32];
    uint32_t attempt;
    uint8_t  account_key[16];
    uint64_t host_uid;
    uint64_t completed;
    uint64_t expiry;
    uint64_t root_gen;
    uint64_t server_gen;
    uint64_t broker_gen;
    uint64_t verifier_gen;
    uint64_t map_gen;
    uint64_t policy_gen;
    uint32_t method;            /* 1 = PAM */
    uint32_t service;           /* 1 = fixed service */
};
#pragma pack(pop)

#define WEX_S1_EVIDENCE_SIZE sizeof(struct wex_s1_evidence_record)

/*
 * WO-14: S1 endorsed-identity context for the standard-user branch.
 *
 * The canonical S1 caller-binding and frozen-request records need endorsed
 * identities and generations that do not exist in production yet (the
 * registration work supplies them). They live here, zero today, so any zero
 * required input denies the request with ERROR_NOT_SUPPORTED before a dialog
 * starts. Tests set them through e1s18_set_s1_context.
 */
struct e1s18_s1_context
{
    /* Generations (all nonzero). */
    uint64_t root_gen, server_gen, broker_gen, verifier_gen, map_gen, policy_gen;
    /* Endorsed pinned identities (all nonzero). */
    uint8_t prefix[16];
    uint64_t host_uid;
    uint8_t host_process[16];
    uint8_t opener[16];
    uint8_t writer[16];
    uint8_t desktop[16];
    uint8_t channel[16];
    /* Image file identity from the held file. */
    uint64_t volume_serial;
    uint64_t file_index;
    uint64_t file_size;
    uint64_t last_write;
    /* Absolute deadline (monotonic ns). */
    uint64_t deadline;
};

/*
 * WO-20: R8 attempt-tuple context for the standard-user branch.
 *
 * The INIT message (R8 step 4) needs the sealed-tuple inputs that the server
 * and verifier will supply later: the dialog id, the verifier owner, the
 * generations, the caller session and the desktop route. They live here, zero
 * in the lab today, so any zero required input denies the request with
 * ERROR_NOT_SUPPORTED before a pipe or process is created (the WO-14 pattern).
 * Tests set them through e1s18_set_r8_context.
 */
#ifdef WEXELEVATE_E1S18
struct e1r8_tuple;

struct e1s18_r8_context
{
    /* Sealed-tuple inputs (all nonzero). */
    uint8_t dialog_id[16];
    uint8_t desktop_route[16];
    uint32_t caller_session;
    uint64_t root_gen, server_gen, broker_gen, verifier_gen, map_gen, policy_gen;
    /* Verifier owner: ASCII D-Bus unique name ":<digits>.<digits>", 4..255 bytes. */
    char verifier_owner[256];
};

void e1s18_set_r8_context( const struct e1s18_r8_context *ctx );
void e1s18_reset_r8_context( void );

/* WO-20: portable R8 INIT/ACK helpers (lab branch). The tuple is built from
 * the request, the in-flight attempt and the R8 context; the INIT is encoded
 * with the request's validated display strings; the ACK is decoded and checked
 * against the tuple. e1s18_build_r8_tuple returns 0 on success, -1 if the R8
 * context is incomplete. e1s18_encode_r8_init returns the encoded length on
 * success, 0 on denial. e1s18_verify_r8_ack returns 0 on success, negative on
 * any rejection. */
int e1s18_build_r8_tuple( const struct request *req,
                          uint64_t request_id,
                          uint32_t attempt,
                          const uint8_t nonce[32],
                          const uint8_t channel_id[16],
                          const uint8_t binding_digest[32],
                          const uint8_t request_digest[32],
                          BOOL secure,
                          struct e1r8_tuple *out );
size_t e1s18_encode_r8_init( const struct request *req,
                             const struct e1r8_tuple *t,
                             uint8_t *buf, size_t buf_len );
int e1s18_verify_r8_ack( const uint8_t *buf, size_t len,
                         const struct e1r8_tuple *t );

/* WO-25: fill the R8 context from live inputs (the S6 OPEN record, the
 * BINDING reply and the verifier owner) instead of relying on a preset
 * context. Called at the top of e1s18_spawn_consent_dialog. Returns
 * ERROR_SUCCESS on success, or the denial (ERROR_ACCESS_DENIED for a
 * validation failure, the op's error otherwise). A denial closes the S6
 * channel after a successful OPEN; success keeps it open for the attempt
 * (ENDORSED travels on that session) until e1s18_r8_release. A preset complete context skips prepare
 * only when the test flag (e1s18_set_r8_skip_prepare) says so; no production
 * path skips it. */
DWORD e1s18_r8_prepare( struct request *req );
/* Closes the S6 channel a successful prepare kept open for the attempt. */
void e1s18_r8_release( struct request *req );
void e1s18_set_r8_skip_prepare( BOOL skip );
#endif

/*
 * Dialog & Transport Boundary Types & Table.
 * Allows test injection of fakes at the dialog boundary (Stage 1)
 * and evidence transport boundary (Stage 2).
 */
typedef DWORD (*e1s18_dialog_fn)( struct request *req,
                                  uint64_t request_id,
                                  const uint8_t nonce[32],
                                  const uint8_t binding_digest[32],
                                  const uint8_t request_digest[32],
                                  const uint8_t channel_token[16],
                                  BOOL secure,
                                  uint8_t *out_answer_buf,
                                  size_t *out_answer_len,
                                  char *out_account,
                                  size_t account_max );

typedef DWORD (*e1s18_transport_fn)( struct request *req,
                                     const char *account_name,
                                     const uint8_t nonce[32],
                                     const uint8_t binding_digest[32],
                                     const uint8_t request_digest[32],
                                     uint8_t *out_evidence_buf,
                                     size_t *out_evidence_len,
                                     uint8_t out_evidence_ref[16] );

struct e1s18_boundary_table
{
    e1s18_dialog_fn    dialog_exec;
    e1s18_transport_fn transport_redeem;
};

void e1s18_set_boundary_table( const struct e1s18_boundary_table *table );
void e1s18_reset_boundary_table( void );
const struct e1s18_boundary_table *e1s18_get_boundary_table( void );

/* Test state reset helpers */
void e1s18_reset_answers( void );
void e1s18_reset_burned_evidence( void );
BOOL e1s18_is_evidence_burned( const uint8_t ref[16] );
void e1s18_reset_prompt_queue( void );
struct wex_prompt_queue *e1s18_get_prompt_queue( void );
void e1s18_set_test_clock( uint64_t now );
/* WO-16: force the monotonic clock to fail (return 0) so the fail-closed
 * denial path can be exercised. Pass FALSE to restore the real clock. */
void e1s18_set_test_clock_fail( BOOL fail );
/* R4: deterministic random source for the attempt nonce and channel id.
 * Pass NULL/0 to restore the real RNG. e1s18_set_test_random_fail forces the
 * RNG to fail so the fail-closed denial path can be exercised. */
void e1s18_set_test_random( const uint8_t *bytes, size_t len );
void e1s18_set_test_random_fail( BOOL fail );
/* WO-14: test hooks for the S1 endorsed-identity context. The context is
 * zero in production (the request denies); tests set it here. */
void e1s18_set_s1_context( const struct e1s18_s1_context *ctx );
void e1s18_reset_s1_context( void );
#ifdef WEXELEVATE_E1S18
/* WO-14 test hook: SHA-256 of tag||data, for golden-vector testing. */
void e1s18_test_sha256( const char *tag, size_t tag_len, const uint8_t *data, size_t data_len, uint8_t out[32] );
#endif

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

    /* Stage 1: S17 Prompt Queue & Dialog.
     * Service plumbing needed: spawn consent.exe /credentials on WinSta0\Winlogon,
     * named pipe IPC channel handshake, conversation loop with S17 prompt queue (H3b),
     * timeout (120s) and cancellation handling.
     * Status in S18: Not yet wired; fails closed with ERROR_NOT_SUPPORTED.
     */
    DWORD (*prompt_dialog)( struct request *req, const uint8_t nonce[32],
                            const uint8_t binding_digest[32], const uint8_t request_digest[32],
                            char *out_account, size_t account_max );

    /* Stage 2: S8 Evidence Redemption.
     * Service plumbing needed: connect to native S8 evidence daemon over Unix domain socket
     * /run/wineexplorer/evidence.sock, send S1 Kind 2 Answer record, receive S1 Kind 3
     * Evidence record (220 B) and 16-byte reference, verify S7 PAM worker authentication.
     * Status in S18: Not yet wired; fails closed with ERROR_NOT_SUPPORTED.
     */
    DWORD (*redeem_evidence)( struct request *req, const char *account_name,
                              const uint8_t nonce[32], const uint8_t binding_digest[32],
                              const uint8_t request_digest[32],
                              uint8_t out_account_key[16], uint8_t out_evidence_ref[16] );

    /* Stage 3: S10 Token Issuance.
     * Calls NtCreateWexAccountToken NEGOTIATE then ISSUE with S1 envelope (204 bytes).
     * Verifies returned token: Primary, Full elevation, High integrity, caller session,
     * distinct nonzero AuthenticationId, Alice's mapped SID; closes handles on any mismatch.
     */
    DWORD (*issue_token)( struct request *req, const uint8_t account_key[16],
                          const uint8_t evidence_ref[16], const uint8_t nonce[32],
                          const uint8_t binding_digest[32], const uint8_t request_digest[32],
                          HANDLE *out_token );

    /* Stage 4: S13/S14 Profile Load & Lease Acquisition.
     * Calls NtLoadWexProfileEx WEX_HKU_LOAD with S12/S13 record (48 bytes),
     * retains lease id for transfer and unload.
     */
    DWORD (*load_profile)( struct request *req, HANDLE token, const uint8_t account_key[16],
                           uint64_t *out_lease_id, HANDLE *out_hkey );

    /* Stage 3b: S12 storage generation query (WEX_HKU_STORAGE_QUERY, op 12).
     * Called after S10 issuance and before LOAD. Calls NtLoadWexProfileEx
     * WEX_HKU_STORAGE_QUERY with the LOAD record (storage_generation 0, the
     * only operation where 0 is accepted). Success: no key, managed=1 and a
     * verified nonzero generation, stored in req->storage_generation.
     * Does not consume the one-shot LOAD attempt.
     */
    DWORD (*query_storage)( struct request *req, HANDLE token );

    /* Stage 5: S15 Launch Preparation.
     * Calls wex_prepare_launch from launch_prep.c with explicit same-server inputs:
     * Bob's uid/gid from caller, profile record, cwd; real-UID requests denied;
     * builds sanitized Windows environment block; no caller environment accepted.
     */
    DWORD (*prep_launch)( struct request *req, HANDLE token,
                          char **out_win_env, size_t *out_win_env_sz,
                          char ***out_unix_env, size_t *out_unix_env_cnt,
                          char **out_cwd );

    /* Stage 6: Image Re-verification before create */
    DWORD (*verify_image)( struct request *req );

    /* Stage 7: Process Creation Suspended */
    DWORD (*create_process_suspended)( struct request *req, HANDLE token,
                                       const char *win_env, size_t win_env_sz,
                                       const char *cwd,
                                       PROCESS_INFORMATION *out_pi );

    /* Stage 8: Installed Child Process & Token Identity Verification */
    DWORD (*verify_child)( struct request *req, HANDLE token, const PROCESS_INFORMATION *pi );

    /* Stage 9: Lease Arming on child PID */
    DWORD (*arm_lease)( UINT64 lease, DWORD pid );

    /* Stage 10: Handle Duplication */
    DWORD (*duplicate_handle)( struct request *req, HANDLE process, HANDLE *out_remote );

    /* Stage 11: Lease Transfer to committed process tracking.
     * Calls NtLoadWexProfileEx WEX_HKU_COMMIT_PROCESS (op 11), using the
     * original S10 context retained through LOAD. No crash-test hook required.
     */
    DWORD (*transfer_lease)( struct request *req, uint64_t lease_id, HANDLE process, HANDLE token );

    /* Stage 12: Pre-resume Checks (binding, stop_event, lease_event) */
    DWORD (*check_pre_resume)( struct request *req );

    /* Stage 13: Guarded Resume */
    DWORD (*resume_thread)( struct request *req, HANDLE thread );

    /* Cleanup / Rollback Hooks.
     * terminate_child returns TRUE only when the child is confirmed dead
     * (R7 item 2); FALSE means the broker must keep the process/thread
     * references and skip the profile unload. */
    BOOL (*terminate_child)( HANDLE process );
    void (*close_remote_handle)( HANDLE caller, HANDLE remote );
    void (*unload_profile)( uint64_t lease_id, HANDLE hkey, HANDLE token );
    void (*close_handle)( HANDLE handle );
    void (*free_buffer)( void *ptr );

    /* WO-25: R8 context preparation and server endorsement (lab branch). The
     * default implementations are Win32-only (the S6 transport and the
     * extended caller-binding query); native stubs return ERROR_NOT_SUPPORTED.
     * r8_open/r8_owner use the S6 transport channel; r8_binding/r8_endorse/
     * r8_recheck use the extended NtQueryWexPipeCallerBindingEx on req->pipe.
     * r8_close closes the S6 channel (E1S6_CLOSE); it is best-effort. */
    DWORD (*r8_open)( struct request *req, unsigned char reg[E1S6_OPEN_SIZE], UINT64 *channel );
    DWORD (*r8_owner)( struct request *req, UINT64 channel, char owner[256] );
    DWORD (*r8_binding)( struct request *req, unsigned char out[124] );
    DWORD (*r8_endorse)( struct request *req, HANDLE dialog, const unsigned char tuple[204],
                         unsigned char dialog_id[16] );
    DWORD (*r8_recheck)( struct request *req, const unsigned char dialog_id[16] );
    DWORD (*r8_close)( struct request *req, UINT64 channel );
};

/* Core Broker Integration APIs */
BOOL e1s18_policy_enabled( void );
BOOL e1s18_check_capabilities( const struct request *req );
DWORD e1s18_handle_standard_request( struct request *req, struct wexelevate_reply *reply );
void e1s18_stop_prompt_queue( void );

/* Operations override for testing */
void e1s18_set_ops( const struct e1s18_ops *ops );
void e1s18_reset_ops( void );
const struct e1s18_ops *e1s18_get_ops( void );

#ifdef WEXELEVATE_E1S18
DWORD e1s18_transport_redeem_live( struct request *req,
                                   const char *account_name,
                                   const uint8_t nonce[32],
                                   const uint8_t binding_digest[32],
                                   const uint8_t request_digest[32],
                                   uint8_t *out_evidence_buf,
                                   size_t *out_evidence_len,
                                   uint8_t out_evidence_ref[16] );
#endif

#ifdef __cplusplus
}
#endif

#endif /* WEXELEVATE_E1S18_BROKER_H */
