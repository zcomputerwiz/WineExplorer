// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "SecondPage.xaml.h"

using namespace XamlProbe;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;

/* the page navigated to from MainPage: reports, then reports whether MainPage was released, and exits */
SecondPage::SecondPage()
{
    InitializeComponent();
    Log(L"second page constructed");
    Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^)
    {
        Log(L"second page loaded, frame back stack " + Frame->BackStackDepth.ToString() + L", note " + Note->ActualWidth.ToString());
        timer = ref new DispatcherTimer();
        TimeSpan span;
        span.Duration = 200000000;   /* 20 s: room for a suspend and resume run */
        timer->Interval = span;
        timer->Tick += ref new EventHandler<Platform::Object^>(this, &SecondPage::Finish);
        timer->Start();
    });
}

void SecondPage::Finish(Platform::Object^, Platform::Object^)
{
    timer->Stop();
    Log(L"main page destroyed by now: " + (MainPageDestroyed ? L"yes" : L"no"));
    Log(L"exit");
    Application::Current->Exit();
}
