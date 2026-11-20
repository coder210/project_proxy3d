using System;
using Joy.Api;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 shared/systems/melee_attack_system.h：攻击结算（近战 + 射击 + 射线）。
    /// 几何检测（半圆 mesh vs box、射线 vs OBB）委托 native GJK/ray（CollisionExt 桥）。
    /// 表现缓冲（attack_flashes/damage_texts/sfx/kill_events）仅客户端渲染用、不参与
    /// 帧同步，本迁移暂不产生（不影响逻辑状态与 checksum）。
    /// 执行顺序固定：玩家数组 → 敌人数组（与 C++ parity 内联一致）。
    /// </summary>
    public static class MeleeAttackSystem
    {
        public const int ENEMY_MELEE_DAMAGE = 1;
        public const int ENEMY_MELEE_DPS_COOLDOWN = 4;
        public const int ENEMY_POUNCE_DAMAGE = 3;
        public const int ENEMY_POUNCE_COOLDOWN = 20;
        public const int ENEMY_BULLET_DAMAGE = 5;

        public const int MELEE_COOLDOWN = 12;
        public const int BULLET_COOLDOWN = 30;
        public const int RAY_COOLDOWN = 45;

        private static readonly long BULLET_SPEED = Fp.FromFloat(0.4f);
        private static readonly long BULLET_RANGE = Fp.FromFloat(12.0f);
        private static readonly long KNOCKBACK_SPEED = Fp.FromFloat(0.08f);
        private const int KNOCKBACK_TICKS = 6;

        private const int SEMI_SEGS = 8;
        private static readonly long DEG2R = Fp.FromFloat(3.14159265f / 180.0f);

        public static void Update(GameState state)
        {
            // 玩家攻击
            for (int pi = 0; pi < state.Players.Length; ++pi)
            {
                var p = state.Players[pi];
                if (!p.Alive) continue;
                AttackOne(state, ref p, true);
                state.Players[pi] = p;
            }
            // 敌人攻击
            for (int ei = 0; ei < state.Enemies.Length; ++ei)
            {
                var e = state.Enemies[ei];
                if (!e.Alive) continue;
                AttackOne(state, ref e, false);
                state.Enemies[ei] = e;
            }
        }

        private static void AttackOne(GameState state, ref Entity a, bool isPlayer)
        {
            // 冷却自然流逝
            if (a.MeleeCooldown > 0) a.MeleeCooldown -= 1;
            if (a.BulletCooldown > 0) a.BulletCooldown -= 1;
            if (a.RayCooldown > 0) a.RayCooldown -= 1;

            bool wantMelee = a.PendingMelee, wantBullet = a.PendingBullet, wantRay = a.PendingRay;
            if (!wantMelee && !wantBullet && !wantRay) return;

            // 死亡实体不攻击
            if (a.Dead) {
                a.PendingMelee = a.PendingBullet = a.PendingRay = false;
                return;
            }

            var cd = state.Characters[isPlayer && a.CharId >= 0 && a.CharId < GameConst.CharCount
                ? a.CharId : GameConst.CharWarrior];
            long R = cd.MeleeRange;

            // ================= 近战 =================
            if (wantMelee && a.MeleeCooldown <= 0)
            {
                a.MeleeCooldown = isPlayer ? MELEE_COOLDOWN : ENEMY_MELEE_DPS_COOLDOWN;

                if (isPlayer)
                {
                    // 半圆 GJK3D 判定
                    long yawFp = Fp.FromFloat(a.Yaw);
                    long fx = Fp.Sin(yawFp);
                    long fz = Fp.Cos(yawFp);

                    const int SEMI_CENTER = 2;
                    const int SEMI_ARC = (SEMI_SEGS + 1) * 2;
                    const int SEMI_TOTAL = SEMI_CENTER + SEMI_ARC;
                    long[] verts = new long[SEMI_TOTAL * 3];
                    long yb = Fp.Sub(a.Y, Fp.FromFloat(0.5f));
                    long yt = Fp.Add(a.Y, Fp.FromFloat(1.2f));

                    int k = 0;
                    verts[k * 3 + 0] = a.X; verts[k * 3 + 1] = yb; verts[k * 3 + 2] = a.Z; k++;
                    verts[k * 3 + 0] = a.X; verts[k * 3 + 1] = yt; verts[k * 3 + 2] = a.Z; k++;
                    for (int i = 0; i <= SEMI_SEGS; ++i)
                    {
                        long t = Fp.Div(Fp.FromInt(i), Fp.FromInt(SEMI_SEGS));
                        long ang = Fp.Sub(yawFp, Fp.Mul(Fp.FromFloat(90.0f), DEG2R));
                        ang = Fp.Add(ang, Fp.Mul(Fp.FromFloat(180.0f), Fp.Mul(DEG2R, t)));
                        long px = Fp.Mul(R, Fp.Sin(ang));
                        long pz = Fp.Mul(R, Fp.Cos(ang));
                        verts[k * 3 + 0] = Fp.Add(a.X, px); verts[k * 3 + 1] = yb; verts[k * 3 + 2] = Fp.Add(a.Z, pz); k++;
                        verts[k * 3 + 0] = Fp.Add(a.X, px); verts[k * 3 + 1] = yt; verts[k * 3 + 2] = Fp.Add(a.Z, pz); k++;
                    }

                    int lockIdx = -1;
                    long lockDist = Fp.FromFloat(1e9f);
                    long dirFx = fx, dirFz = fz;
                    for (int ti = 0; ti < state.Enemies.Length; ++ti)
                    {
                        var tgt = state.Enemies[ti];
                        if (!tgt.Alive || tgt.Dead) continue;
                        long dx = Fp.Sub(tgt.X, a.X);
                        long dz = Fp.Sub(tgt.Z, a.Z);
                        long distSq = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                        if (CollisionExt.MeshBox(verts, SEMI_TOTAL,
                                tgt.X, tgt.Y, tgt.Z,
                                Fp.FromFloat(0.35f), Fp.FromFloat(0.9f), Fp.FromFloat(0.35f),
                                fx, 0, fz,
                                out _, out _, out _, out _))
                        {
                            if (distSq < lockDist) { lockIdx = ti; lockDist = distSq; }
                        }
                    }

                    if (lockIdx >= 0)
                    {
                        var tgt = state.Enemies[lockIdx];
                        long ldx = Fp.Sub(tgt.X, a.X);
                        long ldz = Fp.Sub(tgt.Z, a.Z);
                        long dist = Fp.Sqrt(Fp.Add(Fp.Mul(ldx, ldx), Fp.Mul(ldz, ldz)));
                        dirFx = Fp.Div(ldx, dist);
                        dirFz = Fp.Div(ldz, dist);
                        tgt.Hp -= cd.MeleeDamage;
                        if (tgt.Hp < 0) tgt.Hp = 0;
                        bool kill = false;
                        if (tgt.Hp <= 0 && !tgt.Dead) {
                            tgt.Dead = true;
                            state.Kills += 1;
                            kill = true;
                        }
                        if (!kill && dist > 0) {
                            tgt.KnockVx = Fp.Mul(dirFx, KNOCKBACK_SPEED);
                            tgt.KnockVz = Fp.Mul(dirFz, KNOCKBACK_SPEED);
                            tgt.KnockTicksLeft = KNOCKBACK_TICKS;
                        }
                        state.Enemies[lockIdx] = tgt;
                    }
                }
                else
                {
                    // 敌近战：attack_radius 内命中玩家
                    long range = a.AttackRadius;
                    long best = Fp.FromFloat(1e9f);
                    for (int ti = 0; ti < state.Players.Length; ++ti)
                    {
                        var tgt = state.Players[ti];
                        if (!tgt.Alive || tgt.Dead) continue;
                        if (tgt.SpawnInvuln > 0) continue;
                        long dx = Fp.Sub(tgt.X, a.X);
                        long dz = Fp.Sub(tgt.Z, a.Z);
                        long distSq = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                        if (distSq < best) best = distSq;
                        if (distSq < Fp.Mul(range, range))
                        {
                            int dmg = ENEMY_MELEE_DAMAGE;
                            if (a.BulletCooldown <= 0) {
                                dmg += ENEMY_POUNCE_DAMAGE;
                                a.BulletCooldown = ENEMY_POUNCE_COOLDOWN;
                            }
                            tgt.Hp -= dmg;
                            if (tgt.Hp < 0) tgt.Hp = 0;
                            long kdist = Fp.Sqrt(Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz)));
                            if (kdist > 0) {
                                tgt.KnockVx = Fp.Mul(Fp.Div(dx, kdist), KNOCKBACK_SPEED);
                                tgt.KnockVz = Fp.Mul(Fp.Div(dz, kdist), KNOCKBACK_SPEED);
                                tgt.KnockTicksLeft = KNOCKBACK_TICKS;
                            }
                            state.Players[ti] = tgt;
                            break;
                        }
                    }
                }
            }

            // ================= 射击 =================
            if (wantBullet && a.BulletCooldown <= 0)
            {
                a.BulletCooldown = BULLET_COOLDOWN;
                long sdx = 0, sdz = 0;

                if (isPlayer)
                {
                    int lockIdx = -1;
                    long best = Fp.FromFloat(1e9f);
                    for (int ti = 0; ti < state.Enemies.Length; ++ti)
                    {
                        var tgt = state.Enemies[ti];
                        if (!tgt.Alive || tgt.Dead) continue;
                        long dx = Fp.Sub(tgt.X, a.X), dz = Fp.Sub(tgt.Z, a.Z);
                        long d2 = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                        if (d2 < best) { best = d2; lockIdx = ti; }
                    }
                    if (lockIdx >= 0) {
                        var tgt = state.Enemies[lockIdx];
                        long dx = Fp.Sub(tgt.X, a.X), dz = Fp.Sub(tgt.Z, a.Z);
                        long dist = Fp.Sqrt(Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz)));
                        if (dist > 0) { sdx = Fp.Div(dx, dist); sdz = Fp.Div(dz, dist); }
                    }
                    if (sdx == 0 && sdz == 0) {
                        long yawFp = Fp.FromFloat(a.Yaw);
                        sdx = Fp.Sin(yawFp);
                        sdz = Fp.Cos(yawFp);
                    }
                }
                else
                {
                    int lockIdx = -1;
                    long best = Fp.FromFloat(1e9f);
                    for (int ti = 0; ti < state.Players.Length; ++ti)
                    {
                        var tgt = state.Players[ti];
                        if (!tgt.Alive || tgt.Dead || tgt.SpawnInvuln > 0) continue;
                        long dx = Fp.Sub(tgt.X, a.X), dz = Fp.Sub(tgt.Z, a.Z);
                        long d2 = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                        if (d2 < best) { best = d2; lockIdx = ti; }
                    }
                    if (lockIdx >= 0) {
                        var tgt = state.Players[lockIdx];
                        long dx = Fp.Sub(tgt.X, a.X), dz = Fp.Sub(tgt.Z, a.Z);
                        long dist = Fp.Sqrt(Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz)));
                        if (dist > 0) { sdx = Fp.Div(dx, dist); sdz = Fp.Div(dz, dist); }
                    }
                    if (sdx == 0 && sdz == 0) sdz = Fp.One();
                }

                if (state.BulletCount < state.Bullets.Length)
                {
                    state.Bullets[state.BulletCount++] = new Bullet {
                        Alive = true,
                        X = a.X, Y = a.Y, Z = a.Z,
                        DX = sdx, DZ = sdz,
                        Speed = BULLET_SPEED,
                        RangeLeft = BULLET_RANGE,
                        Damage = isPlayer ? cd.BulletDamage : ENEMY_BULLET_DAMAGE,
                        FromEnemy = !isPlayer,
                        OwnerId = a.Id,
                    };
                }
            }

            // ================= 射线（hitscan，仅玩家） =================
            if (isPlayer && wantRay && a.RayCooldown <= 0)
            {
                a.RayCooldown = RAY_COOLDOWN;
                long yawFp = Fp.FromFloat(a.Yaw);
                long fx = Fp.Sin(yawFp);
                long fz = Fp.Cos(yawFp);
                long RAY_RANGE = cd.RayRange;

                int lockIdx = -1;
                long lockT = Fp.FromFloat(1e9f);
                long lockX = 0, lockZ = 0;
                long bestR2 = Fp.Mul(RAY_RANGE, RAY_RANGE);
                for (int ti = 0; ti < state.Enemies.Length; ++ti)
                {
                    var tgt = state.Enemies[ti];
                    if (!tgt.Alive || tgt.Dead) continue;
                    long dx = Fp.Sub(tgt.X, a.X), dz = Fp.Sub(tgt.Z, a.Z);
                    long r2 = Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz));
                    if (r2 < bestR2) { bestR2 = r2; lockIdx = ti; lockX = tgt.X; lockZ = tgt.Z; }
                }
                if (lockIdx >= 0) {
                    long dx = Fp.Sub(lockX, a.X), dz = Fp.Sub(lockZ, a.Z);
                    long len = Fp.Sqrt(Fp.Add(Fp.Mul(dx, dx), Fp.Mul(dz, dz)));
                    if (len > 0) { fx = Fp.Div(dx, len); fz = Fp.Div(dz, len); }
                    lockT = len;
                }

                // 障碍阻挡：取射线方向最近命中障碍距离
                long beamReach = RAY_RANGE;
                for (int oi = 0; oi < state.ObstacleCount; ++oi)
                {
                    var o = state.Obstacles[oi];
                    if (CollisionExt.RayObb(a.X, a.Y, a.Z, fx, 0, fz,
                            o.X, o.Y, o.Z, o.Hx, o.Y, o.Hz,
                            0, 0, 0, out long dist))
                    {
                        if (dist > 0 && dist < beamReach) beamReach = dist;
                    }
                }

                if (lockIdx >= 0 && lockT >= beamReach) lockIdx = -1;

                if (lockIdx >= 0)
                {
                    var tgt = state.Enemies[lockIdx];
                    tgt.Hp -= cd.RayDamage;
                    if (tgt.Hp < 0) tgt.Hp = 0;
                    bool kill = false;
                    if (tgt.Hp <= 0 && !tgt.Dead) { tgt.Dead = true; state.Kills += 1; kill = true; }
                    if (!kill) {
                        tgt.KnockVx = Fp.Mul(fx, KNOCKBACK_SPEED);
                        tgt.KnockVz = Fp.Mul(fz, KNOCKBACK_SPEED);
                        tgt.KnockTicksLeft = KNOCKBACK_TICKS;
                    }
                    state.Enemies[lockIdx] = tgt;
                }
            }

            a.PendingMelee = a.PendingBullet = a.PendingRay = false;
        }
    }
}
