/*
 * Focused Win32 PE reproduction for kernelbase!CreateAppContainerToken.
 *
 * Reproduces the exact mechanism used by Chromium's base/win/access_token.cc:753
 * (AccessToken::CreateAppContainer) to create lowbox tokens for sandboxed processes
 * (including SandboxType::kNetwork / utility!kNetwork).
 *
 * Expected behavior:
 * - Native Windows 10 (NT 10.0.26300+): GetProcAddress succeeds, CreateAppContainerToken returns TRUE, exit 0.
 * - Project Wine candidate (pre-fix): CreateAppContainerToken is not exported in kernelbase.spec, GetProcAddress returns NULL, exit 2.
 */

#include <windows.h>
#include <userenv.h>
#include <stdio.h>

typedef BOOL (WINAPI *pfnCreateAppContainerToken)(HANDLE, PSECURITY_CAPABILITIES, PHANDLE);

int main(void) {
    HMODULE hKb = LoadLibraryW(L"kernelbase.dll");
    if (!hKb) {
        printf("Failed to load kernelbase.dll\n");
        return 1;
    }
    pfnCreateAppContainerToken pCreateAppContainerToken =
        (pfnCreateAppContainerToken)GetProcAddress(hKb, "CreateAppContainerToken");
    if (!pCreateAppContainerToken) {
        printf("GetProcAddress(kernelbase, CreateAppContainerToken) FAILED\n");
        return 2;
    }
    printf("Resolved kernelbase!CreateAppContainerToken at %p\n", (void*)pCreateAppContainerToken);

    PSID sid = NULL;
    HRESULT hr = DeriveAppContainerSidFromAppContainerName(L"cr.sb.net.test", &sid);
    if (FAILED(hr) || !sid) {
        printf("DeriveAppContainerSidFromAppContainerName failed: 0x%08lx\n", hr);
        return 3;
    }
    printf("Derived SID successfully\n");

    SECURITY_CAPABILITIES sec_cap;
    memset(&sec_cap, 0, sizeof(sec_cap));
    sec_cap.AppContainerSid = sid;
    sec_cap.Capabilities = NULL;
    sec_cap.CapabilityCount = 0;

    HANDLE token = NULL;
    BOOL ok = pCreateAppContainerToken(NULL, &sec_cap, &token);
    if (!ok) {
        DWORD err = GetLastError();
        printf("CreateAppContainerToken FAILED with error %lu\n", err);
        FreeSid(sid);
        return 4;
    }

    printf("SUCCESS: CreateAppContainerToken created token handle %p\n", token);
    CloseHandle(token);
    FreeSid(sid);
    return 0;
}
