// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "SecondPage.xaml.h"
#include "ControlsPage.xaml.h"

using namespace XamlProbe;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;

/* the page navigated to from MainPage: reports, then navigates to ControlsPage */
SecondPage::SecondPage()
{
    InitializeComponent();
    Log(L"second page constructed");
    Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^)
    {
        Log(L"second page loaded, frame back stack " + Frame->BackStackDepth.ToString() + L", note " + Note->ActualWidth.ToString());
        timer = ref new DispatcherTimer();
        TimeSpan span;
        span.Duration = 10000000;
        timer->Interval = span;
        timer->Tick += ref new EventHandler<Platform::Object^>(this, &SecondPage::Finish);
        timer->Start();
    });
}

void SecondPage::Finish(Platform::Object^, Platform::Object^)
{
    timer->Stop();
    timer = nullptr;
    Log(L"navigate to controls page");
    Frame->Navigate(Interop::TypeName(ControlsPage::typeid), nullptr);
}
