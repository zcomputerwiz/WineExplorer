/*
 * chromium_mojo_bootstrap_probe.c
 *
 * Self-contained Win32 probe reproducing the exact Windows Mojo PlatformChannel
 * transport, sandboxed child token/launch sequence, TargetServices::LowerToken,
 * IOCP overlapped ReadFile/WriteFile, and in-band DuplicateHandle transfer used
 * by pinned Chromium 7d9824099030f5c28c939ba08654562faaad7c09:
 *   - mojo/public/cpp/platform/platform_channel.cc (CreateChannel)
 *   - mojo/core/channel_win.cc (ChannelWin IO completion port read/write)
 *   - mojo/core/ipcz_driver/invitation.cc & transport.cc (ConnectNode & handles)
 *   - sandbox/win/src/sandbox_policy_base.cc (MakeTokens)
 *   - sandbox/win/src/restricted_token_utils.cc (CreateRestrictedToken)
 *   - sandbox/win/src/app_container_base.cc (BuildImpersonationToken)
 *   - sandbox/win/src/target_process.cc (Create, SetThreadToken, Init)
 *   - sandbox/win/src/target_services.cc (LowerToken, RevertToSelf)
 *
 * Usage:
 *   chromium_mojo_bootstrap_probe.exe [plain|service_restricted|network_appcontainer|all]
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define PROBE_MAGIC_P2C_1 0x4D4F4A31UL /* "MOJ1" */
#define PROBE_MAGIC_C2P_1 0x4D4F4A32UL /* "MOJ2" */
#define PROBE_MAGIC_P2C_2 0x4D4F4A33UL /* "MOJ3" */
#define PROBE_SHMEM_MAGIC 0x57455853UL /* "WEXS" */

#define PARENT_IOCP_KEY ((ULONG_PTR)0x11110001ULL)
#define CHILD_IOCP_KEY  ((ULONG_PTR)0x22220002ULL)
#define WAIT_TIMEOUT_MS 5000

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint64_t transferred_event_handle;
    uint64_t transferred_shmem_handle;
    uint64_t pre_resume_section_handle;
    char     payload[64];
} mojo_probe_msg_t;
#pragma pack(pop)

typedef struct {
    uint32_t magic;
    uint32_t child_pid;
    uint32_t log_len;
    char     child_log[7900];
} shared_policy_sec_t;

typedef BOOL (WINAPI *pfn_CreateAppContainerToken)(HANDLE, PSECURITY_CAPABILITIES, PHANDLE);
typedef HRESULT (WINAPI *pfn_AppContainerRegisterSid)(PSID, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *pfn_AppContainerUnregisterSid)(PSID);
typedef LONG (NTAPI *pfn_NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *pfn_NtQueryObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static char g_child_log_buf[7800];
static size_t g_child_log_off = 0;
static BOOL g_is_child_process = FALSE;

static void log_line(const char *role, const char *mode, const char *fmt, ...)
{
    char line[512];
    int n1 = snprintf(line, sizeof(line), "[%s:%s] ", role, mode);
    if (n1 < 0) n1 = 0;
    va_list ap;
    va_start(ap, fmt);
    int n2 = vsnprintf(line + n1, sizeof(line) - (size_t)n1 - 2, fmt, ap);
    va_end(ap);
    if (n2 < 0) n2 = 0;
    size_t total = (size_t)(n1 + n2);
    if (total >= sizeof(line) - 2) total = sizeof(line) - 2;
    line[total++] = '\n';
    line[total] = '\0';

    if (g_is_child_process) {
        if (g_child_log_off + total < sizeof(g_child_log_buf) - 1) {
            memcpy(g_child_log_buf + g_child_log_off, line, total);
            g_child_log_off += total;
            g_child_log_buf[g_child_log_off] = '\0';
        }
    } else {
        fwrite(line, 1, total, stdout);
        fflush(stdout);
    }
}

static void grant_sandbox_rx_on_path(const wchar_t *path, PSID extra_sid)
{
    PACL old_dacl = NULL, new_dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    if (GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              NULL, NULL, &old_dacl, NULL, &sd) != ERROR_SUCCESS) {
        return;
    }
    static const wchar_t *sids[] = {
        L"S-1-1-0",      /* Everyone */
        L"S-1-5-12",     /* Restricted */
        L"S-1-5-32-545", /* Users */
        L"S-1-15-2-1"    /* ALL APPLICATION PACKAGES */
    };
    PSID parsed_sids[4] = {0};
    EXPLICIT_ACCESSW ea[5];
    ZeroMemory(ea, sizeof(ea));
    ULONG count = 0;
    for (int i = 0; i < 4; i++) {
        if (ConvertStringSidToSidW(sids[i], &parsed_sids[i])) {
            ea[count].grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
            ea[count].grfAccessMode = GRANT_ACCESS;
            ea[count].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            ea[count].Trustee.TrusteeForm = TRUSTEE_IS_SID;
            ea[count].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
            ea[count].Trustee.ptstrName = (LPWSTR)parsed_sids[i];
            count++;
        }
    }
    if (extra_sid) {
        ea[count].grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
        ea[count].grfAccessMode = GRANT_ACCESS;
        ea[count].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        ea[count].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea[count].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        ea[count].Trustee.ptstrName = (LPWSTR)extra_sid;
        count++;
    }
    if (count > 0 && SetEntriesInAclW(count, ea, old_dacl, &new_dacl) == ERROR_SUCCESS && new_dacl) {
        SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              NULL, NULL, new_dacl, NULL);
        LocalFree(new_dacl);
    }
    for (int i = 0; i < 4; i++)
        if (parsed_sids[i]) LocalFree(parsed_sids[i]);
    if (sd) LocalFree(sd);
}

/* Matches AppContainerBase::BuildImpersonationToken (app_container_base.cc:399-415) */
static BOOL copy_kernel_object_dacl(HANDLE src, HANDLE dst)
{
    BYTE sd_buf[1024];
    DWORD needed = 0;
    if (!GetKernelObjectSecurity(src, DACL_SECURITY_INFORMATION, sd_buf, sizeof(sd_buf), &needed))
        return FALSE;
    return SetKernelObjectSecurity(dst, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)sd_buf);
}

/* Matches sandbox::RestrictedToken::SetDefaultDacl (restricted_token.cc:184-249) */
static BOOL set_restricted_token_default_dacl(HANDLE hToken, HANDLE hBase)
{
    BYTE ubuf[256];
    DWORD ulen = 0;
    if (!GetTokenInformation(hBase, TokenUser, ubuf, sizeof(ubuf), &ulen)) return FALSE;
    TOKEN_USER *tu = (TOKEN_USER *)ubuf;

    PSID pRestricted = NULL;
    if (!ConvertStringSidToSidW(L"S-1-5-12", &pRestricted)) return FALSE;

    DWORD aclSize = sizeof(ACL) + 2 * sizeof(ACCESS_ALLOWED_ACE) +
                    GetLengthSid(tu->User.Sid) + GetLengthSid(pRestricted) + 32;
    PACL pAcl = (PACL)LocalAlloc(LPTR, aclSize);
    if (!pAcl || !InitializeAcl(pAcl, aclSize, ACL_REVISION)) {
        LocalFree(pRestricted);
        if (pAcl) LocalFree(pAcl);
        return FALSE;
    }

    AddAccessAllowedAce(pAcl, ACL_REVISION, GENERIC_ALL, tu->User.Sid);
    AddAccessAllowedAce(pAcl, ACL_REVISION, GENERIC_ALL, pRestricted);
    LocalFree(pRestricted);

    TOKEN_DEFAULT_DACL tdd;
    tdd.DefaultDacl = pAcl;
    BOOL ok = SetTokenInformation(hToken, TokenDefaultDacl, &tdd, sizeof(tdd));
    DWORD err = ok ? 0 : GetLastError();
    LocalFree(pAcl);
    SetLastError(err);
    return ok;
}

/* Matches sandbox::ReplacePackageSidInDacl (sandbox_policy_base.cc:89-108) */
static BOOL replace_package_sid_in_token_dacl(HANDLE hToken, PSID package_sid, ACCESS_MASK access)
{
    DWORD needed = 0;
    GetKernelObjectSecurity(hToken, DACL_SECURITY_INFORMATION, NULL, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0) return FALSE;

    PSECURITY_DESCRIPTOR pOldSd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, needed);
    if (!pOldSd || !GetKernelObjectSecurity(hToken, DACL_SECURITY_INFORMATION, pOldSd, needed, &needed)) {
        if (pOldSd) LocalFree(pOldSd);
        return FALSE;
    }

    PACL pOldDacl = NULL;
    BOOL daclPresent = FALSE, daclDefaulted = FALSE;
    if (!GetSecurityDescriptorDacl(pOldSd, &daclPresent, &pOldDacl, &daclDefaulted) || !daclPresent) {
        LocalFree(pOldSd);
        return FALSE;
    }

    PSID pAllAppPackages = NULL;
    if (!ConvertStringSidToSidW(L"S-1-15-2-1", &pAllAppPackages)) {
        LocalFree(pOldSd);
        return FALSE;
    }

    DWORD newAclSize = (pOldDacl ? pOldDacl->AclSize : sizeof(ACL)) +
                       sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(pAllAppPackages) + 64;
    PACL pNewDacl = (PACL)LocalAlloc(LPTR, newAclSize);
    if (!pNewDacl || !InitializeAcl(pNewDacl, newAclSize, ACL_REVISION)) {
        LocalFree(pAllAppPackages);
        LocalFree(pOldSd);
        if (pNewDacl) LocalFree(pNewDacl);
        return FALSE;
    }

    if (!AddAccessAllowedAce(pNewDacl, ACL_REVISION, access, pAllAppPackages)) {
        LocalFree(pAllAppPackages);
        LocalFree(pOldSd);
        LocalFree(pNewDacl);
        return FALSE;
    }
    LocalFree(pAllAppPackages);

    if (pOldDacl) {
        for (USHORT i = 0; i < pOldDacl->AceCount; i++) {
            LPVOID pAce = NULL;
            if (GetAce(pOldDacl, i, &pAce)) {
                ACE_HEADER *hdr = (ACE_HEADER *)pAce;
                PSID aceSid = NULL;
                if (hdr->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                    aceSid = (PSID)&((ACCESS_ALLOWED_ACE *)pAce)->SidStart;
                } else if (hdr->AceType == ACCESS_DENIED_ACE_TYPE) {
                    aceSid = (PSID)&((ACCESS_DENIED_ACE *)pAce)->SidStart;
                }
                if (aceSid && package_sid && EqualSid(aceSid, package_sid)) {
                    continue;
                }
                AddAce(pNewDacl, ACL_REVISION, MAXDWORD, pAce, hdr->AceSize);
            }
        }
    }

    SECURITY_DESCRIPTOR newSd;
    if (!InitializeSecurityDescriptor(&newSd, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&newSd, TRUE, pNewDacl, FALSE)) {
        LocalFree(pOldSd);
        LocalFree(pNewDacl);
        return FALSE;
    }

    BOOL ok = SetKernelObjectSecurity(hToken, DACL_SECURITY_INFORMATION, &newSd);
    DWORD err = ok ? 0 : GetLastError();
    LocalFree(pOldSd);
    LocalFree(pNewDacl);
    SetLastError(err);
    return ok;
}

static DWORD query_token_il(HANDLE hTok)
{
    BYTE buf[256];
    DWORD ret_len = 0;
    if (!GetTokenInformation(hTok, TokenIntegrityLevel, buf, sizeof(buf), &ret_len))
        return 0xFFFFFFFFUL;
    TOKEN_MANDATORY_LABEL *tml = (TOKEN_MANDATORY_LABEL *)buf;
    if (!tml->Label.Sid || !IsValidSid(tml->Label.Sid))
        return 0xFFFFFFFEUL;
    UCHAR count = *GetSidSubAuthorityCount(tml->Label.Sid);
    if (count == 0)
        return 0xFFFFFFFDUL;
    return *GetSidSubAuthority(tml->Label.Sid, count - 1);
}

static DWORD query_token_is_appcontainer(HANDLE hTok)
{
    DWORD is_ac = 0, ret_len = 0;
    if (!GetTokenInformation(hTok, TokenIsAppContainer, &is_ac, sizeof(is_ac), &ret_len))
        return 0xFFFFFFFFUL;
    return is_ac;
}

static BOOL set_token_il_str(HANDLE hTok, const wchar_t *sid_str)
{
    PSID sid = NULL;
    if (!ConvertStringSidToSidW(sid_str, &sid))
        return FALSE;
    TOKEN_MANDATORY_LABEL tml;
    ZeroMemory(&tml, sizeof(tml));
    tml.Label.Sid = sid;
    tml.Label.Attributes = SE_GROUP_INTEGRITY;
    BOOL ok = SetTokenInformation(hTok, TokenIntegrityLevel, &tml, sizeof(tml));
    LocalFree(sid);
    return ok;
}

static PSID dup_sid(PSID src)
{
    if (!src || !IsValidSid(src)) return NULL;
    DWORD len = GetLengthSid(src);
    PSID dst = LocalAlloc(LMEM_FIXED, len);
    if (dst && !CopySid(len, dst, src)) {
        LocalFree(dst);
        return NULL;
    }
    return dst;
}

/*
 * Matches sandbox::CreateRestrictedToken in sandbox/win/src/restricted_token_utils.cc:75-126
 * (USER_RESTRICTED_NON_ADMIN / USER_LIMITED).
 */
static BOOL create_chromium_restricted_token(
    HANDLE hBase,
    BOOL lockdown_mode,
    BOOL make_impersonation,
    const wchar_t *il_sid_str,
    HANDLE *out_tok)
{
    static const wchar_t *disable_sids[] = {
        L"S-1-5-32-544", /* Administrators */
        L"S-1-5-32-546"  /* Guests */
    };
    static const wchar_t *well_known_restrict[] = {
        L"S-1-5-32-545", /* BuiltinUsers */
        L"S-1-1-0",      /* World / Everyone */
        L"S-1-5-4",      /* Interactive */
        L"S-1-5-11",     /* Authenticated Users */
        L"S-1-5-12"      /* Restricted */
    };
    SID_AND_ATTRIBUTES dis[2];
    SID_AND_ATTRIBUTES res[8];
    ZeroMemory(dis, sizeof(dis));
    ZeroMemory(res, sizeof(res));

    DWORD dis_count = lockdown_mode ? 2 : 0;
    DWORD res_count = 0;

    for (DWORD i = 0; i < dis_count; i++) {
        if (!ConvertStringSidToSidW(disable_sids[i], &dis[i].Sid))
            return FALSE;
    }
    for (DWORD i = 0; i < 5; i++) {
        if (!ConvertStringSidToSidW(well_known_restrict[i], &res[res_count].Sid))
            return FALSE;
        res_count++;
    }

    /* Add Current User SID and Logon Session SID so KnownDlls/BNO/WinSta0 open on Native Windows */
    BYTE user_buf[256];
    DWORD ret_len = 0;
    if (GetTokenInformation(hBase, TokenUser, user_buf, sizeof(user_buf), &ret_len)) {
        TOKEN_USER *tu = (TOKEN_USER *)user_buf;
        PSID u_copy = dup_sid(tu->User.Sid);
        if (u_copy) res[res_count++].Sid = u_copy;
    }

    BYTE grp_buf[4096];
    if (GetTokenInformation(hBase, TokenGroups, grp_buf, sizeof(grp_buf), &ret_len)) {
        TOKEN_GROUPS *tg = (TOKEN_GROUPS *)grp_buf;
        for (DWORD i = 0; i < tg->GroupCount; i++) {
            if ((tg->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID) {
                PSID l_copy = dup_sid(tg->Groups[i].Sid);
                if (l_copy) res[res_count++].Sid = l_copy;
                break;
            }
        }
    }

    HANDLE hRestr = NULL;
    DWORD flags = lockdown_mode ? DISABLE_MAX_PRIVILEGE : 0;
    BOOL ok = CreateRestrictedToken(
        hBase,
        flags,
        dis_count,
        dis_count ? dis : NULL,
        0,
        NULL,
        res_count,
        res,
        &hRestr);

    for (DWORD i = 0; i < dis_count; i++)
        if (dis[i].Sid) LocalFree(dis[i].Sid);
    for (DWORD i = 0; i < res_count; i++)
        if (res[i].Sid) LocalFree(res[i].Sid);

    if (!ok || !hRestr)
        return FALSE;

    HANDLE hFinal = hRestr;
    if (make_impersonation) {
        if (!DuplicateTokenEx(hRestr, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &hFinal)) {
            CloseHandle(hRestr);
            return FALSE;
        }
        CloseHandle(hRestr);
    }

    if (il_sid_str && !set_token_il_str(hFinal, il_sid_str)) {
        CloseHandle(hFinal);
        return FALSE;
    }

    /* Set TokenDefaultDacl per sandbox/win/src/restricted_token.cc:184-249 */
    if (!lockdown_mode) {
        set_restricted_token_default_dacl(hFinal, hBase);
    }

    *out_tok = hFinal;
    return TRUE;
}

static BOOL wait_iocp_packet(
    HANDLE iocp,
    ULONG_PTR expected_key,
    OVERLAPPED *expected_ovl,
    DWORD *out_bytes,
    const char *role,
    const char *mode,
    const char *step_label)
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED *ovl = NULL;
    BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &key, &ovl, WAIT_TIMEOUT_MS);
    DWORD err = ok ? 0 : GetLastError();
    log_line(role, mode, "%s: GQCS ok=%d err=%lu bytes=%lu key=0x%Ix (exp=0x%Ix) ovl=%p (exp=%p)",
             step_label, (int)ok, err, bytes, key, expected_key, (void *)ovl, (void *)expected_ovl);
    if (!ok || key != expected_key || ovl != expected_ovl)
        return FALSE;
    if (out_bytes)
        *out_bytes = bytes;
    return TRUE;
}

static void flush_child_log_to_section(HANDLE pre_sec_h)
{
    if (!pre_sec_h) return;
    shared_policy_sec_t *sec = (shared_policy_sec_t *)MapViewOfFile(
        pre_sec_h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(shared_policy_sec_t));
    if (sec) {
        sec->child_pid = GetCurrentProcessId();
        sec->log_len = (uint32_t)g_child_log_off;
        memcpy(sec->child_log, g_child_log_buf, g_child_log_off + 1);
        UnmapViewOfFile(sec);
    }
}

static int run_child(const char *mode, uintptr_t raw_pipe_h)
{
    g_is_child_process = TRUE;
    HANDLE client_h = (HANDLE)raw_pipe_h;

    log_line("CHILD", mode, "STAGE3_START pid=%lu tid=%lu mojo_handle=0x%Ix",
             GetCurrentProcessId(), GetCurrentThreadId(), raw_pipe_h);

    /* 1. Verify inherited Mojo pipe handle before LowerToken */
    DWORD hflags = 0;
    BOOL hinfo_ok = GetHandleInformation(client_h, &hflags);
    DWORD hinfo_err = hinfo_ok ? 0 : GetLastError();
    DWORD ftype = GetFileType(client_h);
    DWORD server_pid = 0;
    BOOL spid_ok = GetNamedPipeServerProcessId(client_h, &server_pid);
    DWORD spid_err = spid_ok ? 0 : GetLastError();
    log_line("CHILD", mode, "STAGE3_HANDLE_CHECK GetHandleInformation=%d(err=%lu,flags=0x%lx) GetFileType=%lu ServerPid=%lu(ok=%d,err=%lu)",
             (int)hinfo_ok, hinfo_err, hflags, ftype, server_pid, (int)spid_ok, spid_err);
    if (!hinfo_ok || ftype != FILE_TYPE_PIPE) {
        log_line("CHILD", mode, "FAIL: inherited Mojo handle 0x%Ix invalid or not a pipe", raw_pipe_h);
        return 11;
    }

    /* 2. Inspect thread & process tokens before LowerToken */
    HANDLE hThreadTok = NULL;
    BOOL has_thread_tok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hThreadTok);
    DWORD thread_tok_err = has_thread_tok ? 0 : GetLastError();
    DWORD thread_il = has_thread_tok ? query_token_il(hThreadTok) : 0;
    DWORD thread_ac = has_thread_tok ? query_token_is_appcontainer(hThreadTok) : 0;
    if (hThreadTok) CloseHandle(hThreadTok);

    HANDLE hProcTok = NULL;
    BOOL has_proc_tok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hProcTok);
    DWORD proc_il = has_proc_tok ? query_token_il(hProcTok) : 0;
    DWORD proc_ac = has_proc_tok ? query_token_is_appcontainer(hProcTok) : 0;
    if (hProcTok) CloseHandle(hProcTok);

    log_line("CHILD", mode, "STAGE3_PRE_LOWER thread_tok=%d(err=%lu,il=0x%lx,ac=%lu) proc_tok=%d(il=0x%lx,ac=%lu)",
             (int)has_thread_tok, thread_tok_err, thread_il, thread_ac,
             (int)has_proc_tok, proc_il, proc_ac);

    /* 3. Bind client_h to child IOCP and post ReadFile #1 before LowerToken */
    HANDLE child_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!child_iocp) {
        log_line("CHILD", mode, "FAIL: CreateIoCompletionPort(new) failed err=%lu", GetLastError());
        return 16;
    }
    HANDLE assoc = CreateIoCompletionPort(client_h, child_iocp, CHILD_IOCP_KEY, 1);
    if (assoc != child_iocp) {
        log_line("CHILD", mode, "FAIL: CreateIoCompletionPort(bind client_h) failed err=%lu", GetLastError());
        return 17;
    }

    mojo_probe_msg_t rx1;
    ZeroMemory(&rx1, sizeof(rx1));
    OVERLAPPED child_read_ovl1;
    ZeroMemory(&child_read_ovl1, sizeof(child_read_ovl1));
    BOOL r1_ok = ReadFile(client_h, &rx1, sizeof(rx1), NULL, &child_read_ovl1);
    DWORD r1_err = r1_ok ? 0 : GetLastError();
    log_line("CHILD", mode, "STAGE4_READ1_POST ok=%d err=%lu", (int)r1_ok, r1_err);
    if (!r1_ok && r1_err != ERROR_IO_PENDING) {
        log_line("CHILD", mode, "FAIL: ReadFile #1 failed err=%lu", r1_err);
        return 18;
    }

    /* 4. Dequeue Read #1 from IOCP and validate Mojo invitation payload BEFORE LowerToken */
    DWORD r1_bytes = 0;
    if (!wait_iocp_packet(child_iocp, CHILD_IOCP_KEY, &child_read_ovl1, &r1_bytes, "CHILD", mode, "STAGE4_READ1_IOCP")) {
        log_line("CHILD", mode, "FAIL: wait_iocp_packet(Read #1) failed");
        return 19;
    }
    if (r1_bytes != sizeof(rx1) || rx1.magic != PROBE_MAGIC_P2C_1 || strcmp(rx1.payload, "MOJO_INVITATION_CONNECT_NODE") != 0) {
        log_line("CHILD", mode, "FAIL: ReadFile #1 bad payload bytes=%lu magic=0x%08x payload=\"%s\"",
                 r1_bytes, rx1.magic, rx1.payload);
        return 20;
    }
    log_line("CHILD", mode, "STAGE4_READ1_VERIFIED bytes=%lu magic=0x%08x payload=\"%s\"",
             r1_bytes, rx1.magic, rx1.payload);

    /* 5. TargetServicesBase::LowerToken sequence (sandbox/win/src/target_services.cc:127-155) */
    if (strcmp(mode, "service_restricted") == 0 || strcmp(mode, "network_appcontainer") == 0) {
        HANDLE hTokAdj = NULL;
        /* FromCurrentProcess(false, TOKEN_ADJUST_DEFAULT) calls FromProcess, which adds TOKEN_QUERY. */
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &hTokAdj)) {
            /* StorageService lowers to Untrusted (S-1-16-0), NetworkService lowers to Low (S-1-16-4096) */
            const wchar_t *target_il = (strcmp(mode, "service_restricted") == 0) ? L"S-1-16-0" : L"S-1-16-4096";
            BOOL il_ok = set_token_il_str(hTokAdj, target_il);
            log_line("CHILD", mode, "STAGE3_SET_DELAYED_IL target=%ls ok=%d err=%lu",
                     target_il, (int)il_ok, il_ok ? 0 : GetLastError());
            CloseHandle(hTokAdj);
            if (!il_ok) {
                log_line("CHILD", mode, "FAIL: SetProcessIntegrityLevel failed err=%lu -> SBOX_FATAL_INTEGRITY(1)", GetLastError());
                return 1; /* SBOX_FATAL_INTEGRITY */
            }
        } else {
            log_line("CHILD", mode, "FAIL: OpenProcessToken(TOKEN_QUERY | TOKEN_ADJUST_DEFAULT) failed err=%lu -> SBOX_FATAL_INTEGRITY(1)", GetLastError());
            return 1; /* SBOX_FATAL_INTEGRITY */
        }
    }

    if (!RevertToSelf()) {
        log_line("CHILD", mode, "FAIL: RevertToSelf failed err=%lu -> SBOX_FATAL_DROPTOKEN(2)", GetLastError());
        return 2; /* SBOX_FATAL_DROPTOKEN */
    }

    HANDLE hAfterThreadTok = NULL;
    BOOL still_imp = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hAfterThreadTok);
    DWORD after_imp_err = still_imp ? 0 : GetLastError();
    if (hAfterThreadTok) CloseHandle(hAfterThreadTok);

    HANDLE hAfterProcTok = NULL;
    DWORD after_proc_il = 0xFFFFFFFFUL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hAfterProcTok)) {
        after_proc_il = query_token_il(hAfterProcTok);
        CloseHandle(hAfterProcTok);
    }

    /* FlushCachedRegHandles + RegDisablePredefinedCache + WarmupWindowsLocales */
    HKEY hk = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, NULL, 0, MAXIMUM_ALLOWED, &hk) == ERROR_SUCCESS) RegCloseKey(hk);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, NULL, 0, MAXIMUM_ALLOWED, &hk) == ERROR_SUCCESS) RegCloseKey(hk);
    if (RegOpenKeyExW(HKEY_USERS, NULL, 0, MAXIMUM_ALLOWED, &hk) == ERROR_SUCCESS) RegCloseKey(hk);
    RegDisablePredefinedCache();
    LANGID lang_id = GetUserDefaultLangID();
    LCID lcid = GetUserDefaultLCID();
    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH] = {0};
    GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH);

    /* CloseOpenHandles probe: GetProcessHandleCount + NtQueryInformationProcess(ProcessHandleTable=58) + NtQueryObject */
    DWORD handle_count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handle_count);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    pfn_NtQueryInformationProcess pNtQIP = hNtdll ? (pfn_NtQueryInformationProcess)(void *)GetProcAddress(hNtdll, "NtQueryInformationProcess") : NULL;
    pfn_NtQueryObject pNtQO = hNtdll ? (pfn_NtQueryObject)(void *)GetProcAddress(hNtdll, "NtQueryObject") : NULL;
    ULONG qip_len = 0, qo_len = 0;
    BYTE qip_buf[4096], qo_buf[512];
    LONG qip_st = pNtQIP ? pNtQIP(GetCurrentProcess(), 58 /*ProcessHandleTable*/, qip_buf, sizeof(qip_buf), &qip_len) : -1;
    LONG qo_st = pNtQO ? pNtQO(client_h, 2 /*ObjectTypeInformation*/, qo_buf, sizeof(qo_buf), &qo_len) : -1;

    log_line("CHILD", mode, "STAGE3_POST_LOWER RevertToSelf_no_thread_tok=%d(err=%lu) proc_il=0x%lx lang=0x%x lcid=0x%lx hcount=%lu NtQIP(58)=0x%lx NtQO(2)=0x%lx",
             (int)!still_imp, after_imp_err, after_proc_il, (unsigned)lang_id, (unsigned long)lcid, handle_count, (unsigned long)qip_st, (unsigned long)qo_st);

    if (still_imp || after_imp_err != ERROR_NO_TOKEN) {
        log_line("CHILD", mode, "FAIL: thread still impersonating after RevertToSelf");
        return 15;
    }

    /* Validate pre-resume shared policy section & in-band transferred handles under lowered lockdown token */
    HANDLE pre_sec_h = (HANDLE)(uintptr_t)rx1.pre_resume_section_handle;
    DWORD sflags = 0;
    BOOL s_ok = GetHandleInformation(pre_sec_h, &sflags);
    shared_policy_sec_t *sec_view = (shared_policy_sec_t *)MapViewOfFile(pre_sec_h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(shared_policy_sec_t));
    uint32_t sec_magic = sec_view ? sec_view->magic : 0;
    if (sec_view) UnmapViewOfFile(sec_view);
    log_line("CHILD", mode, "STAGE4_SHARED_SECTION handle=0x%Ix ok=%d magic=0x%08x",
             (uintptr_t)pre_sec_h, (int)s_ok, sec_magic);
    if (!s_ok || sec_magic != PROBE_SHMEM_MAGIC) {
        log_line("CHILD", mode, "FAIL: pre-resume shared section invalid in child");
        flush_child_log_to_section(pre_sec_h);
        return 12;
    }

    HANDLE hTransEvent = (HANDLE)(uintptr_t)rx1.transferred_event_handle;
    HANDLE hTransShmem = (HANDLE)(uintptr_t)rx1.transferred_shmem_handle;
    DWORD ev_flags = 0, shm_flags = 0;
    BOOL ev_info_ok = GetHandleInformation(hTransEvent, &ev_flags);
    BOOL shm_info_ok = GetHandleInformation(hTransShmem, &shm_flags);
    DWORD ev_wait = WaitForSingleObject(hTransEvent, 0);
    char *shm_view = (char *)MapViewOfFile(hTransShmem, FILE_MAP_READ, 0, 0, 64);
    char shm_str[32] = {0};
    if (shm_view) {
        memcpy(shm_str, shm_view, 20);
        UnmapViewOfFile(shm_view);
    }
    log_line("CHILD", mode, "STAGE4_INBAND_HANDLES event=0x%Ix(info=%d,wait=%lu) shmem=0x%Ix(info=%d,data=\"%s\")",
             (uintptr_t)hTransEvent, (int)ev_info_ok, ev_wait,
             (uintptr_t)hTransShmem, (int)shm_info_ok, shm_str);
    if (!ev_info_ok || ev_wait != WAIT_OBJECT_0 || !shm_info_ok || strcmp(shm_str, "MOJO_SHMEM_PORTAL_OK") != 0) {
        log_line("CHILD", mode, "FAIL: in-band transferred handles failed validation in child");
        flush_child_log_to_section(pre_sec_h);
        return 21;
    }
    CloseHandle(hTransEvent);
    CloseHandle(hTransShmem);

    /* Post Read #2 AFTER LowerToken and BEFORE parent writes Message #2 */
    mojo_probe_msg_t rx2;
    ZeroMemory(&rx2, sizeof(rx2));
    OVERLAPPED child_read_ovl2;
    ZeroMemory(&child_read_ovl2, sizeof(child_read_ovl2));
    BOOL r2_ok = ReadFile(client_h, &rx2, sizeof(rx2), NULL, &child_read_ovl2);
    DWORD r2_err = r2_ok ? 0 : GetLastError();
    log_line("CHILD", mode, "STAGE4_READ2_PREPOST ok=%d err=%lu", (int)r2_ok, r2_err);
    if (!r2_ok && r2_err != ERROR_IO_PENDING) {
        log_line("CHILD", mode, "FAIL: ReadFile #2 pre-post failed err=%lu", r2_err);
        flush_child_log_to_section(pre_sec_h);
        return 22;
    }

    /* Write reply Message #1 from child to parent AFTER LowerToken */
    mojo_probe_msg_t tx1;
    ZeroMemory(&tx1, sizeof(tx1));
    tx1.magic = PROBE_MAGIC_C2P_1;
    tx1.seq = 1;
    strcpy(tx1.payload, "MOJO_CHILD_ACCEPT_ACK");
    OVERLAPPED child_write_ovl1;
    ZeroMemory(&child_write_ovl1, sizeof(child_write_ovl1));
    BOOL w1_ok = WriteFile(client_h, &tx1, sizeof(tx1), NULL, &child_write_ovl1);
    DWORD w1_err = w1_ok ? 0 : GetLastError();
    log_line("CHILD", mode, "STAGE4_WRITE1_POST ok=%d err=%lu", (int)w1_ok, w1_err);
    if (!w1_ok && w1_err != ERROR_IO_PENDING) {
        log_line("CHILD", mode, "FAIL: WriteFile #1 failed err=%lu", w1_err);
        flush_child_log_to_section(pre_sec_h);
        return 23;
    }
    DWORD w1_bytes = 0;
    if (!wait_iocp_packet(child_iocp, CHILD_IOCP_KEY, &child_write_ovl1, &w1_bytes, "CHILD", mode, "STAGE4_WRITE1_IOCP")) {
        flush_child_log_to_section(pre_sec_h);
        return 24;
    }

    /* Wait for Read #2 completion after parent sends Message #2 */
    DWORD r2_bytes = 0;
    if (!wait_iocp_packet(child_iocp, CHILD_IOCP_KEY, &child_read_ovl2, &r2_bytes, "CHILD", mode, "STAGE4_READ2_IOCP")) {
        flush_child_log_to_section(pre_sec_h);
        return 25;
    }
    if (r2_bytes != sizeof(rx2) || rx2.magic != PROBE_MAGIC_P2C_2 || strcmp(rx2.payload, "MOJO_BROKER_PING2") != 0) {
        log_line("CHILD", mode, "FAIL: ReadFile #2 bad payload bytes=%lu magic=0x%08x", r2_bytes, rx2.magic);
        flush_child_log_to_section(pre_sec_h);
        return 26;
    }

    log_line("CHILD", mode, "CHILD_PASS all stages succeeded");
    flush_child_log_to_section(pre_sec_h);
    CloseHandle(pre_sec_h);
    CloseHandle(child_iocp);
    CloseHandle(client_h);
    return 0;
}

static int run_parent_mode(const char *mode)
{
    log_line("PARENT", mode, "=== BEGIN MODE %s ===", mode);

    /*
     * STAGE 1: PlatformChannel::CreateChannel (mojo/public/cpp/platform/platform_channel.cc:56-118)
     */
    wchar_t pipe_name[128];
    swprintf(pipe_name, 128, L"\\\\.\\pipe\\mojo.%lu.%lu.%lu_%hs",
             GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount(), mode);

    const DWORD kOpenMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    const DWORD kPipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE;
    HANDLE server_h = CreateNamedPipeW(pipe_name, kOpenMode, kPipeMode, 1, 4096, 4096, 5000, NULL);
    if (server_h == INVALID_HANDLE_VALUE) {
        log_line("PARENT", mode, "FAIL: CreateNamedPipeW failed err=%lu", GetLastError());
        return 1;
    }

    const DWORD kDesiredAccess = (FILE_GENERIC_READ | FILE_GENERIC_WRITE) & ~FILE_APPEND_DATA;
    const DWORD kFlags = SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS | FILE_FLAG_OVERLAPPED;
    SECURITY_ATTRIBUTES sa_inherit = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE client_h = CreateFileW(pipe_name, kDesiredAccess, 0, &sa_inherit, OPEN_EXISTING, kFlags, NULL);
    if (client_h == INVALID_HANDLE_VALUE) {
        log_line("PARENT", mode, "FAIL: CreateFileW(client_h) failed err=%lu", GetLastError());
        CloseHandle(server_h);
        return 1;
    }

    SetLastError(0);
    BOOL conn_ok = ConnectNamedPipe(server_h, NULL);
    DWORD conn_err = GetLastError();
    DWORD client_pid = 0;
    BOOL cpid_ok = GetNamedPipeClientProcessId(server_h, &client_pid);
    log_line("PARENT", mode, "STAGE1_CHANNEL server=0x%Ix client=0x%Ix ConnectNamedPipe=%d(err=%lu,exp=535) ClientPid=%lu(ok=%d)",
             (uintptr_t)server_h, (uintptr_t)client_h, (int)conn_ok, conn_err, client_pid, (int)cpid_ok);
    if (conn_ok || conn_err != ERROR_PIPE_CONNECTED) {
        log_line("PARENT", mode, "FAIL: ConnectNamedPipe did not return FALSE + ERROR_PIPE_CONNECTED(535)");
        CloseHandle(client_h);
        CloseHandle(server_h);
        return 1;
    }

    /* Create 7 dummy inheritable handles so PROC_THREAD_ATTRIBUTE_HANDLE_LIST has 8 handles like Chromium R5/R6 */
    HANDLE dummy_handles[7] = {0};
    HANDLE handle_list[8] = {0};
    handle_list[0] = client_h;
    for (int i = 0; i < 7; i++) {
        dummy_handles[i] = CreateEventW(&sa_inherit, TRUE, FALSE, NULL);
        handle_list[i + 1] = dummy_handles[i];
    }

    /*
     * STAGE 2: Token creation & suspended child launch (PolicyBase::MakeTokens + TargetProcess::Create)
     */
    HANDLE hBase = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hBase)) {
        log_line("PARENT", mode, "FAIL: OpenProcessToken(TOKEN_ALL_ACCESS) failed err=%lu", GetLastError());
        return 1;
    }

    HANDLE hLockdown = NULL;
    HANDLE hInitial = NULL;
    HANDLE hJob = NULL;
    BOOL is_ac = (strcmp(mode, "network_appcontainer") == 0);
    SECURITY_CAPABILITIES sc;
    ZeroMemory(&sc, sizeof(sc));
    SID_AND_ATTRIBUTES cap_attrs[3];
    ZeroMemory(cap_attrs, sizeof(cap_attrs));
    PSID ac_sid = NULL;

    HMODULE hKbase = GetModuleHandleW(L"kernelbase.dll");
    HMODULE hUserenv = LoadLibraryW(L"userenv.dll");
    pfn_AppContainerRegisterSid pRegSid = hKbase ? (pfn_AppContainerRegisterSid)(void *)GetProcAddress(hKbase, "AppContainerRegisterSid") : NULL;
    if (!pRegSid && hUserenv) pRegSid = (pfn_AppContainerRegisterSid)(void *)GetProcAddress(hUserenv, "AppContainerRegisterSid");
    pfn_AppContainerUnregisterSid pUnregSid = hKbase ? (pfn_AppContainerUnregisterSid)(void *)GetProcAddress(hKbase, "AppContainerUnregisterSid") : NULL;
    if (!pUnregSid && hUserenv) pUnregSid = (pfn_AppContainerUnregisterSid)(void *)GetProcAddress(hUserenv, "AppContainerUnregisterSid");

    if (strcmp(mode, "plain") == 0) {
        if (!DuplicateTokenEx(hBase, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &hLockdown) ||
            !DuplicateTokenEx(hBase, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &hInitial)) {
            log_line("PARENT", mode, "FAIL: DuplicateTokenEx failed err=%lu", GetLastError());
            return 1;
        }
    } else if (strcmp(mode, "service_restricted") == 0) {
        if (!create_chromium_restricted_token(hBase, TRUE, FALSE, L"S-1-16-4096", &hLockdown) ||
            !create_chromium_restricted_token(hBase, FALSE, TRUE, L"S-1-16-4096", &hInitial)) {
            log_line("PARENT", mode, "FAIL: create_chromium_restricted_token(service) failed err=%lu", GetLastError());
            return 1;
        }
    } else if (is_ac) {
        static const wchar_t *cap_sids[] = { L"S-1-15-3-1", L"S-1-15-3-2", L"S-1-15-3-3" };
        /* Full 8-subauthority AppContainer Package SID from R6 NetworkService trace */
        if (!ConvertStringSidToSidW(L"S-1-15-2-3094097068-3553367332-4200492865-707594790-2909876340-3900668381-2918231144", &ac_sid)) {
            log_line("PARENT", mode, "FAIL: ConvertStringSidToSidW(ac_sid) failed err=%lu", GetLastError());
            return 1;
        }
        for (int i = 0; i < 3; i++) {
            if (!ConvertStringSidToSidW(cap_sids[i], &cap_attrs[i].Sid))
                return 1;
            cap_attrs[i].Attributes = SE_GROUP_ENABLED;
        }
        sc.AppContainerSid = ac_sid;
        sc.Capabilities = cap_attrs;
        sc.CapabilityCount = 3;

        if (pRegSid) {
            HRESULT hr = pRegSid(ac_sid, L"wex_mojo_probe_ac", L"WEX Mojo Probe AppContainer");
            log_line("PARENT", mode, "STAGE2_AC_REGISTER hr=0x%08lx", (unsigned long)hr);
        }

        /* Primary lockdown token for NetworkService (AppContainerType::kDerived uses restricted primary token + SECURITY_CAPABILITIES) */
        if (!create_chromium_restricted_token(hBase, TRUE, FALSE, NULL, &hLockdown)) {
            log_line("PARENT", mode, "FAIL: create_chromium_restricted_token(network primary) failed err=%lu", GetLastError());
            return 1;
        }

        /* Rewrite primary token object DACL per sandbox_policy_base.cc:615-618 */
        BOOL rep_ok = replace_package_sid_in_token_dacl(hLockdown, ac_sid, TOKEN_ALL_ACCESS);
        log_line("PARENT", mode, "STAGE2_REPLACE_PACKAGE_SID_DACL ok=%d err=%lu",
                 (int)rep_ok, rep_ok ? 0 : GetLastError());

        /* Impersonation initial token built with CreateAppContainerToken from restricted token (sandbox_policy_base.cc:624-633) */
        pfn_CreateAppContainerToken pCreateAC = hKbase ? (pfn_CreateAppContainerToken)(void *)GetProcAddress(hKbase, "CreateAppContainerToken") : NULL;
        HANDLE hRestrInitial = NULL;
        if (!create_chromium_restricted_token(hBase, TRUE, FALSE, NULL, &hRestrInitial)) {
            log_line("PARENT", mode, "FAIL: create_chromium_restricted_token(network initial base) failed err=%lu", GetLastError());
            return 1;
        }
        HANDLE hAcPrimary = NULL;
        if (!pCreateAC || !pCreateAC(hRestrInitial, &sc, &hAcPrimary)) {
            log_line("PARENT", mode, "FAIL: CreateAppContainerToken failed err=%lu pfn=%p", GetLastError(), (void *)pCreateAC);
            CloseHandle(hRestrInitial);
            return 1;
        }
        CloseHandle(hRestrInitial);
        if (!DuplicateTokenEx(hAcPrimary, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &hInitial)) {
            log_line("PARENT", mode, "FAIL: DuplicateTokenEx(hAcPrimary -> Impersonation) failed err=%lu", GetLastError());
            CloseHandle(hAcPrimary);
            return 1;
        }
        /* Copy hAcPrimary DACL onto hInitial so AppContainer child can open its own thread token (app_container_base.cc:399-415) */
        copy_kernel_object_dacl(hAcPrimary, hInitial);
        CloseHandle(hAcPrimary);

        hJob = CreateJobObjectW(NULL, NULL);
        if (!hJob) {
            log_line("PARENT", mode, "FAIL: CreateJobObjectW failed err=%lu", GetLastError());
            return 1;
        }
    }
    CloseHandle(hBase);

    /* Create pre-resume shared policy section (TargetProcess::Init) */
    HANDLE hSharedMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, sizeof(shared_policy_sec_t), NULL);
    if (!hSharedMap) {
        log_line("PARENT", mode, "FAIL: CreateFileMappingW(shared policy) failed err=%lu", GetLastError());
        return 1;
    }
    shared_policy_sec_t *shared_view = (shared_policy_sec_t *)MapViewOfFile(hSharedMap, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(shared_policy_sec_t));
    if (shared_view) {
        ZeroMemory(shared_view, sizeof(shared_policy_sec_t));
        shared_view->magic = PROBE_SHMEM_MAGIC;
    }

    /* Build STARTUPINFOEXW attribute list */
    DWORD attr_count = 1 + (is_ac ? 2 : 0);
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, attr_count, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attr_size);
    if (!attr_list || !InitializeProcThreadAttributeList(attr_list, attr_count, 0, &attr_size)) {
        log_line("PARENT", mode, "FAIL: InitializeProcThreadAttributeList failed err=%lu", GetLastError());
        return 1;
    }
    if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handle_list, sizeof(handle_list), NULL, NULL)) {
        log_line("PARENT", mode, "FAIL: UpdateProcThreadAttribute(HANDLE_LIST) failed err=%lu", GetLastError());
        return 1;
    }
    if (is_ac) {
        if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &sc, sizeof(sc), NULL, NULL)) {
            log_line("PARENT", mode, "FAIL: UpdateProcThreadAttribute(SECURITY_CAPABILITIES) failed err=%lu", GetLastError());
            return 1;
        }
        if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &hJob, sizeof(HANDLE), NULL, NULL)) {
            log_line("PARENT", mode, "FAIL: UpdateProcThreadAttribute(JOB_LIST) failed err=%lu", GetLastError());
            return 1;
        }
    }

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attr_list;

    wchar_t img[MAX_PATH];
    GetModuleFileNameW(NULL, img, MAX_PATH);
    grant_sandbox_rx_on_path(img, ac_sid);

    wchar_t cmdline[512];
    swprintf(cmdline, 512, L"\"%ls\" --child --mode=%hs --mojo-platform-channel-handle=%Iu",
             img, mode, (uintptr_t)client_h);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    DWORD cp_flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT;
    if (!CreateProcessAsUserW(hLockdown, img, cmdline, NULL, NULL, TRUE, cp_flags, NULL, NULL, &si.StartupInfo, &pi)) {
        log_line("PARENT", mode, "FAIL: CreateProcessAsUserW failed err=%lu", GetLastError());
        return 1;
    }
    log_line("PARENT", mode, "STAGE2_LAUNCHED child_pid=%lu child_tid=%lu mojo_client_h=0x%Ix",
             pi.dwProcessId, pi.dwThreadId, (uintptr_t)client_h);

    /* Close parent's copy of client_h and dummy handles now that child inherited them */
    CloseHandle(client_h);
    for (int i = 0; i < 7; i++) CloseHandle(dummy_handles[i]);

    /* Apply initial impersonation token to suspended main thread and verify (target_process.cc:184-202) */
    if (!SetThreadToken(&pi.hThread, hInitial)) {
        log_line("PARENT", mode, "FAIL: SetThreadToken(initial) failed err=%lu", GetLastError());
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }
    HANDLE hChkImp = NULL;
    SECURITY_IMPERSONATION_LEVEL imp_level = SecurityAnonymous;
    DWORD imp_len = 0;
    BOOL chk_ok = OpenThreadToken(pi.hThread, TOKEN_IMPERSONATE | TOKEN_QUERY | TOKEN_DUPLICATE, TRUE, &hChkImp) &&
                  GetTokenInformation(hChkImp, TokenImpersonationLevel, &imp_level, sizeof(imp_level), &imp_len) &&
                  (imp_level == SecurityImpersonation);
    if (hChkImp) CloseHandle(hChkImp);
    log_line("PARENT", mode, "STAGE2_THREAD_TOKEN SetThreadToken+OpenThreadToken ok=%d imp_level=%d",
             (int)chk_ok, (int)imp_level);
    if (!chk_ok) {
        log_line("PARENT", mode, "FAIL: suspended thread token verification failed");
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Duplicate shared policy section into suspended child (TargetProcess::Init, target_process.cc:221) */
    HANDLE child_shared_section = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), hSharedMap, pi.hProcess, &child_shared_section,
                         FILE_MAP_READ | FILE_MAP_WRITE | SECTION_QUERY, FALSE, 0)) {
        log_line("PARENT", mode, "FAIL: DuplicateHandle(shared_section -> child) failed err=%lu", GetLastError());
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }
    log_line("PARENT", mode, "STAGE2_SHARED_SECTION duplicated into child as 0x%Ix", (uintptr_t)child_shared_section);

    /*
     * STAGE 4 (Parent Setup before/during Resume): Bind server_h to Parent IOCP, post ReadFile,
     * duplicate in-band handles, and send Message #1.
     */
    HANDLE dup_child_proc = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &dup_child_proc, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        log_line("PARENT", mode, "FAIL: DuplicateHandle(child_process) failed err=%lu", GetLastError());
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    HANDLE parent_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!parent_iocp || CreateIoCompletionPort(server_h, parent_iocp, PARENT_IOCP_KEY, 1) != parent_iocp) {
        log_line("PARENT", mode, "FAIL: Parent CreateIoCompletionPort failed err=%lu", GetLastError());
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Post parent's overlapped ReadFile BEFORE resuming child (ChannelWin::StartOnIOThread) */
    mojo_probe_msg_t parent_rx1;
    ZeroMemory(&parent_rx1, sizeof(parent_rx1));
    OVERLAPPED parent_read_ovl1;
    ZeroMemory(&parent_read_ovl1, sizeof(parent_read_ovl1));
    BOOL pr1_ok = ReadFile(server_h, &parent_rx1, sizeof(parent_rx1), NULL, &parent_read_ovl1);
    DWORD pr1_err = pr1_ok ? 0 : GetLastError();
    log_line("PARENT", mode, "STAGE4_PARENT_READ1_PREPOST ok=%d err=%lu", (int)pr1_ok, pr1_err);
    if (!pr1_ok && pr1_err != ERROR_IO_PENDING) {
        log_line("PARENT", mode, "FAIL: Parent ReadFile #1 failed err=%lu", pr1_err);
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Resume child main thread (TargetProcess::Create) */
    DWORD prev_suspend = ResumeThread(pi.hThread);
    log_line("PARENT", mode, "STAGE2_RESUMED prev_suspend=%lu", prev_suspend);

    /* Create in-band handles (Event + SharedMemory) and transfer to child with DUPLICATE_CLOSE_SOURCE */
    HANDLE hLocalEvent = CreateEventW(NULL, TRUE, TRUE, NULL);
    HANDLE hLocalShm = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
    char *shm_p = (char *)MapViewOfFile(hLocalShm, FILE_MAP_WRITE, 0, 0, 64);
    if (shm_p) {
        strcpy(shm_p, "MOJO_SHMEM_PORTAL_OK");
        UnmapViewOfFile(shm_p);
    }
    HANDLE hRemoteEvent = NULL, hRemoteShm = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), hLocalEvent, dup_child_proc, &hRemoteEvent, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE) ||
        !DuplicateHandle(GetCurrentProcess(), hLocalShm, dup_child_proc, &hRemoteShm, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE)) {
        log_line("PARENT", mode, "FAIL: DuplicateHandle(in-band handles -> child) failed err=%lu", GetLastError());
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Send Message #1 (ConnectNode + in-band handles) */
    mojo_probe_msg_t parent_tx1;
    ZeroMemory(&parent_tx1, sizeof(parent_tx1));
    parent_tx1.magic = PROBE_MAGIC_P2C_1;
    parent_tx1.seq = 1;
    parent_tx1.transferred_event_handle = (uint64_t)(uintptr_t)hRemoteEvent;
    parent_tx1.transferred_shmem_handle = (uint64_t)(uintptr_t)hRemoteShm;
    parent_tx1.pre_resume_section_handle = (uint64_t)(uintptr_t)child_shared_section;
    strcpy(parent_tx1.payload, "MOJO_INVITATION_CONNECT_NODE");

    OVERLAPPED parent_write_ovl1;
    ZeroMemory(&parent_write_ovl1, sizeof(parent_write_ovl1));
    BOOL pw1_ok = WriteFile(server_h, &parent_tx1, sizeof(parent_tx1), NULL, &parent_write_ovl1);
    DWORD pw1_err = pw1_ok ? 0 : GetLastError();
    log_line("PARENT", mode, "STAGE4_PARENT_WRITE1_POST ok=%d err=%lu", (int)pw1_ok, pw1_err);
    if (!pw1_ok && pw1_err != ERROR_IO_PENDING) {
        log_line("PARENT", mode, "FAIL: Parent WriteFile #1 failed err=%lu", pw1_err);
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }
    DWORD pw1_bytes = 0;
    if (!wait_iocp_packet(parent_iocp, PARENT_IOCP_KEY, &parent_write_ovl1, &pw1_bytes, "PARENT", mode, "STAGE4_PARENT_WRITE1_IOCP")) {
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Wait for child's reply on parent_read_ovl1 */
    DWORD pr1_bytes = 0;
    if (!wait_iocp_packet(parent_iocp, PARENT_IOCP_KEY, &parent_read_ovl1, &pr1_bytes, "PARENT", mode, "STAGE4_PARENT_READ1_IOCP")) {
        DWORD ec = 0xFFFFFFFFUL;
        WaitForSingleObject(pi.hProcess, 1000);
        GetExitCodeProcess(pi.hProcess, &ec);
        if (shared_view && shared_view->log_len > 0) {
            fwrite(shared_view->child_log, 1, shared_view->log_len, stdout);
            fflush(stdout);
        }
        log_line("PARENT", mode, "FAIL: child exited early with code=0x%lx (%lu)", ec, ec);
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }
    if (pr1_bytes != sizeof(parent_rx1) || parent_rx1.magic != PROBE_MAGIC_C2P_1 || strcmp(parent_rx1.payload, "MOJO_CHILD_ACCEPT_ACK") != 0) {
        log_line("PARENT", mode, "FAIL: Parent ReadFile #1 bad payload bytes=%lu magic=0x%08x", pr1_bytes, parent_rx1.magic);
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Send Message #2 to wake child's pre-posted ReadFile #2 */
    mojo_probe_msg_t parent_tx2;
    ZeroMemory(&parent_tx2, sizeof(parent_tx2));
    parent_tx2.magic = PROBE_MAGIC_P2C_2;
    parent_tx2.seq = 2;
    strcpy(parent_tx2.payload, "MOJO_BROKER_PING2");
    OVERLAPPED parent_write_ovl2;
    ZeroMemory(&parent_write_ovl2, sizeof(parent_write_ovl2));
    BOOL pw2_ok = WriteFile(server_h, &parent_tx2, sizeof(parent_tx2), NULL, &parent_write_ovl2);
    DWORD pw2_err = pw2_ok ? 0 : GetLastError();
    if (!pw2_ok && pw2_err != ERROR_IO_PENDING) {
        log_line("PARENT", mode, "FAIL: Parent WriteFile #2 failed err=%lu", pw2_err);
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }
    DWORD pw2_bytes = 0;
    if (!wait_iocp_packet(parent_iocp, PARENT_IOCP_KEY, &parent_write_ovl2, &pw2_bytes, "PARENT", mode, "STAGE4_PARENT_WRITE2_IOCP")) {
        TerminateProcess(pi.hProcess, 99);
        return 1;
    }

    /* Wait for child exit */
    DWORD wres = WaitForSingleObject(pi.hProcess, WAIT_TIMEOUT_MS);
    DWORD child_exit = 0xFFFFFFFFUL;
    GetExitCodeProcess(pi.hProcess, &child_exit);
    uint32_t child_wrote_pid = shared_view ? shared_view->child_pid : 0;
    if (shared_view && shared_view->log_len > 0) {
        fwrite(shared_view->child_log, 1, shared_view->log_len, stdout);
        fflush(stdout);
    }
    const char *exit_desc = (child_exit == 0) ? "PASS" :
                            (child_exit == 1) ? "SBOX_FATAL_INTEGRITY" :
                            (child_exit == 2) ? "SBOX_FATAL_DROPTOKEN" : "ERROR";
    log_line("PARENT", mode, "STAGE4_CHILD_EXIT wait=%lu exit_code=%lu (%s) shared_section_child_pid=%u",
             wres, child_exit, exit_desc, child_wrote_pid);

    if (shared_view) UnmapViewOfFile(shared_view);
    CloseHandle(hSharedMap);
    CloseHandle(parent_iocp);
    CloseHandle(server_h);
    CloseHandle(dup_child_proc);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hLockdown);
    CloseHandle(hInitial);
    if (hJob) CloseHandle(hJob);
    DeleteProcThreadAttributeList(attr_list);
    HeapFree(GetProcessHeap(), 0, attr_list);
    if (is_ac) {
        if (pUnregSid && ac_sid) pUnregSid(ac_sid);
        if (ac_sid) LocalFree(ac_sid);
        for (int i = 0; i < 3; i++)
            if (cap_attrs[i].Sid) LocalFree(cap_attrs[i].Sid);
    }

    if (wres != WAIT_OBJECT_0 || child_exit != 0 || child_wrote_pid != pi.dwProcessId) {
        log_line("PARENT", mode, "=== MODE %s VERDICT: FAIL (child_exit=%lu) ===", mode, child_exit);
        return 1;
    }
    log_line("PARENT", mode, "=== MODE %s VERDICT: PASS ===", mode);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    BOOL is_child = FALSE;
    const char *mode = "all";
    uintptr_t raw_pipe_h = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--child") == 0) {
            is_child = TRUE;
        } else if (strncmp(argv[i], "--mode=", 7) == 0) {
            mode = argv[i] + 7;
        } else if (strncmp(argv[i], "--mojo-platform-channel-handle=", 31) == 0) {
            raw_pipe_h = (uintptr_t)_strtoui64(argv[i] + 31, NULL, 10);
        } else if (argv[i][0] != '-') {
            mode = argv[i];
        }
    }

    if (is_child) {
        return run_child(mode, raw_pipe_h);
    }

    if (strcmp(mode, "all") == 0) {
        static const char *all_modes[] = {
            "plain",
            "service_restricted",
            "network_appcontainer"
        };
        int failures = 0;
        for (int i = 0; i < 3; i++) {
            if (run_parent_mode(all_modes[i]) != 0)
                failures++;
        }
        printf("[SUMMARY] failures=%d overall=%s\n", failures, failures == 0 ? "PASS" : "FAIL");
        return failures == 0 ? 0 : 1;
    }

    return run_parent_mode(mode);
}
