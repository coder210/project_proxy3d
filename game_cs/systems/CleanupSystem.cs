using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 复刻 shared/systems/cleanup_system.h：帧尾移除 PlayerStateInput。
    /// C++ 的 registry.remove&lt;PlayerStateInput&gt;(e) 等价于把该组件的三个字段
    /// 归零（下次 input_consume_input 重新 emplace 时默认构造即全 0）。
    /// 只有玩家带 PlayerStateInput（movement_system 的 view 印证），敌人不带。
    /// </summary>
    public static class CleanupSystem
    {
        public static void Update(GameState state)
        {
            for (int i = 0; i < state.Players.Length; ++i)
            {
                var p = state.Players[i];
                p.MoveBits = 0;
                p.YawQuantized = 0;
                p.PitchQuantized = 0;
                state.Players[i] = p;
            }
        }
    }
}
