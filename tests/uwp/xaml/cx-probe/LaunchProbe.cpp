// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
//
// What a launch gives an application, as the Windows Calculator's OnLaunched uses it: the interfaces of the launch
// arguments and their values, the user (only the shape of its id is logged, never the id), ApplicationView before the
// window is activated, SizeHelper. One line each; a missing piece is logged with its HRESULT instead of ending the run.
#include "pch.h"

using namespace XamlProbe;
using namespace Platform;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Foundation;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;

static String^ Hr(Exception^ e) { return L"failed " + e->HResult.ToString(); }

#define PROBE(label, expr) \
    try { Log(String::Concat(L"launch " label L" ", (expr))); } catch (Exception^ e) { Log(L"launch " label L" " + Hr(e)); }

static String^ YesNo(bool b) { return b ? L"yes" : L"no"; }

/* the shape of an identifier: its length and which kinds of characters it has */
static String^ Shape(String^ s)
{
    if (!s) return L"null";
    bool digits = false, upper = false, lower = false, other = false;
    for (auto c : s) { if (iswdigit(c)) digits = true; else if (iswupper(c)) upper = true; else if (iswlower(c)) lower = true; else other = true; }
    return L"length " + s->Length().ToString() + (digits ? L" digits" : L"") + (upper ? L" upper" : L"") +
           (lower ? L" lower" : L"") + (other ? L" other" : L"");
}

void XamlProbe::RunLaunchProbe(LaunchActivatedEventArgs^ e)
{
    Object^ o = e;
    Log(L"launch interfaces: activated " + YesNo(dynamic_cast<IActivatedEventArgs^>(o) != nullptr) +
        L", launch " + YesNo(dynamic_cast<ILaunchActivatedEventArgs^>(o) != nullptr) +
        L", launch2 " + YesNo(dynamic_cast<ILaunchActivatedEventArgs2^>(o) != nullptr) +
        L", with user " + YesNo(dynamic_cast<IActivatedEventArgsWithUser^>(o) != nullptr) +
        L", prelaunch " + YesNo(dynamic_cast<IPrelaunchActivatedEventArgs^>(o) != nullptr) +
        L", view activated " + YesNo(dynamic_cast<IApplicationViewActivatedEventArgs^>(o) != nullptr) +
        L", view switcher " + YesNo(dynamic_cast<IViewSwitcherProvider^>(o) != nullptr));
    /* the previous state and the arguments depend on how the application was started: not logged */
    PROBE(L"kind", ((int)e->Kind).ToString());
    PROBE(L"tile id", L"'" + e->TileId + L"'");
    PROBE(L"prelaunch", YesNo(e->PrelaunchActivated));
    PROBE(L"tile info", e->TileActivatedInfo ? L"set" : L"null");
    PROBE(L"user", e->User ? L"set" : L"null");
    PROBE(L"user type", ((int)e->User->Type).ToString() + L", authentication " + ((int)e->User->AuthenticationStatus).ToString());
    PROBE(L"user id", Shape(e->User->NonRoamableId));
    PROBE(L"user id stable", YesNo(e->User->NonRoamableId == e->User->NonRoamableId));
    PROBE(L"view id", e->CurrentlyShownApplicationViewId.ToString() + L", current view's " +
          (e->CurrentlyShownApplicationViewId == ApplicationView::GetForCurrentView()->Id ? L"same" : L"different"));
    PROBE(L"view switcher", e->ViewSwitcher ? L"set" : L"null");
    PROBE(L"view id for window", (ApplicationView::GetApplicationViewIdForWindow(Windows::UI::Core::CoreWindow::GetForCurrentThread()) ==
                                  ApplicationView::GetForCurrentView()->Id) ? L"current view's" : L"different");
    /* (a null window ends the process on Windows: not probed) */
    PROBE(L"splash", e->SplashScreen ? L"set" : L"null");

    PROBE(L"application view", ApplicationView::GetForCurrentView() ? L"set" : L"null");
    PROBE(L"application view same", YesNo(ApplicationView::GetForCurrentView() == ApplicationView::GetForCurrentView()));
    PROBE(L"view title", L"'" + ApplicationView::GetForCurrentView()->Title + L"', full screen " +
          YesNo(ApplicationView::GetForCurrentView()->IsFullScreenMode));
    PROBE(L"launch mode", ((int)ApplicationView::PreferredLaunchWindowingMode).ToString());
    PROBE(L"launch size", ApplicationView::PreferredLaunchViewSize.Width.ToString() + L"x" +
          ApplicationView::PreferredLaunchViewSize.Height.ToString());
    Size size(320, 500);
    try { size = SizeHelper::FromDimensions(320, 500); Log(L"launch size helper " + size.Width.ToString() + L"x" + size.Height.ToString()); }
    catch (Exception^ x) { Log(L"launch size helper " + Hr(x)); }
    try { ApplicationView::GetForCurrentView()->SetPreferredMinSize(size); Log(L"launch min size ok"); }
    catch (Exception^ x) { Log(L"launch min size " + Hr(x)); }
    /* the size the probe's pages are laid out at: whether a resize before the window is shown sticks differs (Windows
     * keeps the size it remembers for the application), the layout lines below must not depend on it */
    PROBE(L"try resize", YesNo(ApplicationView::GetForCurrentView()->TryResizeView(Size(1200, 900))));
    try
    {
        ApplicationView::PreferredLaunchWindowingMode = ApplicationViewWindowingMode::Auto;
        Log(L"launch mode set, now " + ((int)ApplicationView::PreferredLaunchWindowingMode).ToString());
    }
    catch (Exception^ x) { Log(L"launch mode set " + Hr(x)); }
    RunTitleBarProbe();
}

/* the title bar: CoreApplicationView.TitleBar (the Calculator extends its view into it and lays its own title bar out
 * from Height and the insets) and ApplicationView.TitleBar (whose button colours it sets) */
static String^ Metrics(Windows::ApplicationModel::Core::CoreApplicationViewTitleBar^ t)
{
    return L"extend " + YesNo(t->ExtendViewIntoTitleBar) + L", visible " + YesNo(t->IsVisible) + L", height " +
           t->Height.ToString() + L", insets " + t->SystemOverlayLeftInset.ToString() + L" " +
           t->SystemOverlayRightInset.ToString();
}

void XamlProbe::RunTitleBarProbe()
{
    using namespace Windows::ApplicationModel::Core;
    CoreApplicationViewTitleBar^ t = nullptr;
    try { t = CoreApplication::GetCurrentView()->TitleBar; Log(L"title bar " + (t ? L"set" : L"null")); }
    catch (Exception^ x) { Log(L"title bar " + Hr(x)); }
    if (t)
    {
        PROBE(L"title bar same", YesNo(t == CoreApplication::GetCurrentView()->TitleBar));
        PROBE(L"title bar at launch", Metrics(t));
        try
        {
            static bool logged;
            t->LayoutMetricsChanged += ref new TypedEventHandler<CoreApplicationViewTitleBar^, Object^>(
                [](CoreApplicationViewTitleBar^ sender, Object^ args)
                {
                    if (logged) return;
                    logged = true;
                    Log(L"title bar metrics changed: " + Metrics(sender) + L", args " + (args ? L"set" : L"null"));
                });
            t->ExtendViewIntoTitleBar = true;
            Log(L"launch title bar extended: " + Metrics(t));
            /* back to the standard title bar: an extended view's content also covers the title bar (32 more pixels of
             * height on Windows), which the page lines below must not depend on */
            t->ExtendViewIntoTitleBar = false;
            Log(L"launch title bar restored: " + Metrics(t));
        }
        catch (Exception^ x) { Log(L"launch title bar extend " + Hr(x)); }
    }
    try
    {
        auto bar = ApplicationView::GetForCurrentView()->TitleBar;
        Log(L"launch view title bar " + (bar ? L"set" : L"null"));
        PROBE(L"view title bar button background", bar->ButtonBackgroundColor ? L"set" : L"null");
        bar->ButtonBackgroundColor = Windows::UI::Colors::Transparent;
        bar->ButtonForegroundColor = Windows::UI::Colors::Black;
        PROBE(L"view title bar button background now", bar->ButtonBackgroundColor ? bar->ButtonBackgroundColor->Value.A.ToString() : L"null");
    }
    catch (Exception^ x) { Log(L"launch view title bar " + Hr(x)); }
}
