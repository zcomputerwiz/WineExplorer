// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
namespace UnoSpike;

/* spike: report what Uno laid out and drew, then quit (for unattended runs on Windows and on Wine) */
public sealed partial class MainPage : Page
{
    public MainPage()
    {
        this.InitializeComponent();
        Loaded += async (s, e) =>
        {
            var path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "uno-spike.txt");
            void Log(string line) => System.IO.File.AppendAllText(path, line + System.Environment.NewLine);
            Log($"loaded page {ActualWidth}x{ActualHeight}");
            var button = new Button { Content = "Press me" };
            button.Click += (_, _) => Log("clicked");
            ((StackPanel)((Grid)((ScrollViewer)Content).Content).Children[0]).Children.Add(button);
            await System.Threading.Tasks.Task.Delay(1500);
            var text = (TextBlock)((StackPanel)((Grid)((ScrollViewer)Content).Content).Children[0]).Children[0];
            Log($"text {text.ActualWidth:0}x{text.ActualHeight:0} button {button.ActualWidth:0}x{button.ActualHeight:0}");
            var at = button.TransformToVisual(this).TransformPoint(new Windows.Foundation.Point(0, 0));
            Log($"button at {at.X:0},{at.Y:0}");
            await System.Threading.Tasks.Task.Delay(8000);
            Log("exit");
            Application.Current.Exit();
        };
    }
}
