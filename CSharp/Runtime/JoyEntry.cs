using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace JoyGame
{
    /// <summary>
    /// joy 宿主要求的入口契约（见 third_party/joy/modules/cs/joy_cs_entry.h）：
    /// 宿主从 JoyGame.dll 取 JoyGame.JoyEntry.Bootstrap（UCO）。
    /// 生命周期入口仍由宿主按 GameApplication 类名查找，不在这里重复。
    /// </summary>
    public static class JoyEntry
    {
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Bootstrap(IntPtr lookupFn)
        {
            // UCO 方法不能被 C# 直接调用，必须在此内联绑定初始化。
            Joy.NativeFuncs.Initialize(lookupFn);
        }

        public static void BootstrapMono(IntPtr lookupFn)
        {
            GameApplication.BootstrapMono(lookupFn);
        }
    }
}
