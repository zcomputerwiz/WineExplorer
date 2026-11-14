// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using XamlShim.Abi;

namespace XamlShim;

public static unsafe class Activation
{
    static readonly ConcurrentDictionary<string, ShimFactory> factories = new();

    /* called by xamlshim.dll (DllGetActivationFactory): an AddRef'd IActivationFactory for the class */
    [UnmanagedCallersOnly]
    public static int GetActivationFactory(char* name, IntPtr* factory)
    {
        try
        {
            var n = new string(name);
            *factory = IntPtr.Zero;
            if (!FactoryTable.Create.TryGetValue(n, out var make))
            {
                Ccw.Log($"no factory for {n}");
                return unchecked((int)0x80040111);   /* CLASS_E_CLASSNOTAVAILABLE */
            }
            var f = factories.GetOrAdd(n, _ => make());
            *factory = Ccw.Get(f, Ccw.IID_IActivationFactory);
            Ccw.Trace($"factory {n}");
            return 0;
        }
        catch (Exception e)
        {
            return Ccw.Fail("GetActivationFactory", e);
        }
    }
}
