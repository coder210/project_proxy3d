using System;
using System.Runtime.InteropServices;

namespace ManagedSmoke
{
    /// <summary>
    /// CoreCLR 迁移 spike：验证 Godot 式函数指针表绑定。
    /// C++ 侧把 unmanaged_callbacks[] 数组地址 + 字节大小传给 Initialize，
    /// C# 侧按 IntPtr.Size 步长读回每个 delegate* unmanaged 函数指针。
    /// </summary>
    public static unsafe class Entry
    {
        // 回调表顺序必须与 host.cpp 的 unmanaged_callbacks[] 完全一致。
        private static delegate* unmanaged[Cdecl]<int, int, int> s_add;
        private static delegate* unmanaged[Cdecl]<IntPtr, int> s_log;
        private static bool s_inited;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        public static void Initialize(IntPtr callbacks, int byteSize)
        {
            if (s_inited) throw new InvalidOperationException("Already initialized.");
            s_inited = true;

            int count = byteSize / IntPtr.Size;
            if (count < 2) throw new ArgumentException("callback table too small", nameof(byteSize));

            byte* p = (byte*)callbacks;
            s_add = (delegate* unmanaged[Cdecl]<int, int, int>)ReadCallback(p, 0);
            s_log = (delegate* unmanaged[Cdecl]<IntPtr, int>)ReadCallback(p, 1);
        }

        private static void* ReadCallback(byte* basePtr, int index)
        {
            return *(void**)(basePtr + index * IntPtr.Size);
        }

        // native 入口：必须用 UnmanagedCallersOnly 才能被 hostfxr 取到函数指针。
        // 返回 42 表示成功；返回值让 native 侧断言。
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        public static int Run()
        {
            Console.WriteLine("[managed] Run entered");
            Console.WriteLine($"[managed] Debugger.IsAttached = {System.Diagnostics.Debugger.IsAttached}");

            if (Environment.GetEnvironmentVariable("SMOKE_LAUNCH_DEBUGGER") == "1")
            {
                System.Diagnostics.Debugger.Launch();
            }

            // 通过函数指针表回调 native：验证 managed -> native 绑定。
            int sum = s_add(40, 2);
            Console.WriteLine($"[managed] native add(40,2) = {sum}");

            BreakpointTarget();
            return sum == 42 ? 42 : -1;
        }

        private static void BreakpointTarget()
        {
            Console.WriteLine("[managed] set a breakpoint here");
        }
    }
}
