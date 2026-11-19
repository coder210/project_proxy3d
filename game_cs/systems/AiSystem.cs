using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/ai_system.h：敌人 AI 状态机。
    ///   0=巡逻 1=追击 2=攻击 3=返回出生点
    /// 输出：ai 状态、动作状态、寻路目标格（PathTargetGx/Gz + dirty）、攻击事件（PendingMelee）。
    /// 全部定点运算，双端确定。
    /// </summary>
    public static class AiSystem
    {
        public const int AI_PATROL = 0, AI_CHASE = 1, AI_ATTACK = 2, AI_RETURN = 3;
        public const int ACT_IDLE = 0, ACT_WALK = 1, ACT_ATTACK = 2;

        public static void Update(GameState state)
        {
            // 最近玩家（XZ）
            for (int ei = 0; ei < state.Enemies.Length; ++ei)
            {
                var e = state.Enemies[ei];
                if (!e.Alive) continue;
                if (e.Dead) {   // 死亡敌人：清攻击事件、不移动不攻击
                    e.PendingMelee = e.PendingBullet = e.PendingRay = false;
                    state.Enemies[ei] = e;
                    continue;
                }

                bool foundPlayer = false;
                long nearestDistSq = Fp.Mul(e.DetectRadius, e.DetectRadius);
                long nearestPx = 0, nearestPz = 0;

                for (int pi = 0; pi < state.Players.Length; ++pi)
                {
                    var p = state.Players[pi];
                    if (!p.Alive) continue;
                    long dx = Fp.Sub(p.X, e.X);
                    long dz = Fp.Sub(p.Z, e.Z);
                    long distSq = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                    if (distSq < nearestDistSq) {
                        nearestDistSq = distSq;
                        foundPlayer = true;
                        nearestPx = p.X;
                        nearestPz = p.Z;
                    }
                }

                long attackDistSq = Fp.Mul(e.AttackRadius, e.AttackRadius);
                bool inAttackRange = foundPlayer && (nearestDistSq < attackDistSq);

                // 领地判定：玩家是否在领地内（距 home <= guard_radius）；全局型（guard=0）始终算在内
                bool isGuard = e.GuardRadius > 0;
                long hdx = isGuard ? Fp.Sub(nearestPx, e.PatrolCenterX) : 0;
                long hdz = isGuard ? Fp.Sub(nearestPz, e.PatrolCenterZ) : 0;
                bool inTerritory = !isGuard ||
                    Fp.Add(Fp.Mul(hdx, hdx), Fp.Mul(hdz, hdz)) <= Fp.Mul(e.GuardRadius, e.GuardRadius);

                // 敌人到出生点距离（守卫返回判定）
                long ehx = Fp.Sub(e.X, e.PatrolCenterX);
                long ehz = Fp.Sub(e.Z, e.PatrolCenterZ);
                long distHomeSq = Fp.Add(Fp.Mul(ehx, ehx), Fp.Mul(ehz, ehz));
                long HOME_EPS = Fp.Div(Fp.FromInt(4), Fp.FromInt(5));   // 0.8m

                // 设置追击/返回目标格并置 dirty
                void SetTarget(long tx, long tz)
                {
                    int pgx = Pathfinding.WorldToGrid(tx), pgz = Pathfinding.WorldToGrid(tz);
                    if (pgx != e.PathTargetGx || pgz != e.PathTargetGz) {
                        e.PathTargetGx = pgx;
                        e.PathTargetGz = pgz;
                        e.PathDirty = true;
                    }
                }

                if (inAttackRange && inTerritory)
                {
                    // 状态 2：攻击（丧尸全部近战，不推送子弹）
                    e.AiState = AI_ATTACK;
                    e.ActionState = ACT_ATTACK;
                    e.PendingMelee = true;
                }
                else if (foundPlayer && inTerritory)
                {
                    // 状态 1：追击（走 A*）
                    e.AiState = AI_CHASE;
                    e.ActionState = ACT_WALK;
                    SetTarget(nearestPx, nearestPz);
                    e.AiTimer = 0;
                }
                else if (isGuard && !inTerritory && (e.AiState == AI_CHASE || e.AiState == AI_ATTACK))
                {
                    // 守卫被激怒后玩家逃出领地：返回出生点
                    e.AiState = AI_RETURN;
                    e.ActionState = ACT_WALK;
                    SetTarget(e.PatrolCenterX, e.PatrolCenterZ);
                }
                else if (e.AiState == AI_RETURN)
                {
                    if (distHomeSq < Fp.Mul(HOME_EPS, HOME_EPS)) {
                        e.AiState = AI_PATROL;
                        e.AiTimer = 120;
                    } else {
                        e.ActionState = ACT_WALK;
                        SetTarget(e.PatrolCenterX, e.PatrolCenterZ);
                    }
                }
                else
                {
                    // 状态 0：巡逻
                    e.AiState = AI_PATROL;
                    e.ActionState = ACT_WALK;
                    e.AiTimer -= 1;
                    if (e.AiTimer <= 0) {
                        e.TargetX = Fp.Sub(e.PatrolCenterX, e.PatrolRadius);
                        e.TargetZ = Fp.Sub(e.PatrolCenterZ, e.PatrolRadius);
                        e.AiTimer = 120;
                    }
                    SetTarget(e.TargetX, e.TargetZ);
                }

                state.Enemies[ei] = e;
            }
        }
    }
}
