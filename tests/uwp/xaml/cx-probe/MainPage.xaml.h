// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#pragma once
#include "MainPage.g.h"

namespace XamlProbe
{
    public ref class MainPage sealed
    {
    public:
        MainPage();
        virtual ~MainPage();
    private:
        void OnGo(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void Report(Platform::Object^ sender, Platform::Object^ e);
        Windows::UI::Xaml::DispatcherTimer^ timer;
        int ticks;
    };
}
