// Windows 11 probe of the classes added to Wine in the user activity / jump list / capabilities batch (unpackaged).
#include <windows.h>
#include <stdio.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.UserActivities.h>
#include <winrt/Windows.UI.StartScreen.h>
#include <winrt/Windows.System.Display.h>
#include <winrt/Windows.Devices.Input.h>

using namespace winrt;
using namespace Windows::ApplicationModel::UserActivities;
using namespace Windows::UI::StartScreen;
using namespace Windows::System::Display;
using namespace Windows::Devices::Input;

#define TRY(label, expr) do { try { expr; } catch (hresult_error const &e) { printf("%s: hr %08lx\n", label, (unsigned long)e.code().value); } } while (0)

int main()
{
    init_apartment(apartment_type::multi_threaded);

    TRY("keyboard", { KeyboardCapabilities c; printf("keyboard present %d\n", c.KeyboardPresent()); });
    TRY("mouse", { MouseCapabilities c; printf("mouse present %d vwheel %d hwheel %d swap %d buttons %u\n", c.MousePresent(),
        c.VerticalWheelPresent(), c.HorizontalWheelPresent(), c.SwapButtons(), c.NumberOfButtons()); });
    TRY("touch", { TouchCapabilities c; printf("touch present %d contacts %u\n", c.TouchPresent(), c.Contacts()); });

    TRY("displayrequest", {
        DisplayRequest r;
        try { r.RequestRelease(); printf("release without request: ok\n"); }
        catch (hresult_error const &e) { printf("release without request: hr %08lx\n", (unsigned long)e.code().value); }
        r.RequestActive(); r.RequestActive(); r.RequestRelease(); r.RequestRelease();
        try { r.RequestRelease(); printf("third release: ok\n"); }
        catch (hresult_error const &e) { printf("third release: hr %08lx\n", (unsigned long)e.code().value); }
    });

    TRY("channel", {
        auto channel = UserActivityChannel::GetDefault();
        printf("channel %d\n", channel ? 1 : 0);
        TRY("channel2", { auto c2 = UserActivityChannel::GetForUser(Windows::System::User::GetDefault()); printf("foruser\n"); });
        channel.DeleteAllActivitiesAsync().get();
        try { auto a = channel.GetOrCreateUserActivityAsync(L"").get(); printf("empty id: ok\n"); }
        catch (hresult_error const &e) { printf("empty id: hr %08lx\n", (unsigned long)e.code().value); }
        auto a = channel.GetOrCreateUserActivityAsync(L"wine-probe").get();
        printf("state %d id %ls\n", (int)a.State(), a.ActivityId().c_str());
        a.VisualElements().DisplayText(L"Calculation");
        printf("display text %ls\n", a.VisualElements().DisplayText().c_str());
        a.ActivationUri(Windows::Foundation::Uri(L"wine-probe://x"));
        try { a.SaveAsync().get(); printf("saved, state %d\n", (int)a.State()); }
        catch (hresult_error const &e) { printf("save: hr %08lx\n", (unsigned long)e.code().value); }
        auto b = channel.GetOrCreateUserActivityAsync(L"wine-probe").get();
        printf("again: same object %d, state %d\n", (int)(winrt::get_abi(a) == winrt::get_abi(b)), (int)b.State());
        try { auto s = a.CreateSession(); printf("session id %ls\n", s.ActivityId().c_str()); s.Close(); }
        catch (hresult_error const &e) { printf("session: hr %08lx\n", (unsigned long)e.code().value); }
        channel.DeleteActivityAsync(L"wine-probe").get();
        auto c = channel.GetOrCreateUserActivityAsync(L"wine-probe").get();
        printf("after delete: state %d\n", (int)c.State());
        try { printf("json %ls\n", a.ToJson().c_str()); } catch (hresult_error const &e) { printf("json: hr %08lx\n", (unsigned long)e.code().value); }
        channel.DeleteAllActivitiesAsync().get();
    });

    TRY("jumplist", {
        printf("supported %d\n", (int)JumpList::IsSupported());
        auto list = JumpList::LoadCurrentAsync().get();
        printf("loaded items %u group %d\n", list.Items().Size(), (int)list.SystemGroupKind());
        auto item = JumpListItem::CreateWithArguments(L"--new", L"New window");
        item.GroupName(L"Tasks");
        printf("item kind %d args %ls name %ls removed %d\n", (int)item.Kind(), item.Arguments().c_str(), item.DisplayName().c_str(), (int)item.RemovedByUser());
        auto sep = JumpListItem::CreateSeparator();
        try { sep.DisplayName(L"x"); printf("separator name set: ok\n"); }
        catch (hresult_error const &e) { printf("separator name set: hr %08lx\n", (unsigned long)e.code().value); }
        try { JumpListItem::CreateWithArguments(L"--x", L""); printf("empty name: ok\n"); }
        catch (hresult_error const &e) { printf("empty name: hr %08lx\n", (unsigned long)e.code().value); }
        list.Items().Append(item);
        list.Items().Append(sep);
        try { list.SaveAsync().get(); printf("saved\n"); }
        catch (hresult_error const &e) { printf("save: hr %08lx\n", (unsigned long)e.code().value); }
        auto again = JumpList::LoadCurrentAsync().get();
        printf("reloaded items %u\n", again.Items().Size());
        again.Items().Clear();
        try { again.SaveAsync().get(); } catch (hresult_error const &) {}
    });
    return 0;
}
