/*
 * appcontainer_profile_repro.c — WO-20261028-CHROMIUM-BROWSER-BOUNDARY, Task 1.
 *
 * Focused Win32 PE repro for userenv!CreateAppContainerProfile / DeleteAppContainerProfile,
 * the exact primitive Chromium's LPAC network-service launch depends on:
 *   sandbox/policy/win/sandbox_win.cc  GeneratePolicyForSandboxedProcess -> SetLowBox("cr.sb.net")
 *     -> CreateAppContainerProfile(name, ..., &sid)
 * Under SandboxType::kNetwork (Low Privilege AppContainer) the browser process calls this before
 * spawning network.mojom.NetworkService. In the project Wine candidate it was an unfulfilled stub
 * returning E_NOTIMPL, so policy generation failed and no process was ever spawned; Chromium then
 * logged "Network service crashed or was terminated, restarting service." and fell back to
 * --service-sandbox-type=none roughly 1.1 s after the document navigation had already aborted.
 *
 * Behavior under test (both functions resolved via GetProcAddress so the PE loads on every build):
 *   - CreateAppContainerProfile("cr.sb.net.test", ...) must return S_OK (or
 *     HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) and populate a non-NULL AppContainer SID whose
 *     string form begins "S-1-15-2-" (the AppContainer SID authority/revision prefix).
 *   - DeleteAppContainerProfile("cr.sb.net.test") must be present and succeed (or report the
 *     profile as already absent).
 *
 * Expected results:
 *   - native Windows 10 (NT 10.0.26300): PASS, exit 0.
 *   - project Wine candidate BEFORE the fix: CreateAppContainerProfile returns E_NOTIMPL -> exit 2.
 *   - project Wine candidate AFTER the fix: PASS, exit 0.
 *
 * Build once with mingw and run the identical binary on both platforms:
 *   x86_64-w64-mingw32-gcc -O2 -static -o appcontainer_profile_repro.exe appcontainer_profile_repro.c
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

/* Some MinGW-w64 header sets omit this Win2000+ SID helper; declare it explicitly so the repro
 * builds portably (matches the standard winbase.h prototype). */
extern BOOL WINAPI ConvertSidToStringSidW(PSID Sid, LPWSTR *StringSid);

typedef HRESULT (WINAPI *PFN_CreateAppContainerProfile)(PCWSTR, PCWSTR, PCWSTR,
                                                        SID_AND_ATTRIBUTES *, DWORD, SID **);
typedef HRESULT (WINAPI *PFN_DeleteAppContainerProfile)(PCWSTR);

static const WCHAR *NAME = L"cr.sb.net.test";

int main(void)
{
    FILE *out = stdout;
    HMODULE h = GetModuleHandleW(L"userenv.dll");
    if (!h) h = LoadLibraryW(L"userenv.dll");
    if (!h) { fprintf(out, "[repro] FAIL: userenv.dll not loaded err=%lu\n", GetLastError()); return 10; }

    PFN_CreateAppContainerProfile pCreate =
        (PFN_CreateAppContainerProfile)(void *)GetProcAddress(h, "CreateAppContainerProfile");
    PFN_DeleteAppContainerProfile pDelete =
        (PFN_DeleteAppContainerProfile)(void *)GetProcAddress(h, "DeleteAppContainerProfile");

    fprintf(out, "[repro] userenv!CreateAppContainerProfile=%p DeleteAppContainerProfile=%p\n",
            (void *)pCreate, (void *)pDelete);
    if (!pCreate) { fprintf(out, "[repro] FAIL: CreateAppContainerProfile not exported\n"); return 11; }

    SID *sid = NULL;
    LPWSTR sid_str = NULL;
    HRESULT hr;

    fprintf(out, "[repro] CreateAppContainerProfile(\"%ls\")\n", NAME);
    hr = pCreate(NAME, NAME, L"Test Description", NULL, 0, &sid);
    fprintf(out, "[repro]   hr=0x%08x sid=%p\n", (unsigned)hr, (void *)sid);

    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
        fprintf(out, "[repro] FAIL: CreateAppContainerProfile hr=0x%08x%s\n", (unsigned)hr,
                (hr == E_NOTIMPL) ? " (E_NOTIMPL: unimplemented stub)" : "");
        return 2;
    }
    if (!sid) { fprintf(out, "[repro] FAIL: hr=0x%08x but *container_sid is NULL\n", (unsigned)hr); return 3; }

    if (!ConvertSidToStringSidW(sid, &sid_str)) {
        fprintf(out, "[repro] FAIL: ConvertSidToStringSidW err=%lu\n", GetLastError());
        FreeSid(sid);
        return 4;
    }
    fprintf(out, "[repro]   sid = %ls\n", sid_str);
    if (wcsncmp(sid_str, L"S-1-15-2-", 9) != 0) {
        fprintf(out, "[repro] FAIL: SID not in AppContainer form S-1-15-2-... (got %ls)\n", sid_str);
        LocalFree(sid_str); FreeSid(sid);
        return 5;
    }

    if (!pDelete) {
        fprintf(out, "[repro] FAIL: DeleteAppContainerProfile not exported\n");
        LocalFree(sid_str); FreeSid(sid);
        return 7;
    }
    fprintf(out, "[repro] DeleteAppContainerProfile(\"%ls\")\n", NAME);
    hr = pDelete(NAME);
    fprintf(out, "[repro]   hr=0x%08x\n", (unsigned)hr);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        fprintf(out, "[repro] FAIL: DeleteAppContainerProfile hr=0x%08x%s\n", (unsigned)hr,
                (hr == E_NOTIMPL) ? " (E_NOTIMPL)" : "");
        LocalFree(sid_str); FreeSid(sid);
        return 6;
    }

    LocalFree(sid_str);
    FreeSid(sid);
    fprintf(out, "[repro] PASS\n");
    return 0;
}
