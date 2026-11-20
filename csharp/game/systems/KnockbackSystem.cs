using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 复刻 shared/systems/knockback_system.h：受击击退沿 (vx,vz) 每 tick 位移，
    /// 分轴碰撞（被墙挡住的轴不动，另一轴可滑动），tick 耗尽清除。
    ///
    /// 碰撞检测部分依赖静态障碍的 GJK3D + 地图数据，当前 C# 侧尚未引入静态障碍
    /// （parity 场景无障碍），故 knockback_blocked 恒为 false —— 击退退化为纯位移。
    /// 位移与 tick 递减逻辑与 C++ 逐位一致；接入障碍数据后在此补 blocked 判定即可。
    /// </summary>
    public static class KnockbackSystem
    {
        private static bool knockback_blocked(GameState s, Entity e, long nx, long nz)
        {
            // TODO: C# 侧引入静态障碍后，按 C++ knockback_blocked 用 GJK3D 判定分轴碰撞。
            // 当前无障碍集合，恒不阻挡（与 parity C++ harness 的空 static_view 一致）。
            return false;
        }

        private static void update(ref Entity e, GameState s)
        {
            if (e.KnockTicksLeft <= 0) return;

            // 分轴：X 被挡则不移动 X，Z 被挡则不移动 Z（贴墙滑动）
            long nx = Fp.Add(e.X, e.KnockVx);
            if (!knockback_blocked(s, e, nx, e.Z)) e.X = nx;
            long nz = Fp.Add(e.Z, e.KnockVz);
            if (!knockback_blocked(s, e, e.X, nz)) e.Z = nz;

            e.KnockTicksLeft -= 1;
            if (e.KnockTicksLeft <= 0) {
                e.KnockVx = 0;
                e.KnockVz = 0;
            }
        }

        public static void Update(GameState state)
        {
            for (int i = 0; i < state.Players.Length; ++i) {
                var e = state.Players[i];
                update(ref e, state);
                state.Players[i] = e;
            }
            for (int i = 0; i < state.Enemies.Length; ++i) {
                var e = state.Enemies[i];
                update(ref e, state);
                state.Enemies[i] = e;
            }
        }
    }
}
