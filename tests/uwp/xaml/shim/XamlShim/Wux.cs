// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
//
// The Windows.UI.Xaml members the XAML probe (tests/uwp/xaml/cx-probe) uses, implemented over Uno. Every other member
// of the generated interfaces logs "not implemented" and returns E_NOTIMPL.
using global::Uno.UI.Hosting;
using Mux = Microsoft.UI.Xaml;
using UnoControls = Microsoft.UI.Xaml.Controls;

namespace XamlShim.Abi;

static class Shim
{
    static long tokens;
    public static long Token() => Interlocked.Increment(ref tokens);
    public static readonly List<IntPtr> Kept = new();      /* native objects the framework holds (never released here) */

    /* the shim for a Uno object, made once; the class follows the Uno type or its nearest known base */
    public static ShimObject For(object uno) => Ccw.Wrap(uno, () =>
    {
        for (var t = uno.GetType(); t != null; t = t.BaseType)
        {
            if (Classes.TryGetValue(t.FullName ?? "", out var make))
            {
                var s = make();
                ((UI_Xaml_DependencyObject)s).UnoObject = uno;
                return s;
            }
        }
        throw new NotSupportedException($"no shim class for {uno.GetType().FullName}");
    });

    static readonly Dictionary<string, Func<ShimObject>> Classes = new()
    {
        ["Microsoft.UI.Xaml.Controls.StackPanel"] = () => new UI_Xaml_Controls_StackPanel(),
        ["Microsoft.UI.Xaml.Controls.TextBlock"] = () => new UI_Xaml_Controls_TextBlock(),
        ["Microsoft.UI.Xaml.Controls.Button"] = () => new UI_Xaml_Controls_Button(),
        ["Microsoft.UI.Xaml.Controls.Page"] = () => new UI_Xaml_Controls_Page(),
        ["Microsoft.UI.Xaml.Controls.Frame"] = () => new UI_Xaml_Controls_Frame(),
        ["Microsoft.UI.Xaml.Controls.Panel"] = () => new UI_Xaml_Controls_Panel(),
        ["Microsoft.UI.Xaml.Controls.ContentControl"] = () => new UI_Xaml_Controls_ContentControl(),
        ["Microsoft.UI.Xaml.Controls.Control"] = () => new UI_Xaml_Controls_Control(),
        ["Microsoft.UI.Xaml.FrameworkElement"] = () => new UI_Xaml_FrameworkElement(),
        ["Microsoft.UI.Xaml.UIElement"] = () => new UI_Xaml_UIElement(),
    };

    public static object? UnoOf(IntPtr p) => (Ccw.Unwrap(p) as UI_Xaml_DependencyObject)?.UnoObject;

    /* invoke a delegate (IUnknown-based: Invoke is slot 3) with two IInspectable arguments */
    public static unsafe int Invoke(IntPtr handler, IntPtr sender, IntPtr args) =>
        ((delegate* unmanaged[Stdcall]<IntPtr, IntPtr, IntPtr, int>)(*(IntPtr**)handler)[3])(handler, sender, args);

    public static void Raise(IntPtr handler, ShimObject sender, ShimObject? args)
    {
        try
        {
            Ccw.Trace($"raise: sender {sender.RuntimeClassName} {Ccw.Describe(sender)}");
            var s = Ccw.Get(sender);
            var a = Ccw.GetDefault(args);
            Ccw.Trace($"raise to {handler:x}: sender {s:x} args {a:x}");
            int hr = Invoke(handler, s, a);
            Ccw.Trace($"handler returned {hr:x8}");
            if (hr < 0) Ccw.Log($"handler returned {hr:x8}");
            Ccw.ComRelease(s);
            if (a != IntPtr.Zero) Ccw.ComRelease(a);
        }
        catch (Exception e)
        {
            Ccw.Log($"raise failed: {e}");
        }
    }

    /* composable activation shared by the factories: new shim around uno, inner of outer when there is one */
    public static unsafe int Compose(ShimObject shim, IntPtr outer, IntPtr* inner, IntPtr* result)
    {
        var (i, instance) = Ccw.Compose(shim, outer);
        if (inner != null) *inner = i;
        Ccw.Trace($"compose {shim.RuntimeClassName}: {Ccw.Describe(shim)}");
        /* the framework keeps the application's objects alive (Application.Current, elements in the tree): the
         * experiment holds one reference on every outer object for good; lifetime tracking comes later */
        if (outer != IntPtr.Zero) { Ccw.ComAddRef(outer); lock (Kept) Kept.Add(outer); }
        *result = outer == IntPtr.Zero ? Ccw.GetDefault(shim) : instance;
        if (outer == IntPtr.Zero && instance != IntPtr.Zero) Ccw.ComRelease(instance);
        return 0;
    }
}

public partial class UI_Xaml_DependencyObject
{
    public object? UnoObject;
}

// ---- Application ----

public unsafe partial class UI_Xaml_Application
{
    public static UI_Xaml_Application? Current;

    int IUI_Xaml_IApplication_Abi.Exit()
    {
        Ccw.Log("Application.Exit");
        Mux.Application.Current.Exit();
        return 0;
    }
}

public unsafe partial class UI_Xaml_Application_Factory
{
    int IUI_Xaml_IApplicationFactory_Abi.CreateInstance(IntPtr outer, IntPtr* inner, IntPtr* result)
    {
        var app = new UI_Xaml_Application();
        UI_Xaml_Application.Current = app;
        Ccw.Log($"Application created, outer {(outer != IntPtr.Zero ? "yes" : "no")}");
        return Shim.Compose(app, outer, inner, result);
    }

    int IUI_Xaml_IApplicationStatics_Abi.get_Current(IntPtr* result)
    {
        *result = Ccw.GetDefault(UI_Xaml_Application.Current);
        return 0;
    }

    int IUI_Xaml_IApplicationStatics_Abi.Start(IntPtr callback)
    {
        Ccw.Log("Application.Start");
        Ccw.ComAddRef(callback);
        int status = 0;
        var thread = new Thread(() =>
        {
            try
            {
                XamlShim.UnoHostApp.Launched = () => Launch(callback);
                var host = global::Uno.UI.Hosting.UnoPlatformHostBuilder.Create()
                    .App(() => new XamlShim.UnoHostApp())
                    .UseWin32()
                    .Build();
                host.Run();
                Ccw.Log("Uno host returned");
            }
            catch (Exception e)
            {
                Ccw.Log($"Uno host failed: {e}");
                status = unchecked((int)0x80004005);
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        return status;
    }

    /* on the UI thread, once Uno has launched: the application's callback creates App, then OnLaunched */
    static void Launch(IntPtr callback)
    {
        int hr = UI_Xaml_ApplicationInitializationCallback_Call.Invoke(callback, IntPtr.Zero);
        Ccw.Log($"initialization callback {hr:x8}");
        var app = UI_Xaml_Application.Current;
        if (app == null) { Ccw.Log("no Application was created"); return; }
        var args = new ApplicationModel_Activation_LaunchActivatedEventArgs();
        var overrides = Ccw.Get(app, Iids.UI_Xaml_IApplicationOverrides);
        var a = Ccw.GetDefault(args);
        hr = UI_Xaml_IApplicationOverrides_Call.OnLaunched(overrides, a);
        Ccw.Log($"OnLaunched {hr:x8}");
        Ccw.ComRelease(a);
        Ccw.ComRelease(overrides);
    }

    int IUI_Xaml_IApplicationStatics_Abi.LoadComponent(IntPtr component, IntPtr uri) =>
        XbfLoader.Load(component, uri);

    int IUI_Xaml_IApplicationStatics_Abi.LoadComponentWithResourceLocation(IntPtr component, IntPtr uri, int location) =>
        XbfLoader.Load(component, uri);
}

public unsafe partial class ApplicationModel_Activation_LaunchActivatedEventArgs
{
    int IApplicationModel_Activation_ILaunchActivatedEventArgs_Abi.get_Arguments(IntPtr* result)
    {
        *result = IntPtr.Zero;   /* the empty HSTRING */
        return 0;
    }
}

// ---- Window ----

public unsafe partial class UI_Xaml_Window
{
    public Mux.Window? UnoWindow;

    int IUI_Xaml_IWindow_Abi.put_Content(IntPtr value)
    {
        UnoWindow!.Content = Shim.UnoOf(value) as Mux.UIElement;
        Ccw.Log($"Window.Content = {UnoWindow.Content?.GetType().Name ?? "null"}");
        return 0;
    }

    int IUI_Xaml_IWindow_Abi.Activate()
    {
        UnoWindow!.Activate();
        Ccw.Log("Window.Activate");
        var timer = new Mux.DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (s, e) =>
        {
            timer.Stop();
            void Walk(Mux.DependencyObject o, int depth)
            {
                if (o is Mux.FrameworkElement fe)
                    Ccw.Log($"tree {new string(' ', depth * 2)}{fe.GetType().Name} {fe.ActualWidth:0}x{fe.ActualHeight:0} loaded {fe.IsLoaded}");
                if (depth > 12) return;
                int n = Mux.Media.VisualTreeHelper.GetChildrenCount(o);
                for (int i = 0; i < n; i++) Walk(Mux.Media.VisualTreeHelper.GetChild(o, i), depth + 1);
            }
            Ccw.Log($"window bounds {UnoWindow.Bounds}");
            if (UnoWindow.Content != null) Walk(UnoWindow.Content, 0);
        };
        timer.Start();
        return 0;
    }
}

public unsafe partial class UI_Xaml_Window_Factory
{
    UI_Xaml_Window? current;

    int IUI_Xaml_IWindowStatics_Abi.get_Current(IntPtr* result)
    {
        current ??= new UI_Xaml_Window { UnoWindow = XamlShim.UnoHostApp.MainWindow };
        *result = Ccw.GetDefault(current);
        return 0;
    }
}

// ---- elements ----

public unsafe partial class UI_Xaml_UIElement
{
    public Mux.UIElement UnoElement => (Mux.UIElement)UnoObject!;

    int IUI_Xaml_IUIElement_Abi.TransformToVisual(IntPtr visual, IntPtr* result)
    {
        var target = Shim.UnoOf(visual) as Mux.UIElement;
        var t = UnoElement.TransformToVisual(target);
        *result = Ccw.GetDefault(new UI_Xaml_Media_GeneralTransform { UnoObject = t });
        return 0;
    }
}

public unsafe partial class UI_Xaml_FrameworkElement
{
    Mux.FrameworkElement Fe => (Mux.FrameworkElement)UnoObject!;

    int IUI_Xaml_IFrameworkElement_Abi.get_ActualWidth(double* result) { *result = Fe.ActualWidth; return 0; }
    int IUI_Xaml_IFrameworkElement_Abi.get_ActualHeight(double* result) { *result = Fe.ActualHeight; return 0; }

    int IUI_Xaml_IFrameworkElement_Abi.add_Loaded(IntPtr handler, long* token)
    {
        Ccw.ComAddRef(handler);
        Ccw.Log($"add_Loaded on {RuntimeClassName}");
        Fe.Loaded += (s, e) => { Ccw.Log($"Loaded {RuntimeClassName}"); Shim.Raise(handler, this, new UI_Xaml_RoutedEventArgs()); };
        *token = Shim.Token();
        return 0;
    }
}

public unsafe partial class UI_Xaml_Media_GeneralTransform
{
    int IUI_Xaml_Media_IGeneralTransform_Abi.TransformPoint(Foundation_Point point, Foundation_Point* result)
    {
        var p = ((Mux.Media.GeneralTransform)UnoObject!).TransformPoint(new Windows.Foundation.Point(point.X, point.Y));
        result->X = (float)p.X;
        result->Y = (float)p.Y;
        return 0;
    }
}

public unsafe partial class UI_Xaml_Controls_ContentControl
{
    int IUI_Xaml_Controls_IContentControl_Abi.put_Content(IntPtr value)
    {
        ((UnoControls.ContentControl)UnoObject!).Content = Shim.UnoOf(value);
        return 0;
    }
}

public unsafe partial class UI_Xaml_Controls_UserControl
{
    int IUI_Xaml_Controls_IUserControl_Abi.put_Content(IntPtr value)
    {
        ((UnoControls.UserControl)UnoObject!).Content = Shim.UnoOf(value) as Mux.UIElement;
        return 0;
    }
}

public unsafe partial class UI_Xaml_Controls_Primitives_ButtonBase
{
    int IUI_Xaml_Controls_Primitives_IButtonBase_Abi.add_Click(IntPtr handler, long* token)
    {
        Ccw.ComAddRef(handler);
        ((UnoControls.Primitives.ButtonBase)UnoObject!).Click += (s, e) => Shim.Raise(handler, this, new UI_Xaml_RoutedEventArgs());
        *token = Shim.Token();
        return 0;
    }
}

// ---- Frame and Page ----

public unsafe partial class UI_Xaml_Controls_Frame
{
    /* Navigate(TypeName, parameter): the page comes from the application's own metadata provider */
    int IUI_Xaml_Controls_IFrame_Abi.Navigate(TypeName type, IntPtr parameter, byte* result)
    {
        *result = 0;
        Ccw.Trace($"Navigate: name {type.Name:x} kind {type.Kind}");
        var name = HString.Read(type.Name);
        Ccw.Log($"Navigate {name}");
        var app = UI_Xaml_Application.Current ?? throw new InvalidOperationException("no Application");
        var provider = Ccw.Get(app, Iids.UI_Xaml_Markup_IXamlMetadataProvider);
        Ccw.Trace($"Navigate: provider {provider:x}");
        if (provider == IntPtr.Zero) { Ccw.Log("the application has no IXamlMetadataProvider"); return unchecked((int)0x80004002); }
        IntPtr xamlType, instance;
        int hr = UI_Xaml_Markup_IXamlMetadataProvider_Call.GetXamlType(provider, type, &xamlType);
        Ccw.ComRelease(provider);
        Ccw.Trace($"Navigate: GetXamlType {hr:x8} {xamlType:x}");
        if (hr < 0 || xamlType == IntPtr.Zero) { Ccw.Log($"GetXamlType {name}: {hr:x8}"); return hr < 0 ? hr : unchecked((int)0x80004005); }
        hr = UI_Xaml_Markup_IXamlType_Call.ActivateInstance(xamlType, &instance);
        Ccw.ComRelease(xamlType);
        if (hr < 0) { Ccw.Log($"ActivateInstance {name}: {hr:x8}"); return hr; }
        Shim.Kept.Add(instance);
        var page = Shim.UnoOf(instance);
        Ccw.Log($"Navigate {name}: {page?.GetType().Name ?? "not a shim object"}");
        ((UnoControls.ContentControl)UnoObject!).Content = page;
        *result = 1;
        return 0;
    }
}

public unsafe partial class UI_Xaml_Controls_Frame_Factory
{
    int IUI_Xaml_Controls_IFrameFactory_Abi.CreateInstance(IntPtr outer, IntPtr* inner, IntPtr* result) =>
        Shim.Compose(new UI_Xaml_Controls_Frame { UnoObject = new UnoControls.Frame() }, outer, inner, result);
}

public unsafe partial class UI_Xaml_Controls_Page_Factory
{
    int IUI_Xaml_Controls_IPageFactory_Abi.CreateInstance(IntPtr outer, IntPtr* inner, IntPtr* result)
    {
        var page = new UI_Xaml_Controls_Page { UnoObject = new UnoControls.Page() };
        return Shim.Compose(page, outer, inner, result);
    }
}

// ---- DispatcherTimer ----

public unsafe partial class UI_Xaml_DispatcherTimer
{
    readonly Mux.DispatcherTimer timer = new();

    int IUI_Xaml_IDispatcherTimer_Abi.put_Interval(long ticks) { timer.Interval = TimeSpan.FromTicks(ticks); return 0; }
    int IUI_Xaml_IDispatcherTimer_Abi.Start() { timer.Start(); return 0; }
    int IUI_Xaml_IDispatcherTimer_Abi.Stop() { timer.Stop(); return 0; }

    int IUI_Xaml_IDispatcherTimer_Abi.add_Tick(IntPtr handler, long* token)
    {
        Ccw.ComAddRef(handler);
        timer.Tick += (s, e) => Shim.Raise(handler, this, null);
        *token = Shim.Token();
        return 0;
    }
}

public unsafe partial class UI_Xaml_DispatcherTimer_Factory
{
    int IUI_Xaml_IDispatcherTimerFactory_Abi.CreateInstance(IntPtr outer, IntPtr* inner, IntPtr* result) =>
        Shim.Compose(new UI_Xaml_DispatcherTimer(), outer, inner, result);
}
