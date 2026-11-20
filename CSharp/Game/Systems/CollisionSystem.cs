using System;
using Joy.Api;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/collision_system.h：玩家与碰撞层分离。
    /// 几何检测（GJK box-vs-box）委托 native joy_collision_gjk3df_*（CollisionExt 桥），
    /// 与 C++ 同一实现 → 帧同步确定。推开公式（XZ 归一化法线 + 0.03 墙余量）逐行复刻。
    /// 项目实际碰撞体均为 BOX（玩家/敌人/静态障碍），SPHERE/OBB/MESH 暂不参与（C# 侧无数据）。
    /// </summary>
    public static class CollisionSystem
    {
        private static readonly long PAD = Fp.FromFloat(0.03f);
        private static readonly long EPS_DIR = Fp.FromFloat(1e-10f);
        private static readonly long EPS_N2 = Fp.FromFloat(1e-8f);

        public static void Update(GameState state)
        {
            for (int pi = 0; pi < state.Players.Length; ++pi)
            {
                var p = state.Players[pi];
                if (!p.Alive) continue;
                if (p.ColliderType != ColliderType.Box) continue;

                long px = p.X, py = p.Y, pz = p.Z;
                long phx = p.ColHx, phy = p.ColHy, phz = p.ColHz;

                // ---- 玩家 vs 静态障碍 ----
                for (int oi = 0; oi < state.ObstacleCount; ++oi)
                {
                    var o = state.Obstacles[oi];
                    Resolve(ref px, ref py, ref pz, phx, phy, phz,
                            o.X, o.Y, o.Z, o.Hx, o.Y, o.Hz, true);
                }

                // ---- 玩家 vs 敌人（敌人不可推动，仅推玩家；死亡敌人跳过）----
                for (int ei = 0; ei < state.Enemies.Length; ++ei)
                {
                    var e = state.Enemies[ei];
                    if (!e.Alive || e.Dead) continue;
                    if (e.ColliderType != ColliderType.Box) continue;
                    Resolve(ref px, ref py, ref pz, phx, phy, phz,
                            e.X, e.Y, e.Z, e.ColHx, e.ColHy, e.ColHz, false);
                }

                // ---- 玩家 vs 玩家（仅 other.index > pi，保证双端一致的两两分离顺序）----
                for (int qi = pi + 1; qi < state.Players.Length; ++qi)
                {
                    var q = state.Players[qi];
                    if (!q.Alive) continue;
                    if (q.ColliderType != ColliderType.Box) continue;
                    Resolve(ref px, ref py, ref pz, phx, phy, phz,
                            q.X, q.Y, q.Z, q.ColHx, q.ColHy, q.ColHz, false);
                }

                p.X = px; p.Z = pz;
                state.Players[pi] = p;
            }
        }

        /// <summary>玩家盒(px,py,pz,ph*) 与目标盒(bx,by,bz,bh*) 的分离（复刻 C++ 命中分支）。</summary>
        private static void Resolve(ref long px, ref long py, ref long pz,
                                    long phx, long phy, long phz,
                                    long bx, long by, long bz, long bhx, long bhy, long bhz,
                                    bool isWall)
        {
            // 初判方向：玩家 → 碰撞体的水平方向
            long d0x = Fp.Sub(bx, px);
            long d0z = Fp.Sub(bz, pz);
            long d0len2 = Fp.Add(Fp.Mul(d0x, d0x), Fp.Mul(d0z, d0z));
            long dirx, dirz;
            if (d0len2 > EPS_DIR) {
                long inv = Fp.Div(Fp.One(), Fp.Sqrt(d0len2));
                dirx = Fp.Mul(d0x, inv);
                dirz = Fp.Mul(d0z, inv);
            } else {
                dirx = Fp.One();
                dirz = 0;
            }

            if (!CollisionExt.BoxBox(px, py, pz, phx, phy, phz,
                                     bx, by, bz, bhx, bhy, bhz,
                                     dirx, 0, dirz,
                                     out long nx, out long ny, out long nz, out long depth))
                return;

            if (depth < 0) depth = Fp.Sub(0, depth);

            // 只在 XZ 水平平面推开；法线近乎竖直则不推
            long n2 = Fp.Add(Fp.Mul(nx, nx), Fp.Mul(nz, nz));
            if (n2 <= EPS_N2) return;

            long invN = Fp.Div(Fp.One(), Fp.Sqrt(n2));
            long dxf = Fp.Mul(nx, invN);
            long dzf = Fp.Mul(nz, invN);

            long pad = isWall ? PAD : 0;
            long push = Fp.Add(depth, pad);

            px = Fp.Sub(px, Fp.Mul(dxf, push));
            pz = Fp.Sub(pz, Fp.Mul(dzf, push));
        }
    }
}
