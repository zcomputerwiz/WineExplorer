#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

typedef BOOL (WINAPI *pfnCreateAppContainerToken)(HANDLE, PSECURITY_CAPABILITIES, PHANDLE);

static void run_case(
    int case_num,
    const char *case_name,
    PSECURITY_DESCRIPTOR pSD,
    HANDLE hToken,
    const char *token_name,
    DWORD desired_access
)
{
    GENERIC_MAPPING gm;
    PRIVILEGE_SET priv_set;
    DWORD priv_set_len = sizeof(priv_set);
    DWORD granted_access = 0;
    BOOL access_status = FALSE;
    BOOL ret;
    DWORD err;

    gm.GenericRead = FILE_GENERIC_READ;
    gm.GenericWrite = FILE_GENERIC_WRITE;
    gm.GenericExecute = FILE_GENERIC_EXECUTE;
    gm.GenericAll = FILE_ALL_ACCESS;

    SetLastError(0);
    ret = AccessCheck(
        pSD,
        hToken,
        desired_access,
        &gm,
        &priv_set,
        &priv_set_len,
        &granted_access,
        &access_status
    );
    err = GetLastError();

    printf("[PROBE] Case=%d Desc=\"%s\" Token=\"%s\" Desired=0x%08lX Ret=%d Status=%d Granted=0x%08lX Err=%lu\n",
           case_num, case_name, token_name, desired_access, ret, access_status, granted_access, err);
}

int main(void)
{
    HANDLE hProcToken = NULL;
    HANDLE hNormalImp = NULL;
    HANDLE hAcPrimary = NULL;
    HANDLE hAcImp = NULL;
    TOKEN_USER *pUser = NULL;
    DWORD user_len = 0;
    LPWSTR user_sid_str = NULL;
    pfnCreateAppContainerToken pCreateAppContainerToken = NULL;
    PSID package_sid = NULL;
    SECURITY_CAPABILITIES sec_caps;
    HMODULE hMod;
    HRESULT hr;
    WCHAR sddl[1024];
    PSECURITY_DESCRIPTOR pSD_user_only = NULL;
    PSECURITY_DESCRIPTOR pSD_app_packages = NULL;
    PSECURITY_DESCRIPTOR pSD_package_sid = NULL;
    DWORD is_ac = 0, is_ac_len = sizeof(is_ac);
    BOOL bOk;

    printf("================================================================================\n");
    printf("AccessCheck Lowbox Boundary Empirical Probe (Native Win32 PE)\n");
    printf("================================================================================\n");

    /* 1. Open current process token */
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hProcToken))
    {
        printf("[FATAL] OpenProcessToken failed: %lu\n", GetLastError());
        return 1;
    }

    /* 2. Get user SID */
    GetTokenInformation(hProcToken, TokenUser, NULL, 0, &user_len);
    pUser = (TOKEN_USER *)malloc(user_len);
    if (!GetTokenInformation(hProcToken, TokenUser, pUser, user_len, &user_len) ||
        !ConvertSidToStringSidW(pUser->User.Sid, &user_sid_str))
    {
        printf("[FATAL] GetTokenInformation(TokenUser) failed: %lu\n", GetLastError());
        return 1;
    }
    printf("[INFO] Current User SID: %ls\n", user_sid_str);

    /* 3. Create Normal Impersonation Token */
    if (!DuplicateTokenEx(hProcToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &hNormalImp))
    {
        printf("[FATAL] DuplicateTokenEx(Normal) failed: %lu\n", GetLastError());
        return 1;
    }

    /* 4. Resolve CreateAppContainerToken */
    hMod = GetModuleHandleW(L"kernelbase.dll");
    if (hMod) pCreateAppContainerToken = (pfnCreateAppContainerToken)GetProcAddress(hMod, "CreateAppContainerToken");
    if (!pCreateAppContainerToken)
    {
        hMod = LoadLibraryW(L"kernelbase.dll");
        if (hMod) pCreateAppContainerToken = (pfnCreateAppContainerToken)GetProcAddress(hMod, "CreateAppContainerToken");
    }
    if (!pCreateAppContainerToken)
    {
        printf("[FATAL] CreateAppContainerToken not found\n");
        return 1;
    }

    /* 5. Create AppContainer Token */
    {
        typedef HRESULT (WINAPI *pfnDeriveAppContainerSid)(PCWSTR, PSID *);
        typedef HRESULT (WINAPI *pfnCreateProfile)(PCWSTR, PCWSTR, PCWSTR, SID_AND_ATTRIBUTES *, DWORD, PSID *);
        typedef HRESULT (WINAPI *pfnDeleteProfile)(PCWSTR);
        HMODULE hUserEnv = LoadLibraryW(L"userenv.dll");
        pfnDeriveAppContainerSid pDerive = NULL;
        pfnCreateProfile pCreateProfile = NULL;
        pfnDeleteProfile pDeleteProfile = NULL;
        WCHAR *pkg_sid_str = NULL;

        if (hUserEnv)
        {
            pDerive = (pfnDeriveAppContainerSid)GetProcAddress(hUserEnv, "DeriveAppContainerSidFromAppContainerName");
            pCreateProfile = (pfnCreateProfile)GetProcAddress(hUserEnv, "CreateAppContainerProfile");
            pDeleteProfile = (pfnDeleteProfile)GetProcAddress(hUserEnv, "DeleteAppContainerProfile");
        }

        if (pCreateProfile)
        {
            if (pDeleteProfile) pDeleteProfile(L"WexAccessProbeTest");
            hr = pCreateProfile(L"WexAccessProbeTest", L"Probe", L"Desc", NULL, 0, &package_sid);
        }
        if (!package_sid && pDerive)
        {
            hr = pDerive(L"WexAccessProbeTest", &package_sid);
        }
        if (!package_sid)
        {
            ConvertStringSidToSidW(L"S-1-15-2-12345-67890-99999", &package_sid);
        }
        if (ConvertSidToStringSidW(package_sid, &pkg_sid_str))
        {
            printf("[INFO] Target AppContainer Package SID: %ls\n", pkg_sid_str);
            LocalFree(pkg_sid_str);
        }
    }

    memset(&sec_caps, 0, sizeof(sec_caps));
    sec_caps.AppContainerSid = package_sid;
    sec_caps.Capabilities = NULL;
    sec_caps.CapabilityCount = 0;

    /* If first parameter is NULL, CreateAppContainerToken derives from caller's token */
    SetLastError(0);
    bOk = pCreateAppContainerToken(NULL, &sec_caps, &hAcPrimary);
    if (!bOk)
    {
        /* Try with hProcToken as fallback */
        bOk = pCreateAppContainerToken(hProcToken, &sec_caps, &hAcPrimary);
    }
    if (!bOk || !hAcPrimary)
    {
        printf("[FATAL] CreateAppContainerToken failed: bOk=%d err=%lu\n", bOk, GetLastError());
        return 1;
    }

    /* Check TokenIsAppContainer */
    if (GetTokenInformation(hAcPrimary, (TOKEN_INFORMATION_CLASS)29 /* TokenIsAppContainer */, &is_ac, sizeof(is_ac), &is_ac_len))
        printf("[INFO] AppContainer Primary Token: TokenIsAppContainer=%lu\n", is_ac);
    else
        printf("[WARN] GetTokenInformation(TokenIsAppContainer) err=%lu\n", GetLastError());

    /* Duplicate AppContainer token to Impersonation token for AccessCheck */
    if (!DuplicateTokenEx(hAcPrimary, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &hAcImp))
    {
        printf("[FATAL] DuplicateTokenEx(AppContainer) failed: %lu\n", GetLastError());
        return 1;
    }

    /* 6. Build Security Descriptors */
    /* Case 1: Ordinary-User-Only DACL */
    swprintf(sddl, ARRAY_SIZE(sddl), L"O:%lsG:%lsD:(A;;FR;;;%ls)", user_sid_str, user_sid_str, user_sid_str);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &pSD_user_only, NULL))
    {
        printf("[FATAL] ConvertStringSecurityDescriptor (User-Only) failed: %lu\n", GetLastError());
        return 1;
    }

    /* Case 2: DACL with ALL APPLICATION PACKAGES (S-1-15-2-1 / AC) */
    swprintf(sddl, ARRAY_SIZE(sddl), L"O:%lsG:%lsD:(A;;FR;;;%ls)(A;;FR;;;AC)", user_sid_str, user_sid_str, user_sid_str);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &pSD_app_packages, NULL))
    {
        printf("[FATAL] ConvertStringSecurityDescriptor (ALL APP PKGS) failed: %lu\n", GetLastError());
        return 1;
    }

    /* Case 3: DACL with Specific Package SID */
    {
        WCHAR *pkg_sid_str = NULL;
        ConvertSidToStringSidW(package_sid, &pkg_sid_str);
        swprintf(sddl, ARRAY_SIZE(sddl), L"O:%lsG:%lsD:(A;;FR;;;%ls)(A;;FR;;;%ls)", user_sid_str, user_sid_str, user_sid_str, pkg_sid_str ? pkg_sid_str : L"S-1-15-2-1");
        if (pkg_sid_str) LocalFree(pkg_sid_str);
    }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &pSD_package_sid, NULL))
    {
        printf("[FATAL] ConvertStringSecurityDescriptor (Package SID) failed: %lu\n", GetLastError());
        return 1;
    }

    printf("[INFO] Security descriptors constructed successfully.\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("Evaluating AccessCheck across both tokens and 3 DACL configurations\n");
    printf("--------------------------------------------------------------------------------\n");

    /* Case 1: User-Only DACL */
    run_case(1, "User-Only DACL (GENERIC_READ)", pSD_user_only, hNormalImp, "Normal", FILE_GENERIC_READ);
    run_case(1, "User-Only DACL (GENERIC_READ)", pSD_user_only, hAcImp, "AppContainer", FILE_GENERIC_READ);
    run_case(1, "User-Only DACL (MAXIMUM_ALLOWED)", pSD_user_only, hNormalImp, "Normal", MAXIMUM_ALLOWED);
    run_case(1, "User-Only DACL (MAXIMUM_ALLOWED)", pSD_user_only, hAcImp, "AppContainer", MAXIMUM_ALLOWED);

    /* Case 2: User + ALL APPLICATION PACKAGES */
    run_case(2, "ALL APPLICATION PACKAGES (GENERIC_READ)", pSD_app_packages, hNormalImp, "Normal", FILE_GENERIC_READ);
    run_case(2, "ALL APPLICATION PACKAGES (GENERIC_READ)", pSD_app_packages, hAcImp, "AppContainer", FILE_GENERIC_READ);
    run_case(2, "ALL APPLICATION PACKAGES (MAXIMUM_ALLOWED)", pSD_app_packages, hNormalImp, "Normal", MAXIMUM_ALLOWED);
    run_case(2, "ALL APPLICATION PACKAGES (MAXALLOWED)", pSD_app_packages, hAcImp, "AppContainer", MAXIMUM_ALLOWED);

    /* Case 3: User + Specific Package SID */
    run_case(3, "Specific Package SID (GENERIC_READ)", pSD_package_sid, hNormalImp, "Normal", FILE_GENERIC_READ);
    run_case(3, "Specific Package SID (GENERIC_READ)", pSD_package_sid, hAcImp, "AppContainer", FILE_GENERIC_READ);
    run_case(3, "Specific Package SID (MAXIMUM_ALLOWED)", pSD_package_sid, hNormalImp, "Normal", MAXIMUM_ALLOWED);
    run_case(3, "Specific Package SID (MAXIMUM_ALLOWED)", pSD_package_sid, hAcImp, "AppContainer", MAXIMUM_ALLOWED);

    printf("================================================================================\n");
    printf("PROBE_COMPLETE\n");

    /* Cleanup */
    if (pSD_user_only) LocalFree(pSD_user_only);
    if (pSD_app_packages) LocalFree(pSD_app_packages);
    if (pSD_package_sid) LocalFree(pSD_package_sid);
    if (package_sid) FreeSid(package_sid);
    if (user_sid_str) LocalFree(user_sid_str);
    if (pUser) free(pUser);
    if (hNormalImp) CloseHandle(hNormalImp);
    if (hAcImp) CloseHandle(hAcImp);
    if (hAcPrimary) CloseHandle(hAcPrimary);
    if (hProcToken) CloseHandle(hProcToken);

    return 0;
}
