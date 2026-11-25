// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// lab test app: logs the 0x60 messages (and a few others) that the app's own threads see, through thread hooks installed from inside
// the same process (WH_CALLWNDPROC: sent messages, WH_GETMESSAGE: posted ones); the log path is in the environment variable CMT_HOOKLOG
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace CmTest
{
    public static class HookLog
    {
        private delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")] private static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr module, uint thread);
        [DllImport("user32.dll")] private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();

        private static readonly object sync = new object();
        private static StreamWriter writer;
        private static readonly HashSet<uint> hooked = new HashSet<uint>();
        private static readonly List<HookProc> keep = new List<HookProc>();
        private static readonly int start = Environment.TickCount;

        private static void Log(string kind, IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp)
        {
            if (msg != 0x60 && msg != 0x113) return;
            var cls = new StringBuilder(64);
            GetClassName(hwnd, cls, 64);
            lock (sync)
            {
                writer.WriteLine("{0,5} tid {1} {2} hwnd {3:x} '{4}' msg {5:x} wp {6:x} lp {7:x}", Environment.TickCount - start,
                                 GetCurrentThreadId(), kind, (long)hwnd, cls, msg, (long)wp, (long)lp);
                writer.Flush();
            }
        }

        private static IntPtr CallWndProc(int code, IntPtr wParam, IntPtr lParam)
        {
            if (code >= 0)
            {
                IntPtr lp = Marshal.ReadIntPtr(lParam, 0), wp = Marshal.ReadIntPtr(lParam, 8);
                uint msg = (uint)Marshal.ReadInt32(lParam, 16);
                IntPtr hwnd = Marshal.ReadIntPtr(lParam, 24);
                Log(wParam != IntPtr.Zero ? "sent-by-this-thread" : "sent", hwnd, msg, wp, lp);
            }
            return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
        }

        private static IntPtr GetMsg(int code, IntPtr wParam, IntPtr lParam)
        {
            if (code >= 0)
            {
                IntPtr hwnd = Marshal.ReadIntPtr(lParam, 0);
                uint msg = (uint)Marshal.ReadInt32(lParam, 8);
                IntPtr wp = Marshal.ReadIntPtr(lParam, 16), lp = Marshal.ReadIntPtr(lParam, 24);
                Log(wParam.ToInt32() == 1 ? "posted(remove)" : "posted(peek)", hwnd, msg, wp, lp);
            }
            return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
        }

        [DllImport("ntdll.dll")] private static extern int NtQueryObject(IntPtr h, int cls, byte[] info, int len, out int ret);
        [DllImport("ntdll.dll")] private static extern int NtSetIoCompletion(IntPtr port, IntPtr key, IntPtr apc, int status, IntPtr info);
        [DllImport("kernel32.dll")] private static extern bool GetHandleInformation(IntPtr h, out uint flags);

        // black-box experiment: post a completion with APC context 0 to every protected IoCompletion port of this process (the ones
        // that NtUserInitThreadCoreMessagingIocp2 returned) and note what happens (the process survives or not, the hook log shows
        // the messages)
        private static void InjectCompletions()
        {
            string mode = Environment.GetEnvironmentVariable("CMT_INJECT");
            if (string.IsNullOrEmpty(mode)) return;
            Thread.Sleep(4000);
            var ports = new List<IntPtr>();
            for (long v = 4; v < 0x4000; v += 4)
            {
                var buf = new byte[512];
                int len;
                if (NtQueryObject((IntPtr)v, 2, buf, buf.Length, out len) != 0) continue;
                int n = BitConverter.ToUInt16(buf, 0) / 2;
                long ptr = BitConverter.ToInt64(buf, 8);
                string name = n > 0 && n < 40 ? Marshal.PtrToStringUni((IntPtr)ptr, n) : "";
                uint flags;
                if (name == "IoCompletion" && GetHandleInformation((IntPtr)v, out flags) && (flags & 2) != 0) ports.Add((IntPtr)v);
            }
            lock (sync) { writer.WriteLine("{0,5} protected IoCompletion handles: {1}", Environment.TickCount - start, string.Join(",", ports.ConvertAll(p => ((long)p).ToString("x")))); writer.Flush(); }
            foreach (var p in ports)
            {
                IntPtr apc = mode == "apc" ? (IntPtr)0x1234 : IntPtr.Zero;
                int st = NtSetIoCompletion(p, (IntPtr)0x77, apc, 0, IntPtr.Zero);
                lock (sync) { writer.WriteLine("{0,5} NtSetIoCompletion({1:x}, key 0x77, apc {2:x}) = {3:x}", Environment.TickCount - start, (long)p, (long)apc, st); writer.Flush(); }
                Thread.Sleep(1500);
            }
        }

        public static void Start()
        {
            string path = Environment.GetEnvironmentVariable("CMT_HOOKLOG");
            if (string.IsNullOrEmpty(path)) return;
            writer = new StreamWriter(path, false);
            var thread = new Thread(() =>
            {
                var proc1 = new HookProc(CallWndProc);
                var proc2 = new HookProc(GetMsg);
                keep.Add(proc1);
                keep.Add(proc2);
                uint self = GetCurrentThreadId();
                for (int i = 0; i < 300; i++)
                {
                    foreach (ProcessThread t in Process.GetCurrentProcess().Threads)
                    {
                        uint tid = (uint)t.Id;
                        if (tid == self || !hooked.Add(tid)) continue;
                        SetWindowsHookEx(4, proc1, IntPtr.Zero, tid);
                        SetWindowsHookEx(3, proc2, IntPtr.Zero, tid);
                    }
                    Thread.Sleep(20);
                }
            });
            thread.IsBackground = true;
            thread.Start();
            var inject = new Thread(InjectCompletions);
            inject.IsBackground = true;
            inject.Start();
        }
    }
}
