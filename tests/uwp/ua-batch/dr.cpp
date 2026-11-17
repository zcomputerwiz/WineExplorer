#include <windows.h>
#include <stdio.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Display.h>
using namespace winrt;
using namespace Windows::System::Display;
int main()
{
    init_apartment(apartment_type::single_threaded);
    try {
        DisplayRequest r;
        try { r.RequestRelease(); printf("release without request: ok\n"); }
        catch (hresult_error const &e) { printf("release without request: hr %08lx\n", (unsigned long)e.code().value); }
        r.RequestActive(); r.RequestActive(); r.RequestRelease(); r.RequestRelease();
        try { r.RequestRelease(); printf("third release: ok\n"); }
        catch (hresult_error const &e) { printf("third release: hr %08lx\n", (unsigned long)e.code().value); }
        DisplayRequest r2;
        try { r2.RequestActive(); r.RequestRelease(); printf("release of another object's request: ok\n"); }
        catch (hresult_error const &e) { printf("release of another object's request: hr %08lx\n", (unsigned long)e.code().value); }
    } catch (hresult_error const &e) { printf("displayrequest: hr %08lx\n", (unsigned long)e.code().value); }
    return 0;
}
