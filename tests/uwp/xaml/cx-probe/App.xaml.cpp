// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "MainPage.xaml.h"

using namespace XamlProbe;
using namespace Windows::UI::Xaml;

/* one line per call to LocalState\xamlprobe.txt */
void XamlProbe::Log(Platform::String^ line)
{
    auto path = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\xamlprobe.txt";
    FILE *f;
    if (!_wfopen_s(&f, path->Data(), L"a, ccs=UTF-8"))
    {
        fwprintf(f, L"%s\n", line->Data());
        fclose(f);
    }
}

/* what Application.Resources holds: its size and the two resources of App.xaml */
Platform::String^ XamlProbe::ResourcesSummary()
{
    try
    {
        auto r = Application::Current->Resources;
        if (!r) return L"null";
        Platform::String^ s = L"size " + r->Size.ToString();
        s += r->HasKey(L"ProbeWidth") ? L", ProbeWidth " + r->Lookup(L"ProbeWidth")->ToString() : L", no ProbeWidth";
        s += r->HasKey(L"ProbeBrush") ? L", ProbeBrush set" : L", no ProbeBrush";
        return s;
    }
    catch (Platform::Exception^ e) { return L"failed " + e->HResult.ToString(); }
}

App::App()
{
    Log(L"app constructed");
    InitializeComponent();
    Log(L"app resources after InitializeComponent: " + ResourcesSummary());
    Suspending += ref new SuspendingEventHandler(this, &App::OnSuspending);
    Resuming += ref new Windows::Foundation::EventHandler<Platform::Object^>(this, &App::OnResuming);
}

/* the suspend runs (plm.exe on Windows, a minimized CoreWindow on Wine): sender, deadline, deferral */
void App::OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ e)
{
    auto deferral = e->SuspendingOperation->GetDeferral();
    /* DateTime.UniversalTime counts 100 ns since 1601, like FILETIME (Windows.Globalization.Calendar is not used:
     * Wine does not have it yet) */
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    auto left = e->SuspendingOperation->Deadline.UniversalTime - (((long long)now.dwHighDateTime << 32) | now.dwLowDateTime);
    Log(L"suspending, sender is the application " + (sender == Application::Current ? L"yes" : L"no") +
        L", deadline in " + (left / 10000000).ToString() + L" s");
    deferral->Complete();
}

void App::OnResuming(Platform::Object^ sender, Platform::Object^)
{
    Log(L"resuming, sender is the application " + (sender == Application::Current ? L"yes" : L"no"));
}

void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e)
{
    Log(L"launched");
    Log(L"app resources at launch: " + ResourcesSummary());
    RunLaunchProbe(e);
    auto frame = ref new Controls::Frame();
    Window::Current->Content = frame;
    frame->Navigate(Interop::TypeName(MainPage::typeid), e->Arguments);
    Window::Current->Activate();
}
