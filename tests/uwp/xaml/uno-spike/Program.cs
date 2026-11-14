// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
using Uno.UI.Hosting;

namespace UnoSpike;

internal class Program
{
    internal static void Log(string line) =>
        System.IO.File.AppendAllText(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "uno-spike.txt"), line + System.Environment.NewLine);

    [STAThread]
    public static void Main(string[] args)
    {
        Log($"main {System.Runtime.InteropServices.RuntimeInformation.OSDescription} {System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture}");
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log($"unhandled {e.ExceptionObject}");
        try
        {
            App.InitializeLogging();

            /* Win32 first: under Wine, DISPLAY is set and the X11 host could otherwise be chosen */
            var host = UnoPlatformHostBuilder.Create()
                .App(() => new App())
                .UseWin32()
                .UseX11()
                .UseLinuxFrameBuffer()
                .UseMacOS()
                .Build();

            Log($"host {host.GetType().FullName}");
            host.Run();
            Log("host returned");
        }
        catch (Exception e)
        {
            Log($"exception {e}");
            throw;
        }
    }
}
