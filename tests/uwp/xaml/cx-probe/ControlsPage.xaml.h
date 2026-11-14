// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#pragma once
#include "ControlsPage.g.h"

namespace XamlProbe
{
    public ref class ControlsPage sealed
    {
    public:
        ControlsPage();
    private:
        void Step(Platform::Object^ sender, Platform::Object^ e);
        Windows::UI::Xaml::DispatcherTimer^ timer;
        int ticks;
    };
}
