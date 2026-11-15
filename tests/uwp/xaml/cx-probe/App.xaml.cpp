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

App::App()
{
    Log(L"app constructed");
    InitializeComponent();
}

void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e)
{
    Log(L"launched");
    auto frame = ref new Controls::Frame();
    Window::Current->Content = frame;
    frame->Navigate(Interop::TypeName(MainPage::typeid), e->Arguments);
    Window::Current->Activate();
}
