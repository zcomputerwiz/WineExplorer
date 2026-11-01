// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
using System;
class RuntimeSmoke
{
    static int Main()
    {
        Console.WriteLine("Managed {0}-bit process: PASS ({1})", IntPtr.Size * 8, Environment.Version);
        return 0;
    }
}
