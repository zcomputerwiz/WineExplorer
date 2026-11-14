// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "ControlsPage.xaml.h"

using namespace XamlProbe;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;

/* the common building blocks: Grid, Border, TextBox, CheckBox, Image, ListView (Items), ScrollViewer with a StackPanel
 * filled through Children; reports their layout, the list, the scroll extent and a ChangeView, then waits 20 s (room
 * for a suspend and resume run), reports whether MainPage was released and exits */
ControlsPage::ControlsPage() : ticks(0)
{
    InitializeComponent();
    for (int i = 1; i <= 5; i++)
        List->Items->Append(L"Item " + i.ToString());
    for (int i = 1; i <= 30; i++)
    {
        auto line = ref new TextBlock();
        line->Text = L"Line " + i.ToString();
        Long->Children->Append(line);
    }
    Log(L"controls page constructed, list " + List->Items->Size.ToString() + L", lines " + Long->Children->Size.ToString());
    Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^)
    {
        timer = ref new DispatcherTimer();
        TimeSpan span;
        span.Duration = 15000000;
        timer->Interval = span;
        timer->Tick += ref new EventHandler<Platform::Object^>(this, &ControlsPage::Step);
        timer->Start();
    });
}

static Platform::String^ Place(FrameworkElement^ element, UIElement^ root)
{
    auto at = element->TransformToVisual(root)->TransformPoint(Point(0, 0));
    return element->ActualWidth.ToString() + L"x" + element->ActualHeight.ToString() + L" at " +
           at.X.ToString() + L"," + at.Y.ToString();
}

void ControlsPage::Step(Platform::Object^, Platform::Object^)
{
    switch (ticks++)
    {
    case 0:
        Log(L"Layout Grid " + Place(Layout, this));
        Log(L"Box Border " + Place(Box, this));
        Log(L"BoxText TextBlock " + Place(BoxText, this));
        Log(L"Input TextBox " + Place(Input, this) + L", text " + Input->Text);
        Log(L"Check CheckBox " + Place(Check, this) + L", checked " + (Check->IsChecked->Value ? L"yes" : L"no"));
        Log(L"Picture Image " + Place(Picture, this));
        Log(L"List ListView " + Place(List, this) + L", items " + List->Items->Size.ToString());
        Log(L"Scroll ScrollViewer " + Place(Scroll, this) + L", extent " + Scroll->ExtentHeight.ToString() +
            L", viewport " + Scroll->ViewportHeight.ToString());
        Scroll->ChangeView(nullptr, 100.0, nullptr, true);
        /* struct properties, events */
        {
            auto pad = Box->Padding;
            auto corner = Box->CornerRadius;
            Log(L"Box padding " + pad.Left.ToString() + L"," + pad.Top.ToString() + L"," + pad.Right.ToString() + L"," +
                pad.Bottom.ToString() + L", corner " + corner.TopLeft.ToString());
            Thickness wider = { 10, 6, 10, 6 };
            Box->Padding = wider;
            Input->TextChanged += ref new TextChangedEventHandler([this](Platform::Object^ sender, TextChangedEventArgs^)
            {
                Log(L"TextChanged, sender is the text box " + (sender == Input ? L"yes" : L"no") + L", text " + Input->Text);
            });
            Check->Unchecked += ref new RoutedEventHandler([this](Platform::Object^ sender, RoutedEventArgs^ e)
            {
                Log(L"Unchecked, sender is the check box " + (sender == Check ? L"yes" : L"no") +
                    L", source is the check box " + (e->OriginalSource == Check ? L"yes" : L"no"));
            });
            Input->Text = L"Changed";
            Check->IsChecked = false;
        }
        break;
    case 1:
        Log(L"Scroll after ChangeView, offset " + Scroll->VerticalOffset.ToString());
        Log(L"Box after padding " + Place(Box, this) + L", text at " + Place(BoxText, this));
    {
        TimeSpan span;
        span.Duration = 200000000;   /* 20 s: room for a suspend and resume run */
        timer->Stop();
        timer->Interval = span;
        timer->Start();
    }
        break;
    default:
        timer->Stop();
        Log(L"main page destroyed by now: " + (MainPageDestroyed ? L"yes" : L"no"));
        Log(L"exit");
        Application::Current->Exit();
        break;
    }
}
