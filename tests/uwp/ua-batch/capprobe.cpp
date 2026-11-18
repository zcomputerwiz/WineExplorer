// Windows 11 probe of Windows.Security.Authorization.AppCapabilityAccess.AppCapability for an unpackaged desktop app.
#include <windows.h>
#include <stdio.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

using namespace winrt;
using namespace Windows::Security::Authorization::AppCapabilityAccess;

#define TRY(label, ...) do { try { __VA_ARGS__; } catch (hresult_error const &e) { printf("%s: hr %08lx\n", label, (unsigned long)e.code().value); } } while (0)

int main()
{
    init_apartment(apartment_type::multi_threaded);
    for (auto name : { L"internetClient", L"webcam", L"microphone", L"location", L"contacts", L"bogusCapabilityName", L"" })
    {
        TRY("create", {
            auto c = AppCapability::Create(name);
            printf("[%ls] name '%ls' user %d ", name, c.CapabilityName().c_str(), c.User() ? 1 : 0);
            printf("check %d ", (int)c.CheckAccess());
            printf("request %d\n", (int)c.RequestAccessAsync().get());
        });
    }
    TRY("create null", { auto c = AppCapability::Create(hstring()); (void)c; printf("create with empty ok\n"); });
    TRY("withpid", {
        auto user = Windows::System::User::FindAllAsync().get().GetAt(0);
        auto c = AppCapability::CreateWithProcessIdForUser(user, L"webcam", GetCurrentProcessId());
        printf("withpid check %d\n", (int)c.CheckAccess());
        auto c2 = AppCapability::CreateWithProcessIdForUser(user, L"webcam", 0);
        printf("pid 0 check %d\n", (int)c2.CheckAccess());
    });
    TRY("map", {
        auto names = winrt::single_threaded_vector<hstring>({ L"internetClient", L"webcam" });
        auto map = AppCapability::RequestAccessForCapabilitiesAsync(names).get();
        printf("map size %u\n", map.Size());
        for (auto const &kv : map) printf("  %ls = %d\n", kv.Key().c_str(), (int)kv.Value());
    });
    TRY("displaymessage", { auto c = AppCapability::Create(L"webcam"); c.DisplayMessage(L"hi"); printf("displaymessage %ls\n", c.DisplayMessage().c_str()); });
    TRY("event", { auto c = AppCapability::Create(L"webcam"); auto t = c.AccessChanged([](auto &&, auto &&) {}); c.AccessChanged(t); printf("event ok\n"); });
    return 0;
}
