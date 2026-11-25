// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// lab test app: a minimal WinUI 3 window with a menu bar and text (App.xaml only for the control resources)
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace CmTest
{
    public partial class App : Application
    {
        private Window window;

        public App()
        {
            InitializeComponent();
            HookLog.Start();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs args)
        {
            var menu = new MenuBar();
            foreach (var name in new[] { "File", "Edit", "View" })
                menu.Items.Add(new MenuBarItem { Title = name });
            var panel = new StackPanel();
            panel.Children.Add(menu);
            panel.Children.Add(new TextBlock { Text = "CoreMessaging test window", Margin = new Thickness(12) });
            window = new Window { Title = "CmTest", Content = panel };
            window.Activate();
        }
    }
}
