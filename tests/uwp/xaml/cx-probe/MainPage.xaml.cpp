// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "MainPage.xaml.h"

using namespace XamlProbe;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;

/* report what XAML laid out (page, TextBlock, Button) after 1.5 s, then exit after 5 s */
MainPage::MainPage() : ticks(0)
{
    InitializeComponent();
    Log(L"page constructed");
    Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^)
    {
        Log(L"page loaded " + ActualWidth.ToString() + L"x" + ActualHeight.ToString());
        timer = ref new DispatcherTimer();
        TimeSpan span;
        span.Duration = 15000000;
        timer->Interval = span;
        timer->Tick += ref new EventHandler<Platform::Object^>(this, &MainPage::Report);
        timer->Start();
    });
}

static Platform::String^ Box(FrameworkElement^ element, UIElement^ root)
{
    auto at = element->TransformToVisual(root)->TransformPoint(Point(0, 0));
    return element->ActualWidth.ToString() + L"x" + element->ActualHeight.ToString() + L" at " +
           at.X.ToString() + L"," + at.Y.ToString();
}

void MainPage::Report(Platform::Object^, Platform::Object^)
{
    if (ticks++ == 0)
    {
        Log(L"Title TextBlock " + Box(Title, this));
        Log(L"Go Button " + Box(Go, this));
        return;
    }
    if (ticks < 4) return;
    timer->Stop();
    Log(L"exit");
    Application::Current->Exit();
}

void MainPage::OnGo(Platform::Object^, RoutedEventArgs^)
{
    Log(L"clicked");
}
