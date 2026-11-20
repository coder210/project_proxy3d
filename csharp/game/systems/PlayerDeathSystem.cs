using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/player_death_system.h。
    /// 玩家死亡判定 / 重生计时 / 回出生点 / 清输入动作状态；敌人不自动重生（由 wave 处理）。
    /// 全部用整数 tick 与定点位置，与 C++ 同一逻辑，无需网络同步。
    /// </summary>
    public static class PlayerDeathSystem
    {
        public static void Update(GameState state)
        {
            for (int i = 0; i < state.Players.Length; ++i)
            {
                var p = state.Players[i];
                if (!p.Alive) continue;

                // 死亡判定：HP 归零（melee_attack 已把 HP 钳制在 0）
                if (!p.Dead && p.Hp <= 0)
                {
                    p.Dead = true;
                    p.RespawnTimer = GameConst.PlayerRespawnTicks;
                }

                // 无敌保护倒计时
                if (p.SpawnInvuln > 0) p.SpawnInvuln -= 1;

                // 重生计时与传送
                if (p.Dead)
                {
                    p.RespawnTimer -= 1;
                    if (p.RespawnTimer <= 0)
                    {
                        p.Dead = false;
                        p.Hp = p.MaxHp;
                        p.SpawnInvuln = GameConst.SpawnInvulnTicks;
                        p.X = p.SpawnX; p.Y = p.SpawnY; p.Z = p.SpawnZ;
                        // 清重生前残留输入/动作状态
                        p.MoveBits = 0;
                        p.ActionState = 0;      // IDLE
                        p.MeleeCooldown = 0;
                        // 注：TransformComponent 是渲染插值坐标，逻辑层不持有，跳过。
                    }
                }

                state.Players[i] = p;
            }
        }
    }
}
