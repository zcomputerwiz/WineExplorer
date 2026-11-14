// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#pragma once
#include "SecondPage.g.h"

namespace XamlProbe
{
    public ref class SecondPage sealed
    {
    public:
        SecondPage();
    private:
        void Finish(Platform::Object^ sender, Platform::Object^ e);
        Windows::UI::Xaml::DispatcherTimer^ timer;
    };
}
