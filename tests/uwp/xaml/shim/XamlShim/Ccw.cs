// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
//
// Native-callable wrappers (CCWs) for the Windows.UI.Xaml ABI. Every managed ShimObject gets one CcwState: a strong
// GCHandle (the experiment never frees objects), a reference count, and one 16-byte native block per interface:
// [vtable][GCHandle]. The vtables come from the generated code (Abi.g.cs); slots 0 to 5 (IUnknown, IInspectable) are
// filled here. Aggregation: a composable class created with an outer object (the application's App or MainPage)
// forwards QueryInterface, AddRef and Release of every interface block to the outer; the outer holds the separate
// non-delegating inner block, which answers queries itself.
using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace XamlShim.Abi;

public abstract class ShimObject
{
    public virtual string RuntimeClassName => GetType().Name;
    internal CcwState? CcwData;
}

/* an activation factory: IActivationFactory plus the factory, statics and composable interfaces of one class */
public abstract class ShimFactory : ShimObject
{
    public abstract string ClassName { get; }
    public override string RuntimeClassName => ClassName;
    public virtual unsafe int ActivateInstance(IntPtr* result)
    {
        Ccw.NotImplemented(ClassName + ".ActivateInstance");
        *result = IntPtr.Zero;
        return unchecked((int)0x80004001);
    }
}

internal sealed unsafe class CcwState
{
    public readonly ShimObject Managed;
    public readonly GCHandle Handle;
    public int Refs;
    public IntPtr Outer;
    public IntPtr Inner;                              /* the non-delegating block, when aggregated */
    public readonly ConcurrentDictionary<Guid, IntPtr> Blocks = new();
    public readonly (Guid Iid, IntPtr Vtbl, Func<object, bool> Implements)[] Table;

    public CcwState(ShimObject managed)
    {
        Managed = managed;
        Handle = GCHandle.Alloc(this);
        if (managed is ShimFactory f)
        {
            var list = new List<(Guid, IntPtr, Func<object, bool>)> { (Ccw.IID_IActivationFactory, Ccw.ActivationFactoryVtbl, _ => true) };
            if (ClassTable.Factory.TryGetValue(f.ClassName, out var t)) list.AddRange(t);
            Table = list.ToArray();
        }
        else if (!ClassTable.Interfaces.TryGetValue(managed.RuntimeClassName, out Table!))
            Table = Array.Empty<(Guid, IntPtr, Func<object, bool>)>();
    }

    public IntPtr Block(Guid iid, IntPtr vtbl) => Blocks.GetOrAdd(iid, _ =>
    {
        var p = (IntPtr*)NativeMemory.Alloc(16);
        p[0] = vtbl;
        p[1] = GCHandle.ToIntPtr(Handle);
        return (IntPtr)p;
    });

    /* the identity: the block of the first (default) interface */
    public IntPtr Identity => Table.Length > 0 ? Block(Table[0].Iid, Table[0].Vtbl) : Block(Ccw.IID_IInspectable, Ccw.InspectableVtbl);

    public int LocalQuery(Guid iid, IntPtr* result)
    {
        *result = IntPtr.Zero;
        if (iid == Ccw.IID_IUnknown || iid == Ccw.IID_IInspectable ||
            iid == Ccw.IID_IAgileObject || iid == Ccw.IID_IShimObject)
        {
            *result = iid == Ccw.IID_IUnknown && Inner != IntPtr.Zero ? Inner : Identity;
        }
        else
        {
            foreach (var (i, vtbl, _) in Table)
                if (i == iid) { *result = Block(i, vtbl); break; }
        }
        if (*result == IntPtr.Zero)
        {
            Ccw.Trace($"QI {Managed.RuntimeClassName} {iid}: no interface");
            return unchecked((int)0x80004002);
        }
        /* COM aggregation: the reference goes where the returned pointer's Release will go, which for every block but
         * the non-delegating inner one is the outer object */
        if (Outer != IntPtr.Zero && *result != Inner) Ccw.ComAddRef(Outer);
        else Interlocked.Increment(ref Refs);
        return 0;
    }
}

public static unsafe class Ccw
{
    public static readonly Guid IID_IUnknown = new("00000000-0000-0000-C000-000000000046");
    public static readonly Guid IID_IInspectable = new("AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90");
    public static readonly Guid IID_IAgileObject = new("94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90");
    public static readonly Guid IID_IActivationFactory = new("00000035-0000-0000-C000-000000000046");
    /* private: answered only by our objects, so an outer object forwards it to its inner shim */
    public static readonly Guid IID_IShimObject = new("6d1f8a0e-3a52-4c55-9b0e-57696e655845");

    static readonly ConditionalWeakTable<object, ShimObject> wrappers = new();
    static readonly string logPath = Path.Combine(Path.GetTempPath(), "xamlshim.txt");
    static readonly object logLock = new();
    public static bool Tracing = Environment.GetEnvironmentVariable("XAMLSHIM_TRACE") == "1";

    public static void Log(string line)
    {
        lock (logLock) File.AppendAllText(logPath, $"{Environment.CurrentManagedThreadId}: {line}{Environment.NewLine}");
    }
    public static void Trace(string line) { if (Tracing) Log(line); }
    public static void NotImplemented(string what) => Log("not implemented: " + what);
    public static int Fail(string what, Exception e) { Log($"exception in {what}: {e}"); return e.HResult != 0 ? e.HResult : unchecked((int)0x80004005); }

    // ---- vtables ----

    public static readonly IntPtr InspectableVtbl = MakeVtbl(0);
    public static readonly IntPtr ActivationFactoryVtbl = MakeVtbl(1);
    static readonly IntPtr innerVtbl = MakeInner();
    public static IntPtr QueryThunk => (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, Guid*, IntPtr*, int>)&Query;

    static IntPtr MakeVtbl(int extra)
    {
        var v = (IntPtr*)RuntimeHelpers.AllocateTypeAssociatedMemory(typeof(Ccw), sizeof(IntPtr) * (6 + extra));
        FillInspectable(v);
        if (extra == 1) v[6] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, IntPtr*, int>)&ActivateInstance;
        return (IntPtr)v;
    }

    static IntPtr MakeInner()
    {
        var v = (IntPtr*)RuntimeHelpers.AllocateTypeAssociatedMemory(typeof(Ccw), sizeof(IntPtr) * 6);
        FillInspectable(v);
        v[0] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, Guid*, IntPtr*, int>)&InnerQuery;
        v[1] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, uint>)&InnerAddRef;
        v[2] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, uint>)&InnerRelease;
        return (IntPtr)v;
    }

    public static void FillInspectable(IntPtr* v)
    {
        v[0] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, Guid*, IntPtr*, int>)&Query;
        v[1] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, uint>)&AddRef;
        v[2] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, uint>)&Release;
        v[3] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, uint*, Guid**, int>)&GetIids;
        v[4] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, IntPtr*, int>)&GetRuntimeClassName;
        v[5] = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr, int*, int>)&GetTrustLevel;
    }

    static CcwState State(IntPtr block) => (CcwState)GCHandle.FromIntPtr(((IntPtr*)block)[1]).Target!;
    public static object Target(IntPtr block) => State(block).Managed;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int Query(IntPtr block, Guid* iid, IntPtr* result)
    {
        var s = State(block);
        if (s.Outer != IntPtr.Zero) return ((delegate* unmanaged[Stdcall]<IntPtr, Guid*, IntPtr*, int>)(*(IntPtr**)s.Outer)[0])(s.Outer, iid, result);
        return s.LocalQuery(*iid, result);
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static uint AddRef(IntPtr block)
    {
        var s = State(block);
        if (s.Outer != IntPtr.Zero) return ComAddRef(s.Outer);
        return (uint)Interlocked.Increment(ref s.Refs);
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static uint Release(IntPtr block)
    {
        var s = State(block);
        if (s.Outer != IntPtr.Zero) return ComRelease(s.Outer);
        return (uint)Math.Max(0, Interlocked.Decrement(ref s.Refs));   /* never freed in the experiment */
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int InnerQuery(IntPtr block, Guid* iid, IntPtr* result) => State(block).LocalQuery(*iid, result);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static uint InnerAddRef(IntPtr block) => (uint)Interlocked.Increment(ref State(block).Refs);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static uint InnerRelease(IntPtr block) => (uint)Math.Max(0, Interlocked.Decrement(ref State(block).Refs));
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int GetIids(IntPtr block, uint* count, Guid** iids) { *count = 0; *iids = null; return 0; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int GetRuntimeClassName(IntPtr block, IntPtr* name) { *name = HString.Create(State(block).Managed.RuntimeClassName); return 0; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int GetTrustLevel(IntPtr block, int* level) { *level = 0; return 0; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvStdcall) })]
    static int ActivateInstance(IntPtr block, IntPtr* result)
    {
        try { return ((ShimFactory)Target(block)).ActivateInstance(result); }
        catch (Exception e) { return Fail("ActivateInstance", e); }
    }

    // ---- managed side ----

    static CcwState StateOf(ShimObject o)
    {
        lock (o) return o.CcwData ??= new CcwState(o);
    }

    public static string Describe(ShimObject o)
    {
        var s = o.CcwData;
        if (s == null) return "no ccw";
        var vtbl = s.Outer != IntPtr.Zero ? *(IntPtr*)s.Outer : IntPtr.Zero;
        return $"outer {s.Outer:x} vtbl {vtbl:x} inner {s.Inner:x} refs {s.Refs}";
    }

    /* an AddRef'd IInspectable for o (its outer object when aggregated) */
    public static IntPtr Get(ShimObject? o)
    {
        if (o == null) return IntPtr.Zero;
        var s = StateOf(o);
        if (s.Outer != IntPtr.Zero) { ComAddRef(s.Outer); return s.Outer; }
        Interlocked.Increment(ref s.Refs);
        return s.Identity;
    }

    /* an AddRef'd pointer to the default interface of o: what the ABI passes for a parameter of a class type */
    public static IntPtr GetDefault(ShimObject? o)
    {
        if (o == null) return IntPtr.Zero;
        var s = StateOf(o);
        return Get(o, s.Table.Length > 0 ? s.Table[0].Iid : IID_IInspectable);
    }

    /* an AddRef'd pointer to one interface of o */
    public static IntPtr Get(ShimObject o, Guid iid)
    {
        var s = StateOf(o);
        IntPtr p;
        if (s.Outer != IntPtr.Zero) { QueryInterface(s.Outer, iid, out p); return p; }
        s.LocalQuery(iid, &p);
        return p;
    }

    /* composable activation: the new object o is the inner of outer (may be null); returns (inner, instance) */
    public static (IntPtr Inner, IntPtr Instance) Compose(ShimObject o, IntPtr outer)
    {
        var s = StateOf(o);
        if (outer == IntPtr.Zero) return (IntPtr.Zero, Get(o));
        s.Outer = outer;                                  /* not AddRef'd: the outer owns the inner */
        var inner = (IntPtr*)NativeMemory.Alloc(16);
        inner[0] = innerVtbl;
        inner[1] = GCHandle.ToIntPtr(s.Handle);
        s.Inner = (IntPtr)inner;
        s.Refs = 1;
        var instance = s.Identity;
        ComAddRef(outer);
        return ((IntPtr)inner, instance);
    }

    /* the shim object behind a native pointer: one of our blocks, or an outer object that aggregates one */
    public static ShimObject? Unwrap(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        if ((*(IntPtr**)p)[0] == QueryThunk) return State(p).Managed;
        if (QueryInterface(p, IID_IShimObject, out var block) < 0) return null;
        var o = State(block).Managed;
        ComRelease(block);
        return o;
    }

    /* the shim wrapping a framework (Uno) object, made once per object */
    public static T Wrap<T>(object uno, Func<T> make) where T : ShimObject => (T)wrappers.GetValue(uno, _ => make());
    public static ShimObject? Wrapped(object uno) => wrappers.TryGetValue(uno, out var w) ? w : null;

    public static int QueryInterface(IntPtr p, Guid iid, out IntPtr result)
    {
        IntPtr r;
        int hr = ((delegate* unmanaged[Stdcall]<IntPtr, Guid*, IntPtr*, int>)(*(IntPtr**)p)[0])(p, &iid, &r);
        result = r;
        return hr;
    }
    public static uint ComAddRef(IntPtr p) => ((delegate* unmanaged[Stdcall]<IntPtr, uint>)(*(IntPtr**)p)[1])(p);
    public static uint ComRelease(IntPtr p) => ((delegate* unmanaged[Stdcall]<IntPtr, uint>)(*(IntPtr**)p)[2])(p);
}

/* Windows.UI.Xaml.Interop.TypeName: HSTRING Name, TypeKind Kind (Primitive 0, Metadata 1, Custom 2) */
[StructLayout(LayoutKind.Sequential)]
public struct TypeName
{
    public IntPtr Name;
    public int Kind;
}

public static unsafe class HString
{
    [DllImport("combase.dll")] static extern int WindowsCreateString(char* s, uint length, IntPtr* result);
    [DllImport("combase.dll")] static extern char* WindowsGetStringRawBuffer(IntPtr s, uint* length);
    [DllImport("combase.dll")] static extern int WindowsDeleteString(IntPtr s);

    public static IntPtr Create(string s)
    {
        IntPtr h;
        fixed (char* p = s) WindowsCreateString(p, (uint)s.Length, &h);
        return h;
    }
    public static string Read(IntPtr h)
    {
        if (h == IntPtr.Zero) return "";
        uint length;
        var p = WindowsGetStringRawBuffer(h, &length);
        return new string(p, 0, (int)length);
    }
    public static void Delete(IntPtr h) { if (h != IntPtr.Zero) WindowsDeleteString(h); }
}
