// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
//
// Application.LoadComponent for compiled XAML: the page's XBF is turned back into XAML text by XbfKit (MIT,
// github.com/0x5bfa/XbfKit), connection IDs are noted and removed, Uno's XamlReader builds the content, and the
// application's generated IComponentConnector.Connect receives each connected element (x:Name fields, event handlers).
// Experiment limits: the XBF is read from <package root>\<Name>.xbf (reading it from resources.pri is next); only the
// root's content is loaded (the root's own properties and resources are logged and skipped).
using System.Xml.Linq;
using Mux = Microsoft.UI.Xaml;
using UnoControls = Microsoft.UI.Xaml.Controls;

namespace XamlShim.Abi;

public static unsafe class XbfLoader
{
    static readonly XNamespace X = "http://schemas.microsoft.com/winfx/2006/xaml";

    public static int Load(IntPtr component, IntPtr uri)
    {
        var location = UriText(uri);
        var target = Ccw.Unwrap(component);
        Ccw.Log($"LoadComponent {location} into {target?.RuntimeClassName ?? "an unknown object"}");
        var name = location[(location.LastIndexOf('/') + 1)..];
        var root = Path.GetDirectoryName(Environment.ProcessPath) ?? ".";
        var path = Path.Combine(root, Path.ChangeExtension(name, ".xbf"));
        if (!File.Exists(path)) { Ccw.Log($"no {path}"); return unchecked((int)0x80070002); }

        string xaml;
        using (var stream = File.OpenRead(path))
            xaml = XbfKit.XbfDecompiler.Decompile(stream, XbfKit.XbfDialect.WUX, new XbfKit.XbfDecompilationOptions { IncludeConnectionIds = true });
        var doc = XDocument.Parse(xaml);
        var top = doc.Root!;
        foreach (var a in top.Attributes().Where(a => !a.IsNamespaceDeclaration && a.Name != X + "Class"))
            Ccw.Log($"root attribute {a.Name.LocalName} skipped");

        /* connection IDs: named elements keep their names, others get one */
        var connections = new List<(string Name, int Id)>();
        foreach (var e in top.Descendants())
        {
            var id = e.Attribute(X + "ConnectionId");
            if (id == null) continue;
            var n = e.Attribute(X + "Name")?.Value;
            if (n == null) { n = "__connection" + id.Value; e.SetAttributeValue(X + "Name", n); }
            connections.Add((n, int.Parse(id.Value)));
            id.Remove();
        }
        if (top.Attribute(X + "ConnectionId") is XAttribute rootId) { Ccw.Log($"root connection {rootId.Value} skipped"); rootId.Remove(); }

        var content = top.Elements().Where(e => !e.Name.LocalName.Contains('.')).ToList();
        if (target is not UI_Xaml_DependencyObject shim || content.Count == 0)
        {
            Ccw.Log($"nothing loaded ({content.Count} content elements)");
            return 0;
        }
        if (content.Count > 1) Ccw.Log($"{content.Count} content elements: only the first is loaded");
        var element = new XElement(content[0]);
        foreach (var ns in top.Attributes().Where(a => a.IsNamespaceDeclaration)) element.SetAttributeValue(ns.Name, ns.Value);
        var built = (Mux.UIElement)Mux.Markup.XamlReader.Load(element.ToString());

        switch (shim.UnoObject)
        {
        case UnoControls.UserControl user: user.Content = built; break;
        case UnoControls.ContentControl control: control.Content = built; break;
        default: Ccw.Log($"cannot set the content of {shim.UnoObject?.GetType().Name}"); return 0;
        }

        if (connections.Count == 0) return 0;
        if (Ccw.QueryInterface(component, Iids.UI_Xaml_Markup_IComponentConnector, out var connector) < 0)
        {
            Ccw.Log("the component has no IComponentConnector");
            return 0;
        }
        foreach (var (n, id) in connections)
        {
            var found = (built as Mux.FrameworkElement)?.FindName(n);
            if (found == null) { Ccw.Log($"connection {id}: {n} not found"); continue; }
            var wrapper = Shim.For(found);
            var p = Ccw.Get(wrapper);
            int hr = UI_Xaml_Markup_IComponentConnector_Call.Connect(connector, id, p);
            Ccw.Log($"Connect {id} {n} ({wrapper.RuntimeClassName}): {hr:x8}");
            Ccw.ComRelease(p);
        }
        Ccw.ComRelease(connector);
        return 0;
    }

    static string UriText(IntPtr uri)
    {
        if (Ccw.QueryInterface(uri, Iids.Foundation_IUriRuntimeClass, out var p) < 0) return "";
        IntPtr h;
        Foundation_IUriRuntimeClass_Call.get_AbsoluteUri(p, &h);
        Ccw.ComRelease(p);
        var s = HString.Read(h);
        HString.Delete(h);
        return s;
    }
}
