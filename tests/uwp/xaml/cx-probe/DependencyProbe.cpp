// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
#include "pch.h"
#include "DependencyProbe.h"

using namespace XamlProbe;
using namespace Platform;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Interop;

int Gauge::Changes = 0;
String^ Gauge::LastChange = L"";

/* registered on first use, like C++/CX code commonly does (and in static initialisers, as the Calculator does) */
DependencyProperty^ Gauge::LevelProperty::get()
{
    static DependencyProperty^ property = DependencyProperty::Register(L"Level", TypeName(double::typeid),
        TypeName(Gauge::typeid), ref new PropertyMetadata(1.5, ref new PropertyChangedCallback(
            [](DependencyObject^ d, DependencyPropertyChangedEventArgs^ e)
            {
                Gauge::Changes++;
                Gauge::LastChange = (e->OldValue ? e->OldValue->ToString() : L"null") + L" -> " +
                                    (e->NewValue ? e->NewValue->ToString() : L"null") +
                                    (e->Property == Gauge::LevelProperty ? L", property is Level" : L", another property") +
                                    (dynamic_cast<Gauge^>(d) ? L", sender is a Gauge" : L", sender is not a Gauge");
            })));
    return property;
}

DependencyProperty^ Marks::NoteProperty::get()
{
    static DependencyProperty^ property = DependencyProperty::RegisterAttached(L"Note", TypeName(String::typeid),
        TypeName(Marks::typeid), ref new PropertyMetadata(L"none"));
    return property;
}

String^ Marks::GetNote(DependencyObject^ target) { return safe_cast<String^>(target->GetValue(NoteProperty)); }
void Marks::SetNote(DependencyObject^ target, String^ value) { target->SetValue(NoteProperty, value); }

static String^ Show(Object^ o) { return o ? o->ToString() : L"null"; }

void XamlProbe::RunDependencyProbe(DependencyObject^ element)
{
    auto g = ref new Gauge();
    Log(L"dp default " + g->Level.ToString() + L", local " +
        (g->ReadLocalValue(Gauge::LevelProperty) == DependencyProperty::UnsetValue ? L"unset" : L"set") +
        L", metadata default " + Show(Gauge::LevelProperty->GetMetadata(TypeName(Gauge::typeid))->DefaultValue));
    g->Level = 3.25;
    Log(L"dp set " + g->Level.ToString() + L", changes " + Gauge::Changes.ToString() + L": " + Gauge::LastChange);
    g->Level = 3.25;
    Log(L"dp same value again, changes " + Gauge::Changes.ToString());
    long long token = g->RegisterPropertyChangedCallback(Gauge::LevelProperty, ref new DependencyPropertyChangedCallback(
        [](DependencyObject^ sender, DependencyProperty^ dp)
        {
            Log(L"dp registered callback, property is Level " + (dp == Gauge::LevelProperty ? L"yes" : L"no") +
                L", value " + safe_cast<Gauge^>(sender)->Level.ToString());
        }));
    g->ClearValue(Gauge::LevelProperty);
    Log(L"dp cleared " + g->Level.ToString() + L", changes " + Gauge::Changes.ToString() + L": " + Gauge::LastChange);
    g->UnregisterPropertyChangedCallback(Gauge::LevelProperty, token);
    g->Level = 7;
    Log(L"dp after unregister " + g->Level.ToString() + L", changes " + Gauge::Changes.ToString());

    Log(L"attached default " + Marks::GetNote(element));
    Marks::SetNote(element, L"checked");
    Log(L"attached set " + Marks::GetNote(element) + L", other element " + Marks::GetNote(ref new Gauge()));
}
