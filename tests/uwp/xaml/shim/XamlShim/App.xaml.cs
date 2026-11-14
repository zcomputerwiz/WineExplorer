// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
namespace XamlShim;

/* the Uno application; Windows.UI.Xaml.Application.Start runs it and is told when it has launched */
public partial class UnoHostApp : Microsoft.UI.Xaml.Application
{
    public static Microsoft.UI.Xaml.Window? MainWindow;
    public static Action? Launched;

    public UnoHostApp()
    {
        this.InitializeComponent();
    }

    protected override void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
    {
        MainWindow = new Microsoft.UI.Xaml.Window();
        Launched?.Invoke();
    }
}
