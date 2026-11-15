// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#pragma once
#include "App.g.h"

namespace XamlProbe
{
    void Log(Platform::String^ line);
    Platform::String^ ResourcesSummary();
    void RunLaunchProbe(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e);
    void RunTitleBarProbe();
    void RunUserActivityProbe();
    extern bool MainPageDestroyed;      /* set by MainPage's destructor */

    ref class App sealed
    {
    public:
        App();
        virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ e) override;
    private:
        void OnSuspending(Platform::Object^ sender, Windows::ApplicationModel::SuspendingEventArgs^ e);
        void OnResuming(Platform::Object^ sender, Platform::Object^ e);
    };
}
