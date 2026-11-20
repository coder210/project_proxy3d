using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/input_consume_input_system.h：消费网络输入命令，写入玩家输入状态。
    /// - move_bits = dir（整段位掩码），yaw_quantized 直写
    /// - 跳跃上升沿：jump_down && !jump_prev → jump_requested，再更新 jump_prev
    /// - attack 非 0 时按类型置 PendingMelee/Bullet/Ray（对应 pending_actions 入队）
    /// 说明：C++ pending_actions 是 {seq,type} FIFO 队列，本迁移沿用现有 bool 表示
    /// （同一 tick 单玩家至多一种攻击，多攻击并发场景不在 parity 覆盖范围）。
    /// </summary>
    public static class InputConsumeInputSystem
    {
        public static void Update(GameState state)
        {
            var net = state.Net;
            if (net.FrameId < 0) return;

            for (int i = 0; i < net.NumInputs; ++i)
            {
                var input = net.Inputs[i];

                // 按 conv 找存活玩家
                int slot = -1;
                for (int k = 0; k < state.Players.Length; ++k)
                {
                    var p = state.Players[k];
                    if (p.Alive && p.Conv == input.Conv) { slot = k; break; }
                }
                if (slot < 0) continue;

                var pl = state.Players[slot];
                pl.MoveBits = (byte)input.Dir;
                pl.YawQuantized = input.YawQuantized;

                // 跳跃上升沿（INPUT_JUMP = 1<<5 = 32）
                bool jumpDown = (input.Dir & GameConst.InputJump) != 0;
                if (jumpDown && !pl.JumpPrev) pl.JumpRequested = true;
                pl.JumpPrev = jumpDown;

                // 攻击事件入队 → 映射到待处理标志
                int atk = input.Attack;
                if (atk == GameConst.AttackMelee) pl.PendingMelee = true;
                else if (atk == GameConst.AttackBullet) pl.PendingBullet = true;
                else if (atk == GameConst.AttackRay) pl.PendingRay = true;

                state.Players[slot] = pl;
            }
            net.NumInputs = 0;
        }
    }
}
