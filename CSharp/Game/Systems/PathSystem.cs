using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 敌人 A* 寻路系统主循环（复刻 shared/systems/pathfinding_system.h:238-326）。
    /// AiSystem 写 PathTargetGx/Gz + PathDirty；本系统按 10 tick 节流重算路径，
    /// 并沿路径逐格中心轴对齐逼近移动。死亡敌人与攻击状态跳过。
    /// 工作区常驻 GameState，全程定点/整数运算，帧同步确定。
    /// </summary>
    public static class PathSystem
    {
        public static void Update(GameState state)
        {
            if (state.PfWalkableDirty)
            {
                var walkable = Pathfinding.BuildWalkable(state);
                Array.Copy(walkable, state.PfWalkable, MapConst.Cells);
                state.PfWalkableDirty = false;
            }

            for (int i = 0; i < state.Enemies.Length; ++i)
            {
                var e = state.Enemies[i];
                if (!e.Alive || e.Dead) continue;

                // 仅巡逻/追击/返回需要 A* 移动；攻击状态清空路径原地不动
                if (e.AiState != AiSystem.AI_PATROL && e.AiState != AiSystem.AI_CHASE && e.AiState != AiSystem.AI_RETURN)
                {
                    e.PathLen = 0;
                    e.NextIdx = 0;
                    e.PathDirty = false;
                    state.Enemies[i] = e;
                    continue;
                }

                if (e.RecomputeCooldown > 0) e.RecomputeCooldown--;
                if (e.PathDirty)
                {
                    if (e.RecomputeCooldown == 0)
                    {
                        e.RecomputeCooldown = Pathfinding.RecomputeInterval;
                        int sx = Pathfinding.WorldToGrid(e.X);
                        int sz = Pathfinding.WorldToGrid(e.Z);
                        e.PathLen = Pathfinding.AStar(state.PfWalkable, sx, sz,
                            e.PathTargetGx, e.PathTargetGz, state.EnemyPaths[i], MapConst.Cells,
                            state.PfGScore, state.PfFScore, state.PfCame,
                            state.PfClosed, state.PfInOpen, state.PfHeap);
                        e.NextIdx = 0;
                    }
                    e.PathDirty = false;
                }

                if (e.PathLen > 0 && e.NextIdx < e.PathLen)
                {
                    int idx = state.EnemyPaths[i][e.NextIdx];
                    int gx = idx % MapConst.GridN;
                    int gz = idx / MapConst.GridN;
                    long tx = Pathfinding.GridToWorld(gx);
                    long tz = Pathfinding.GridToWorld(gz);
                    long dx = Fp.Sub(tx, e.X);
                    long dz = Fp.Sub(tz, e.Z);

                    if (Fp.Abs(dx) <= Pathfinding.PF_STEP && Fp.Abs(dz) <= Pathfinding.PF_STEP)
                    {
                        e.X = tx;
                        e.Z = tz;
                        e.NextIdx++;
                    }
                    else if (Fp.Abs(dx) > Fp.Abs(dz))
                    {
                        e.X = dx > 0 ? Fp.Add(e.X, Pathfinding.PF_STEP) : Fp.Sub(e.X, Pathfinding.PF_STEP);
                    }
                    else
                    {
                        e.Z = dz > 0 ? Fp.Add(e.Z, Pathfinding.PF_STEP) : Fp.Sub(e.Z, Pathfinding.PF_STEP);
                    }
                }
                else if (e.AiState != AiSystem.AI_CHASE)
                {
                    e.ActionState = AiSystem.ACT_IDLE;
                }

                state.Enemies[i] = e;
            }
        }
    }
}
