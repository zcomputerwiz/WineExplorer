// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
using System.Runtime.InteropServices;
using Uno.UI.Hosting;

namespace UnoSpike;

/* XAML experiment step 2a: entry point for a native process that hosts CoreCLR itself (hostfxr) and asks Uno to
 * show a piece of XAML text, the way a Windows.UI.Xaml shim would. */
public static class Shim
{
    internal static string? Xaml;

    [UnmanagedCallersOnly]
    public static int Start(IntPtr xaml)
    {
        try
        {
            Xaml = Marshal.PtrToStringUni(xaml);
            Program.Log($"shim start pid {Environment.ProcessId} {RuntimeInformation.ProcessArchitecture} xaml {Xaml?.Length}");
            App.InitializeLogging();
            var host = UnoPlatformHostBuilder.Create()
                .App(() => new App())
                .UseWin32()
                .Build();
            host.Run();
            Program.Log("shim returned");
            return 0;
        }
        catch (Exception e)
        {
            Program.Log($"shim exception {e}");
            return 1;
        }
    }

    /* called from App.OnLaunched instead of navigating to MainPage */
    internal static void Show(Window window)
    {
        var root = (FrameworkElement)Microsoft.UI.Xaml.Markup.XamlReader.Load(Xaml!);
        window.Content = root;
        root.Loaded += async (_, _) =>
        {
            Program.Log($"shim loaded {root.GetType().Name} {root.ActualWidth:0}x{root.ActualHeight:0}");
            await System.Threading.Tasks.Task.Delay(1500);
            foreach (var name in new[] { "Title", "Go" })
            {
                if (root.FindName(name) is FrameworkElement e)
                {
                    var at = e.TransformToVisual(root).TransformPoint(new Windows.Foundation.Point(0, 0));
                    Program.Log($"shim {name} {e.GetType().Name} {e.ActualWidth:0}x{e.ActualHeight:0} at {at.X:0},{at.Y:0}");
                }
                else
                    Program.Log($"shim {name} missing");
            }
            await System.Threading.Tasks.Task.Delay(3000);
            Program.Log("shim exit");
            Application.Current.Exit();
        };
    }
}
