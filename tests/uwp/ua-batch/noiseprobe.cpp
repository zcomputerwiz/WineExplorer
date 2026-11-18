// Windows 11 probe of the small settings classes (unpackaged desktop app).
#include <windows.h>
#include <stdio.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.Management.Core.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt;
#define TRY(label, ...) do { try { __VA_ARGS__; } catch (hresult_error const &e) { printf("%s: hr %08lx\n", label, (unsigned long)e.code().value); } } while (0)
int main()
{
    init_apartment(apartment_type::multi_threaded);
    TRY("education", printf("education %d\n", (int)Windows::System::Profile::EducationSettings::IsEducationEnvironment()));
    TRY("platformdiag", {
        using namespace Windows::System::Profile;
        printf("collection level %d\n", (int)PlatformDiagnosticsAndUsageDataSettings::CollectionLevel());
        for (int l = 0; l <= 3; l++) printf("  can collect %d: %d\n", l, (int)PlatformDiagnosticsAndUsageDataSettings::CanCollectDiagnostics((PlatformDataCollectionLevel)l));
        auto t = PlatformDiagnosticsAndUsageDataSettings::CollectionLevelChanged([](auto &&, auto &&) {});
        PlatformDiagnosticsAndUsageDataSettings::CollectionLevelChanged(t);
        printf("event ok\n");
    });
    TRY("assigned", { auto a = Windows::System::UserProfile::AssignedAccessSettings::GetDefault(); printf("assigned enabled %d kiosk %d user %d\n", (int)a.IsEnabled(), (int)a.IsSingleAppKioskMode(), a.User() ? 1 : 0); });
    TRY("assigned user", { auto u = Windows::System::User::FindAllAsync().get().GetAt(0); auto a = Windows::System::UserProfile::AssignedAccessSettings::GetForUser(u); printf("assigned for user ok %d\n", a.User() ? 1 : 0); });
    TRY("assigned null", { auto a = Windows::System::UserProfile::AssignedAccessSettings::GetForUser(nullptr); printf("assigned null user ok\n"); });
    TRY("diag", { auto d = Windows::System::UserProfile::DiagnosticsSettings::GetDefault(); printf("diag tailor %d user %d\n", (int)d.CanUseDiagnosticsToTailorExperiences(), d.User() ? 1 : 0); });
    TRY("diag null", { auto d = Windows::System::UserProfile::DiagnosticsSettings::GetForUser(nullptr); printf("diag null user ok\n"); });
    TRY("appdatamanager", { auto d = Windows::Management::Core::ApplicationDataManager::CreateForPackageFamily(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe"); printf("manager ok %d\n", d ? 1 : 0); });
    TRY("appdatamanager bogus", { auto d = Windows::Management::Core::ApplicationDataManager::CreateForPackageFamily(L"Bogus.Family_0123456789abc"); printf("manager bogus ok %d\n", d ? 1 : 0); });
    TRY("appdatamanager empty", { auto d = Windows::Management::Core::ApplicationDataManager::CreateForPackageFamily(L""); printf("manager empty ok\n"); });
    TRY("pen", { auto p = Windows::Devices::Input::PenDevice::GetFromPointerId(1); printf("pen for pointer 1: %d\n", p ? 1 : 0); });
    TRY("pen0", { auto p = Windows::Devices::Input::PenDevice::GetFromPointerId(0); printf("pen for pointer 0: %d\n", p ? 1 : 0); });
    return 0;
}
