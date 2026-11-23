using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using ProxyGame;

// 入口类命名空间/类名必须与宿主的 csx_find_method("JoyGame","GameApplication",...) 一致。
namespace JoyGame
{
    /// <summary>
    /// 引擎式托管入口（命名参考 MonoGame / Unity / Unreal）：
    ///   Initialize -> LoadContent -> BeginPlay -> [Update + FixedUpdate]* -> Shutdown
    /// C++ 宿主只反射 Initialize / Update / Shutdown 三个入口，其余由 C# 内部驱动。
    ///   三条路径：
    ///    - PROXY_PARITY=1：快照自检（回归用），不建窗口/渲染。
    ///    - smokeFrames&gt;0：headless 冒烟，只驱动固定步逻辑，不建 GPU 上下文。
    ///    - 正常：GameApp3D 自建窗口 + r3d，C# 全权驱动逻辑与 3D 渲染。
    /// </summary>
    public static class GameApplication
    {
        public const float FixedDt = 1.0f / 20.0f;

        private static GameApp3D _app;
        private static GameState _smokeState;
        private static float _fixedAccumulator;
        private static int _frame;

        /// <summary>
        /// CoreCLR 绑定初始化入口。必须由宿主从 JoyGame.dll 取函数指针调用，
        /// 保证 NativeFuncs.Initialize 与 Initialize/Update/Shutdown 在同一个
        /// AssemblyLoadContext 实例上（否则函数指针表填在 A、业务调用落在 B，全 NULL）。
        /// </summary>
        // 桌面 UCO 入口：逻辑内联，不转调普通方法（避免 Linux/macOS
        // CoreCLR 上 UCO wrapper 首调 JIT 的 GC transition 平台差异）。
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Bootstrap(IntPtr lookupFn)
        {
            Joy.NativeFuncs.Initialize(lookupFn);
        }

        // Android 普通入口：mono_runtime_invoke 只能调普通 public static 方法。
        public static void BootstrapMono(IntPtr lookupFn)
        {
            Joy.NativeFuncs.Initialize(lookupFn);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Initialize(int width, int height, int smokeFrames)
        {
            // 调试辅助：JOY_ATTACH_DEBUGGER=1 时弹出 JIT 调试器选择（VS 需已注册为 JIT 调试器）。
            if (Environment.GetEnvironmentVariable("JOY_ATTACH_DEBUGGER") == "1") {
                try { System.Diagnostics.Debugger.Launch(); } catch { /* Mono 未启用调试支持时忽略 */ }
            }

            if (Environment.GetEnvironmentVariable("PROXY_PARITY") == "1")
            {
                Parity.Run();
                Joy.Host.set_exit_code(0);
                Joy.Host.quit();
                return;
            }

            if (smokeFrames > 0)
            {
                _smokeState = new GameState();
                _fixedAccumulator = 0f;
                _frame = 0;
                Joy.Host.log("[proxy.cs] smoke init " + smokeFrames + " frames (logic-only)");
                return;
            }

            _app = new GameApp3D(width, height);
            _fixedAccumulator = 0f;
            _frame = 0;
            LoadContent();
            BeginPlay();
            Joy.Host.log("[proxy.cs] 3D init " + width + "x" + height);
        }

        public static void InitializeMono(int width, int height, int smokeFrames)
        {
            // 调试辅助：JOY_ATTACH_DEBUGGER=1 时弹出 JIT 调试器选择（VS 需已注册为 JIT 调试器）。
            if (Environment.GetEnvironmentVariable("JOY_ATTACH_DEBUGGER") == "1") {
                try { System.Diagnostics.Debugger.Launch(); } catch { /* Mono 未启用调试支持时忽略 */ }
            }

            if (Environment.GetEnvironmentVariable("PROXY_PARITY") == "1")
            {
                Parity.Run();
                Joy.Host.set_exit_code(0);
                Joy.Host.quit();
                return;
            }

            if (smokeFrames > 0)
            {
                _smokeState = new GameState();
                _fixedAccumulator = 0f;
                _frame = 0;
                Joy.Host.log("[proxy.cs] smoke init " + smokeFrames + " frames (logic-only)");
                return;
            }

            _app = new GameApp3D(width, height);
            _fixedAccumulator = 0f;
            _frame = 0;
            LoadContent();
            BeginPlay();
            Joy.Host.log("[proxy.cs] 3D init " + width + "x" + height);
        }

        public static void LoadContent()
        {
            // GameApp3D 构造期已完成窗口/r3d/mesh/字体初始化；此入口保留给后续资源扩展。
            Joy.Host.log("[proxy.cs] content loaded");
        }

        public static void BeginPlay()
        {
            if (_app != null) _app.Start();
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Update(float dt)
        {
            AccumulateFixed(dt);

            if (_app != null) { _app.Tick(dt); _frame++; return; }
        }

        public static void UpdateMono(float dt)
        {
            AccumulateFixed(dt);

            if (_app != null) { _app.Tick(dt); _frame++; return; }
        }

        public static void FixedUpdate(float dt)
        {
            if (_app != null) _app.Scenes.FixedUpdate(dt);
            else if (_smokeState != null) SmokeTick(dt);
        }

        private static void AccumulateFixed(float dt)
        {
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;
            _fixedAccumulator += dt;
            while (_fixedAccumulator >= FixedDt)
            {
                FixedUpdate(FixedDt);
                _fixedAccumulator -= FixedDt;
            }
        }

        private static void SmokeTick(float dt)
        {
            _smokeState.Frame++;
            _frame++;
            ProxyGame.Systems.InputConsumeJoinSystem.Update(_smokeState);
            ProxyGame.Systems.InputConsumeLeaveSystem.Update(_smokeState);
            ProxyGame.Systems.InputConsumeInputSystem.Update(_smokeState);
            ProxyGame.Systems.PlayerDeathSystem.Update(_smokeState);
            ProxyGame.Systems.MovementSystem.Update(_smokeState);
            ProxyGame.Systems.KnockbackSystem.Update(_smokeState);
            ProxyGame.Systems.CleanupSystem.Update(_smokeState);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Shutdown()
        {
            Joy.Host.log("[proxy.cs] shutdown frames=" + _frame);
        }

        public static void ShutdownMono()
        {
            Joy.Host.log("[proxy.cs] shutdown frames=" + _frame);
        }

        public static int Frame => _frame;
        public static float FixedAlpha => _fixedAccumulator / FixedDt;
    }
}
