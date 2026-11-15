// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#pragma once

namespace XamlProbe
{
    /* a dependency object of the application with a property registered from native code */
    public ref class Gauge sealed : Windows::UI::Xaml::DependencyObject
    {
    public:
        Gauge() {}
        static property Windows::UI::Xaml::DependencyProperty^ LevelProperty { Windows::UI::Xaml::DependencyProperty^ get(); }
        property double Level
        {
            double get() { return safe_cast<double>(GetValue(LevelProperty)); }
            void set(double value) { SetValue(LevelProperty, value); }
        }
    internal:
        static int Changes;
        static Platform::String^ LastChange;
    };

    /* an attached property registered from native code, set on framework elements */
    public ref class Marks sealed
    {
    public:
        static property Windows::UI::Xaml::DependencyProperty^ NoteProperty { Windows::UI::Xaml::DependencyProperty^ get(); }
        static Platform::String^ GetNote(Windows::UI::Xaml::DependencyObject^ target);
        static void SetNote(Windows::UI::Xaml::DependencyObject^ target, Platform::String^ value);
    };

    /* the dependency property checks: reported through Log, one line each */
    void RunDependencyProbe(Windows::UI::Xaml::DependencyObject^ element);
}
