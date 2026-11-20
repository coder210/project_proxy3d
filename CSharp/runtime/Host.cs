using System;
using ProxyGame;

// 入口类命名空间/类名必须与宿主的 csx_find_method("JoyGame","Host",...) 一致。
namespace JoyGame
{
    /// <summary>
    /// C++ 瘦宿主通过 joy cs 模块反射调用的入口。
    /// 方法名/参数个数必须与宿主 csx_find_method 一致：
    ///   OnInit(int width, int height, int smokeFrames)
    ///   OnFrame(float dt)
    ///   OnShutdown()
    ///
    /// 三条路径：
    ///  - PROXY_PARITY=1：快照自检（回归用），不建窗口/渲染。
    ///  - smokeFrames>0：headless 冒烟，只驱动逻辑 tick，不建 GPU 上下文。
    ///  - 正常：GameApp3D 自建窗口 + r3d，C# 全权驱动逻辑与 3D 渲染。
    /// </summary>
    public static class Host
    {
        public const float FixedDt = 1.0f / 20.0f;

        private static GameApp3D _app;
        private static GameState _smokeState;
        private static float _accumulator;
        private static int _frame;

        public static void OnInit(int width, int height, int smokeFrames)
        {
            if (Environment.GetEnvironmentVariable("PROXY_PARITY") == "1")
            {
                Parity.Run();
                Joy.Host.set_exit_code(0);
                Joy.Host.quit();
                return;
            }

            if (smokeFrames > 0)
            {
                // headless 冒烟：不建窗口/r3d，只跑确定性逻辑，验证无异常。
                _smokeState = new GameState();
                _accumulator = 0f;
                _frame = 0;
                Joy.Host.log("[proxy.cs] smoke init " + smokeFrames + " frames (logic-only)");
                return;
            }

            _app = new GameApp3D(width, height);
            _app.Start();
            Joy.Host.log("[proxy.cs] 3D init " + width + "x" + height);
        }

        public static void OnFrame(float dt)
        {
            if (_app != null) { _app.Tick(dt); _frame++; return; }
            if (_smokeState == null) return;
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;
            _accumulator += dt;
            while (_accumulator >= FixedDt)
            {
                SmokeTick(FixedDt);
                _accumulator -= FixedDt;
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

        public static void OnShutdown()
        {
            Joy.Host.log("[proxy.cs] shutdown frames=" + _frame);
        }

        public static int Frame => _frame;
    }
}
