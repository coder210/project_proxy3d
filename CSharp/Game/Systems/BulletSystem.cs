using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/bullet_system.h：子弹移动 + 命中 + 销毁。
    /// 命中判定用 O(1) 定点球-盒（bullet_hit_box），替代昂贵的 GJK；
    /// 玩家/敌人/障碍均为轴对齐盒体，故球-盒精确且双端确定。
    /// </summary>
    public static class BulletSystem
    {
        private static readonly long BULLET_RADIUS = Fp.FromFloat(0.15f);
        private static readonly long KNOCKBACK_SPEED = Fp.FromFloat(0.08f);
        private const int KNOCKBACK_TICKS = 6;

        private static readonly long DEFAULT_HX = Fp.FromFloat(0.35f);
        private static readonly long DEFAULT_HY = Fp.FromFloat(0.9f);
        private static readonly long DEFAULT_HZ = Fp.FromFloat(0.35f);

        /// <summary>球(c, r) 与轴对齐盒(bc, ths) 的定点判定（球心到盒最短点距离² ≤ r²）。</summary>
        private static bool HitBox(long cx, long cy, long cz, long r,
                                   long bx, long by, long bz,
                                   long hx, long hy, long hz)
        {
            long loX = Fp.Sub(bx, hx), hiX = Fp.Add(bx, hx);
            long loY = Fp.Sub(by, hy), hiY = Fp.Add(by, hy);
            long loZ = Fp.Sub(bz, hz), hiZ = Fp.Add(bz, hz);
            long clX = cx < loX ? loX : (cx > hiX ? hiX : cx);
            long clY = cy < loY ? loY : (cy > hiY ? hiY : cy);
            long clZ = cz < loZ ? loZ : (cz > hiZ ? hiZ : cz);
            long dx = Fp.Sub(cx, clX);
            long dy = Fp.Sub(cy, clY);
            long dz = Fp.Sub(cz, clZ);
            long d2 = Fp.Add(Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dy, dy)), Fp.Mul(dz, dz));
            return d2 <= Fp.Mul(r, r);
        }

        public static void Update(GameState state)
        {
            int write = 0;
            for (int i = 0; i < state.BulletCount; ++i)
            {
                var b = state.Bullets[i];

                // 1) 移动
                b.X = Fp.Add(b.X, Fp.Mul(b.DX, b.Speed));
                b.Z = Fp.Add(b.Z, Fp.Mul(b.DZ, b.Speed));
                b.RangeLeft = Fp.Sub(b.RangeLeft, b.Speed);
                if (b.RangeLeft <= 0) continue;   // 射程耗尽 → 销毁

                bool hit = false;

                // 2) 命中目标（按来源选目标侧）
                if (b.FromEnemy)
                {
                    for (int pi = 0; pi < state.Players.Length && !hit; ++pi)
                    {
                        var t = state.Players[pi];
                        if (!t.Alive || t.Dead) continue;
                        if (t.Id == b.OwnerId) continue;          // 不伤射手自身
                        if (t.SpawnInvuln > 0) continue;          // 无敌保护
                        long hx = t.ColliderType == ColliderType.Box ? t.ColHx : DEFAULT_HX;
                        long hy = t.ColliderType == ColliderType.Box ? t.ColHy : DEFAULT_HY;
                        long hz = t.ColliderType == ColliderType.Box ? t.ColHz : DEFAULT_HZ;
                        if (HitBox(b.X, b.Y, b.Z, BULLET_RADIUS, t.X, t.Y, t.Z, hx, hy, hz))
                        {
                            t.Hp -= b.Damage;
                            if (t.Hp < 0) t.Hp = 0;
                            if (t.Hp <= 0 && !t.Dead) t.Dead = true;
                            state.Players[pi] = t;
                            hit = true;
                        }
                    }
                }
                else
                {
                    for (int ei = 0; ei < state.Enemies.Length && !hit; ++ei)
                    {
                        var t = state.Enemies[ei];
                        if (!t.Alive || t.Dead) continue;
                        if (t.Id == b.OwnerId) continue;
                        long hx = t.ColliderType == ColliderType.Box ? t.ColHx : DEFAULT_HX;
                        long hy = t.ColliderType == ColliderType.Box ? t.ColHy : DEFAULT_HY;
                        long hz = t.ColliderType == ColliderType.Box ? t.ColHz : DEFAULT_HZ;
                        if (HitBox(b.X, b.Y, b.Z, BULLET_RADIUS, t.X, t.Y, t.Z, hx, hy, hz))
                        {
                            t.Hp -= b.Damage;
                            if (t.Hp < 0) t.Hp = 0;
                            bool kill = false;
                            if (t.Hp <= 0 && !t.Dead) {
                                t.Dead = true;
                                state.Kills += 1;
                                kill = true;
                            }
                            if (!kill) {
                                t.KnockVx = Fp.Mul(b.DX, KNOCKBACK_SPEED);
                                t.KnockVz = Fp.Mul(b.DZ, KNOCKBACK_SPEED);
                                t.KnockTicksLeft = KNOCKBACK_TICKS;
                            }
                            state.Enemies[ei] = t;
                            hit = true;
                        }
                    }
                }
                if (hit) continue;

                // 3) 命中静态障碍 → 销毁（子弹不穿墙）
                for (int oi = 0; oi < state.ObstacleCount && !hit; ++oi)
                {
                    var o = state.Obstacles[oi];
                    if (HitBox(b.X, b.Y, b.Z, BULLET_RADIUS, o.X, o.Y, o.Z, o.Hx, o.Y, o.Hz))
                        hit = true;
                }
                if (hit) continue;

                state.Bullets[write++] = b;   // 存活，保留
            }
            state.BulletCount = write;
        }
    }
}
