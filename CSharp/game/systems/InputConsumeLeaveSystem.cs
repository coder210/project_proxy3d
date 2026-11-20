using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/input_consume_leave_system.h：消费网络离开请求，销毁玩家实体。
    /// 按 conv 匹配存活玩家并置 Alive=false（等价 registry.destroy）。
    /// </summary>
    public static class InputConsumeLeaveSystem
    {
        public static void Update(GameState state)
        {
            var net = state.Net;
            if (net.FrameId < 0) return;

            for (int i = 0; i < net.NumLeaves; ++i)
            {
                int conv = net.Leaves[i].Conv;
                for (int k = 0; k < state.Players.Length; ++k)
                {
                    var p = state.Players[k];
                    if (p.Alive && p.Conv == conv)
                    {
                        p.Alive = false;
                        state.Players[k] = p;
                        break;
                    }
                }
            }
            net.NumLeaves = 0;
        }
    }
}
