using System;
using System.Runtime.InteropServices;

namespace ManagedSmoke
{
    public static class Entry
    {
        [UnmanagedCallersOnly]
        public static int Run()
        {
            Console.WriteLine("[managed] Run entered");
            Console.WriteLine($"[managed] Debugger.IsAttached = {System.Diagnostics.Debugger.IsAttached}");

            // 兜底：若 VS 的混合(.NET Core)调试引擎没能自动挂上，设置环境变量
            // SMOKE_LAUNCH_DEBUGGER=1 后会弹出附加托管调试器对话框，选当前 VS 实例即可命中断点。
            if (Environment.GetEnvironmentVariable("SMOKE_LAUNCH_DEBUGGER") == "1")
            {
                System.Diagnostics.Debugger.Launch();
            }

            BreakpointTarget();
            return 42;
        }

        private static void BreakpointTarget()
        {
            Console.WriteLine("[managed] set a breakpoint here");
        }
    }
}
