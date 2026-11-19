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
    /// 当前骨架：只维护 GameState 与固定步累加器，不建渲染（r3d 由宿主持有后借用，
    /// 渲染 pass 迁移阶段再接）。逻辑 tick 固定 1/20 秒，与 C++ 原 FIXED_TIMESTEP 一致。
    /// </summary>
    public static class Host
    {
        public const float FixedDt = 1.0f / 20.0f;

        private static GameState _state;
        private static float _accumulator;
        private static int _frame;

        public static void OnInit(int width, int height, int smokeFrames)
        {
            // 快照模式：由 PROXY_PARITY 环境变量触发，输出确定场景的状态快照后让宿主退出。
            // 供回归/自检使用，与已移除的 C++ parity 基准保持同格式。
            if (Environment.GetEnvironmentVariable("PROXY_PARITY") == "1")
            {
                Parity.Run();
                Joy.Host.set_exit_code(0);
                Joy.Host.quit();
                return;
            }

            _state = new GameState();
            _accumulator = 0f;
            _frame = 0;
            Joy.Host.log("[proxy.cs] init " + width + "x" + height
                       + (smokeFrames > 0 ? " smoke=" + smokeFrames : ""));
        }

        public static void OnFrame(float dt)
        {
            if (_state == null) return;
            if (dt < 0f) dt = 0f;
            if (dt > 0.1f) dt = 0.1f;

            _accumulator += dt;
            while (_accumulator >= FixedDt)
            {
                Tick(FixedDt);
                _accumulator -= FixedDt;
            }
        }

        private static void Tick(float dt)
        {
            _state.Frame++;
            _frame++;
            // 系统按 C++ 的相对顺序逐个迁入：
            // PreUpdate: join → leave → input（消费网络命令；C++ 在 game_scene 注册）
            // OnUpdate: ai → pathfinding → melee_attack → bullet → player_death → wave → wave_spawn
            // → movement → knockback → collision → cleanup
            ProxyGame.Systems.InputConsumeJoinSystem.Update(_state);
            ProxyGame.Systems.InputConsumeLeaveSystem.Update(_state);
            ProxyGame.Systems.InputConsumeInputSystem.Update(_state);
            ProxyGame.Systems.PlayerDeathSystem.Update(_state);
            ProxyGame.Systems.MovementSystem.Update(_state);
            ProxyGame.Systems.KnockbackSystem.Update(_state);
            // 帧尾清输入（C++ cleanup_system，最后执行）
            ProxyGame.Systems.CleanupSystem.Update(_state);
        }

        public static void OnShutdown()
        {
            Joy.Host.log("[proxy.cs] shutdown frames=" + _frame);
        }

        /// <summary>供冒烟/测试读取内部状态（只读快照）。</summary>
        public static int Frame => _frame;
    }
}
