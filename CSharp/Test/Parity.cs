using System;
using ProxyGame;
using Joy.Api;

namespace JoyGame
{
    /// <summary>
    /// 帧同步快照自检（C# 侧）。由 GameApplication.Initialize 在 PROXY_PARITY=1 时触发。
    /// 构造确定场景与输入序列，跑 30 tick，按固定格式打印玩家状态（fp 原始 long），
    /// 供回归比对。原 C++/C# 逐位对照基准已移除，本脚本保留为独立快照自检。
    /// </summary>
    public static class Parity
    {
        private const byte INPUT_UP = 1, INPUT_DOWN = 2, INPUT_LEFT = 4, INPUT_RIGHT = 8;

        public static void Run()
        {
            // r3d 绑定自检：确认 7 个手写 InternalCall（含新补的 R3dCreate）已注册且空句柄安全
            string r3dDetail;
            bool r3dOk = R3dExt.SelfTest(out r3dDetail);
            Joy.Host.log("[r3dselftest] " + (r3dOk ? "PASS " : "FAIL ") + r3dDetail);

            // 地图解析对照：与 C++ harness 的 parity_maps 同格式
            for (int lv = 1; lv <= MapTables.LevelCount; ++lv) {
                var ll = MapData.BuildLayout(lv);
                var L = ll.Layout;
                Joy.Host.log(string.Format("MAP {0} ok={1} count={2} seed={3} player={4} enemy_n={5}",
                    lv, ll.Valid ? 1 : 0, L.Count, L.Seed, ll.PlayerCell, ll.EnemyN));
                int kmax = L.Count < 4 ? L.Count : 4;
                for (int k = 0; k < kmax; ++k) {
                    var o = L.Obs[k];
                    Joy.Host.log(string.Format("OBS {0} {1} {2} {3} {4} {5} {6} {7} {8} {9}",
                        lv, o.X10, o.Z10, o.Hx10, o.Hz10, o.Sy10, o.Gx0, o.Gz0, o.Gw, o.Gh));
                }
                int wsum = 0, wcnt = 0;
                for (int cell = 0; cell < MapConst.Cells; ++cell) {
                    if (L.Walkable[cell]) wsum += cell;
                    if (!L.Walkable[cell]) wcnt++;
                }
                Joy.Host.log(string.Format("WALK {0} sum={1} blocked={2}", lv, wsum, wcnt));
                for (int ei = 0; ei < ll.EnemyN; ++ei)
                    Joy.Host.log(string.Format("ECELL {0} {1}", lv, ll.EnemyCells[ei]));
            }

            // 敌人生成验证（level 1）：与 C++ harness 的 parity_wave 同格式
            {
                var spawnState = new GameState { Level = 1, MapSeed = 1, Wave = 0 };
                int got = ProxyGame.Systems.WaveSpawnSystem.SpawnLevel(spawnState, 1);
                Joy.Host.log("WAVE 1 got=" + got);
                for (int k = 0; k < got; ++k) {
                    var e = spawnState.Enemies[k];
                    Joy.Host.log(string.Format(
                        "ENEMY 1 {0} {1} {2} {3} {4} {5} {6} {7}",
                        e.SpawnIdx, e.X, e.Z, e.Hp, e.PatrolRadius, e.DetectRadius,
                        e.AttackRadius, e.GuardRadius));
                }
            }

            // A* 寻路验证（level 1）：障碍 → walkable → 几组起终点路径
            {
                var pfState = new GameState { Level = 1, MapSeed = 1 };
                var layout = MapData.BuildLayout(1).Layout;
                int nextId = 10000;
                ProxyGame.Systems.Pathfinding.BuildObstaclesFromLayout(pfState, layout, ref nextId);
                var walk = ProxyGame.Systems.Pathfinding.BuildWalkable(pfState);
                int wsum = 0, wcnt = 0;
                for (int c = 0; c < MapConst.Cells; ++c) {
                    if (walk[c]) wsum += c;
                    if (!walk[c]) wcnt++;
                }
                Joy.Host.log(string.Format("PFWALK 1 obs={0} sum={1} blocked={2}",
                    pfState.ObstacleCount, wsum, wcnt));

                // 用玩家出生格(=8,7) → 各敌人出生格（地图保证连通），验证真实寻路
                var ll2 = MapData.BuildLayout(1);
                var caseList = new System.Collections.Generic.List<int[]>();
                int psx = ll2.PlayerCell % MapConst.GridN, psz = ll2.PlayerCell / MapConst.GridN;
                for (int ei2 = 0; ei2 < ll2.EnemyN; ++ei2) {
                    caseList.Add(new[] { psx, psz,
                        ll2.EnemyCells[ei2] % MapConst.GridN, ll2.EnemyCells[ei2] / MapConst.GridN });
                }
                caseList.Add(new[] { psx, psz, psx, psz });  // 同点
                // 起点房间内确定连通的格（验证真实寻路，非死区）
                caseList.Add(new[] { psx, psz, psx + 4, psz + 1 });
                caseList.Add(new[] { psx + 4, psz + 1, psx, psz });
                var cases = caseList.ToArray();
                foreach (var cs in cases) {
                    int len = ProxyGame.Systems.Pathfinding.FindPath(walk, cs[0], cs[1], cs[2], cs[3], out int[] path);
                    int psum = 0;
                    for (int k = 0; k < len; ++k) psum = unchecked(psum * 31 + path[k]);
                    Joy.Host.log(string.Format("PF {0} {1} {2} {3} len={4} sum={5}",
                        cs[0], cs[1], cs[2], cs[3], len, psum));
                }
            }

            // AI 状态机验证：玩家 + 8 敌人（wave 位置），跑 5 tick
            {
                var aiState = new GameState { Level = 1, MapSeed = 1 };
                var ll3 = MapData.BuildLayout(1);
                int psx = ll3.PlayerCell % MapConst.GridN, psz = ll3.PlayerCell / MapConst.GridN;
                var pl = new Entity {
                    Id = 1, Kind = EntityKind.Player, Alive = true,
                    X = MapData.GridToWorld(psx), Y = Fp.FromFloat(0.6f), Z = MapData.GridToWorld(psz),
                };
                aiState.Players[0] = pl;

                for (int i2 = 0; i2 < 8; ++i2) {
                    int tile = ll3.EnemyCells[i2 % ll3.EnemyN];
                    int si = (i2 < SpawnDefs.EnemySpawnCount) ? i2
                        : (1 + 0 * 7 + i2 * 13) % SpawnDefs.EnemySpawnCount;
                    var sd = SpawnDefs.EnemySpawns[si];
                    int gx = tile % MapConst.GridN, gz = tile / MapConst.GridN;
                    aiState.Enemies[i2] = new Entity {
                        Id = 2 + i2, Kind = EntityKind.Enemy, Alive = true,
                        X = MapData.GridToWorld(gx), Z = MapData.GridToWorld(gz),
                        Hp = GameConst.EnemyMaxHp, MaxHp = GameConst.EnemyMaxHp,
                        PatrolCenterX = MapData.GridToWorld(gx), PatrolCenterZ = MapData.GridToWorld(gz),
                        TargetX = MapData.GridToWorld(gx), TargetZ = MapData.GridToWorld(gz),
                        PatrolRadius = Fp.FromFloat(sd.PatrolRadius),
                        DetectRadius = Fp.FromFloat(sd.DetectRadius),
                        AttackRadius = Fp.FromFloat(sd.AttackRadius),
                        GuardRadius = Fp.FromFloat(sd.GuardRadius),
                    };
                }

                for (int t2 = 0; t2 < 5; ++t2) {
                    // 模拟上一 tick melee_attack 消费并清空事件
                    for (int k2 = 0; k2 < aiState.Enemies.Length; ++k2) {
                        var en2 = aiState.Enemies[k2];
                        en2.PendingMelee = en2.PendingBullet = en2.PendingRay = false;
                        aiState.Enemies[k2] = en2;
                    }
                    ProxyGame.Systems.AiSystem.Update(aiState);
                    for (int k2 = 0; k2 < 8; ++k2) {
                        var en2 = aiState.Enemies[k2];
                        Joy.Host.log(string.Format(
                            "AI {0} {1} state={2} act={3} tgx={4} tgz={5} dirty={6} melee={7}",
                            t2, en2.Id, en2.AiState, en2.ActionState,
                            en2.PathTargetGx, en2.PathTargetGz,
                            en2.PathDirty ? 1 : 0, en2.PendingMelee ? 1 : 0));
                    }
                }
            }

            // 碰撞分离验证：玩家 vs 障碍（真实 GJK），两个位置各跑 3 轮
            {
                var colState = new GameState { Level = 1, MapSeed = 1 };
                var llc = MapData.BuildLayout(1);
                int nextIdc = 10000;
                ProxyGame.Systems.Pathfinding.BuildObstaclesFromLayout(colState, llc.Layout, ref nextIdc);

                int pcx = llc.PlayerCell % MapConst.GridN, pcz = llc.PlayerCell / MapConst.GridN;
                long spawnX = MapData.GridToWorld(pcx), spawnZ = MapData.GridToWorld(pcz);
                long wallX = Fp.Div(Fp.FromInt(llc.Layout.Obs[0].X10), Fp.FromInt(10));
                long wallZ = Fp.Div(Fp.FromInt(llc.Layout.Obs[0].Z10), Fp.FromInt(10));

                long[] caseX = { spawnX, wallX, 0 };
                long[] caseZ = { spawnZ, wallZ, Fp.FromFloat(-25.9f) };
                string[] caseName = { "spawn", "wall", "edge" };

                for (int ci = 0; ci < 3; ++ci) {
                    var st = new GameState { Level = 1, MapSeed = 1 };
                    st.ObstacleCount = colState.ObstacleCount;
                    Array.Copy(colState.Obstacles, st.Obstacles, colState.ObstacleCount);
                    st.Players[0] = new Entity {
                        Id = 1, Kind = EntityKind.Player, Alive = true,
                        X = caseX[ci], Y = Fp.FromFloat(0.6f), Z = caseZ[ci],
                        ColliderType = ColliderType.Box,
                        ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                    };
                    for (int it = 0; it < 3; ++it) {
                        ProxyGame.Systems.CollisionSystem.Update(st);
                        var pl = st.Players[0];
                        Joy.Host.log(string.Format("COL {0} {1} {2} {3}",
                            caseName[ci], it, pl.X, pl.Z));
                    }
                }
            }

            // 攻击结算验证：近战 / 射击 / 敌人近战 / 射线 四轮
            {
                var ms = new GameState { Level = 1, MapSeed = 1 };
                var llm = MapData.BuildLayout(1);
                int nid = 10000;
                ProxyGame.Systems.Pathfinding.BuildObstaclesFromLayout(ms, llm.Layout, ref nid);

                ms.Players[0] = new Entity {
                    Id = 1, Kind = EntityKind.Player, Alive = true, CharId = GameConst.CharWarrior,
                    X = 0, Y = Fp.FromFloat(0.6f), Z = 0,
                    Hp = GameConst.PlayerMaxHp, MaxHp = GameConst.PlayerMaxHp,
                    ColliderType = ColliderType.Box,
                    ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                };
                Entity MakeEnemy(int id, float ex, float ez) => new Entity {
                    Id = id, Kind = EntityKind.Enemy, Alive = true,
                    X = Fp.FromFloat(ex), Z = Fp.FromFloat(ez),
                    Hp = GameConst.EnemyMaxHp, MaxHp = GameConst.EnemyMaxHp,
                    ColliderType = ColliderType.Box,
                    ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                    PatrolCenterX = Fp.FromFloat(ex), PatrolCenterZ = Fp.FromFloat(ez),
                    DetectRadius = Fp.FromFloat(400f), AttackRadius = Fp.FromFloat(2f),
                    PatrolRadius = Fp.FromFloat(3f),
                };
                ms.Enemies[0] = MakeEnemy(2, 0.0f, 1.0f);
                ms.Enemies[1] = MakeEnemy(3, 0.0f, 3.0f);

                void Dump(string tag, int round) {
                    var pl = ms.Players[0];
                    var e1 = ms.Enemies[0];
                    var e2 = ms.Enemies[1];
                    Joy.Host.log(string.Format(
                        "ML {0} {1} php={2} e1hp={3} e2hp={4} e1dead={5} e2dead={6} kills={7} bullets={8}",
                        tag, round, pl.Hp, e1.Hp, e2.Hp,
                        e1.Dead ? 1 : 0, e2.Dead ? 1 : 0, ms.Kills, ms.BulletCount));
                }

                // 轮0：玩家近战
                var p0 = ms.Players[0]; p0.PendingMelee = true; ms.Players[0] = p0;
                ProxyGame.Systems.MeleeAttackSystem.Update(ms);
                Dump("melee", 0);

                // 轮1：玩家射击
                var p1 = ms.Players[0]; p1.PendingBullet = true; ms.Players[0] = p1;
                ProxyGame.Systems.MeleeAttackSystem.Update(ms);
                Dump("shoot", 1);

                // 轮2：敌人1 近战
                var en1 = ms.Enemies[0]; en1.PendingMelee = true; ms.Enemies[0] = en1;
                ProxyGame.Systems.MeleeAttackSystem.Update(ms);
                Dump("enemy", 2);

                // 轮3：玩家射线
                var p3 = ms.Players[0]; p3.PendingRay = true; ms.Players[0] = p3;
                ProxyGame.Systems.MeleeAttackSystem.Update(ms);
                Dump("ray", 3);
            }

            // 子弹系统验证：手动创建一颗 +Z 子弹，跑 10 tick
            {
                var bs = new GameState { Level = 1, MapSeed = 1 };
                var llb = MapData.BuildLayout(1);
                int nidb = 10000;
                ProxyGame.Systems.Pathfinding.BuildObstaclesFromLayout(bs, llb.Layout, ref nidb);

                bs.Players[0] = new Entity {
                    Id = 1, Kind = EntityKind.Player, Alive = true,
                    X = 0, Y = Fp.FromFloat(0.6f), Z = 0,
                    Hp = GameConst.PlayerMaxHp, MaxHp = GameConst.PlayerMaxHp,
                    ColliderType = ColliderType.Box,
                    ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                };
                bs.Enemies[0] = new Entity {
                    Id = 2, Kind = EntityKind.Enemy, Alive = true,
                    X = 0, Y = 0, Z = Fp.FromFloat(1.0f),
                    Hp = GameConst.EnemyMaxHp, MaxHp = GameConst.EnemyMaxHp,
                    ColliderType = ColliderType.Box,
                    ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                };
                bs.Bullets[0] = new Bullet {
                    Alive = true, X = 0, Y = Fp.FromFloat(0.6f), Z = 0,
                    DX = 0, DZ = Fp.One(),
                    Speed = Fp.FromFloat(0.4f), RangeLeft = Fp.FromFloat(12.0f),
                    Damage = 8, FromEnemy = false, OwnerId = 1,
                };
                bs.BulletCount = 1;

                for (int bt = 0; bt < 10; ++bt) {
                    ProxyGame.Systems.BulletSystem.Update(bs);
                    var en = bs.Enemies[0];
                    var pl = bs.Players[0];
                    Joy.Host.log(string.Format("BL {0} bullets={1} ehp={2} php={3} kills={4}",
                        bt, bs.BulletCount, en.Hp, pl.Hp, bs.Kills));
                }
            }

            // Wave 流程验证：开局建关 → 有敌人存活 → 部分击杀 → 全灭清场 → 稳定
            {
                var ws = new GameState { Level = 1 };
                ws.Players[0] = new Entity {
                    Id = 1, Kind = EntityKind.Player, Alive = true,
                    X = 0, Y = Fp.FromFloat(0.6f), Z = 0,
                    Hp = GameConst.PlayerMaxHp, MaxHp = GameConst.PlayerMaxHp,
                };

                void DumpW(string tag) {
                    int alive = 0;
                    for (int k = 0; k < ws.Enemies.Length; ++k) {
                        var e = ws.Enemies[k];
                        if (e.Kind == EntityKind.Enemy && e.Alive && !e.Dead) alive++;
                    }
                    Joy.Host.log(string.Format("WAVECLR {0} wave={1} cleared={2} alive={3} seed={4}",
                        tag, ws.Wave, ws.LevelCleared ? 1 : 0, alive, ws.MapSeed));
                }

                DumpW("init");
                ProxyGame.Systems.WaveSystem.Update(ws);
                DumpW("start");
                ProxyGame.Systems.WaveSystem.Update(ws);
                DumpW("idle");
                for (int k = 0; k < 3; ++k) {
                    var e = ws.Enemies[k]; e.Dead = true; ws.Enemies[k] = e;
                }
                ProxyGame.Systems.WaveSystem.Update(ws);
                DumpW("partial");
                for (int k = 0; k < ws.Enemies.Length; ++k) {
                    var e = ws.Enemies[k];
                    if (e.Kind == EntityKind.Enemy && e.Alive) { e.Dead = true; ws.Enemies[k] = e; }
                }
                ProxyGame.Systems.WaveSystem.Update(ws);
                DumpW("clear");
                ProxyGame.Systems.WaveSystem.Update(ws);
                DumpW("stable");
            }

            // 网络输入消费验证：join×2 → 去重 → input(移动/跳跃/攻击) → leave → rejoin
            {
                var ns = new GameState();
                ns.Net.FrameId = 0;

                void DumpN(string phase) {
                    int alive = 0;
                    for (int k = 0; k < ns.Players.Length; ++k) if (ns.Players[k].Alive) alive++;
                    Joy.Host.log(string.Format("IC {0} n={1} cam={2}", phase, alive, ns.CameraFollowConv));
                    var list = new System.Collections.Generic.List<Entity>();
                    for (int k = 0; k < ns.Players.Length; ++k) if (ns.Players[k].Alive) list.Add(ns.Players[k]);
                    list.Sort((a, b) => a.Id.CompareTo(b.Id));
                    foreach (var pu in list) {
                        Joy.Host.log(string.Format(
                            "ICJ {0} {1} conv={2} char={3} x={4} y={5} z={6} hp={7} max={8} move={9} yaw={10} jp={11} jreq={12} atk={13}{14}{15}",
                            phase, pu.Id, pu.Conv, pu.CharId, pu.X, pu.Y, pu.Z, pu.Hp, pu.MaxHp,
                            pu.MoveBits, pu.YawQuantized, pu.JumpPrev ? 1 : 0, pu.JumpRequested ? 1 : 0,
                            pu.PendingMelee ? 1 : 0, pu.PendingBullet ? 1 : 0, pu.PendingRay ? 1 : 0));
                    }
                }
                void ClearTransient() {
                    for (int k = 0; k < ns.Players.Length; ++k) {
                        var pu = ns.Players[k];
                        pu.JumpRequested = false;
                        pu.PendingMelee = pu.PendingBullet = pu.PendingRay = false;
                        ns.Players[k] = pu;
                    }
                }

                // 阶段1：两个 join（战士 conv=11、射手 conv=22）
                ns.Net.Joins[0] = new NetJoin { Conv = 11, X = Fp.FromFloat(3.2f), Y = Fp.FromFloat(0.6f), Z = Fp.FromFloat(2.3f), CharId = 0, Nickname = "A" };
                ns.Net.Joins[1] = new NetJoin { Conv = 22, X = Fp.FromFloat(-1.5f), Y = Fp.FromFloat(0.6f), Z = Fp.FromFloat(4.0f), CharId = 1, Nickname = "B" };
                ns.Net.NumJoins = 2;
                ProxyGame.Systems.InputConsumeJoinSystem.Update(ns);
                DumpN("join2");
                ClearTransient();

                // 阶段2：重复 join conv=11（去重）
                ns.Net.Joins[0] = new NetJoin { Conv = 11, X = Fp.FromFloat(9.9f), Y = 0, Z = Fp.FromFloat(9.9f), CharId = 2, Nickname = "X" };
                ns.Net.NumJoins = 1;
                ProxyGame.Systems.InputConsumeJoinSystem.Update(ns);
                DumpN("dup");

                // 阶段3：input（conv=11 上+跳 上升沿；conv=22 右+射击）
                ns.Net.Inputs[0] = new NetInput { ClientSeq = 1, Conv = 11, Dir = GameConst.InputUp | GameConst.InputJump, Attack = 0, YawQuantized = 100 };
                ns.Net.Inputs[1] = new NetInput { ClientSeq = 2, Conv = 22, Dir = GameConst.InputRight, Attack = GameConst.AttackBullet, YawQuantized = -200 };
                ns.Net.NumInputs = 2;
                ProxyGame.Systems.InputConsumeInputSystem.Update(ns);
                DumpN("input1");

                // 阶段4：清瞬态后 conv=11 仍按跳（不连跳）；conv=22 近战
                ClearTransient();
                ns.Net.Inputs[0] = new NetInput { ClientSeq = 3, Conv = 11, Dir = GameConst.InputJump, Attack = 0, YawQuantized = 100 };
                ns.Net.Inputs[1] = new NetInput { ClientSeq = 4, Conv = 22, Dir = 0, Attack = GameConst.AttackMelee, YawQuantized = -200 };
                ns.Net.NumInputs = 2;
                ProxyGame.Systems.InputConsumeInputSystem.Update(ns);
                DumpN("input2");

                // 阶段5：leave conv=11
                ClearTransient();
                ns.Net.Leaves[0] = new NetLeave { Conv = 11 };
                ns.Net.NumLeaves = 1;
                ProxyGame.Systems.InputConsumeLeaveSystem.Update(ns);
                DumpN("leave");

                // 阶段6：join conv=33（刺客）填补空槽
                ns.Net.Joins[0] = new NetJoin { Conv = 33, X = Fp.FromFloat(0.1f), Y = Fp.FromFloat(0.6f), Z = Fp.FromFloat(0.2f), CharId = 2, Nickname = "C" };
                ns.Net.NumJoins = 1;
                ProxyGame.Systems.InputConsumeJoinSystem.Update(ns);
                DumpN("rejoin");
            }

            // 协议/快照字节对照：wave_seed + pack_entity + pack_world（与 C++ 真实 msgpack-c 逐字节对照）
            {
                uint[] bases = { 0, 1, 12345, 0xFFFFFFF0u };
                int[] waves = { 0, 1, 3, 7 };
                for (int k = 0; k < 4; ++k)
                    Joy.Host.log(string.Format("PMSEED {0} {1} {2}",
                        bases[k], waves[k], ProxyGame.Procmap.WaveSeed(bases[k], waves[k])));

                var e1 = new S2CEntity().InitZero();
                e1.Id = 7; e1.Type = S2CType.Enemy;
                e1.PositionX = Fp.FromFloat(3.2f); e1.PositionY = Fp.FromFloat(0.6f); e1.PositionZ = Fp.FromFloat(2.3f);
                e1.Width = Fp.FromFloat(0.5f); e1.Height = Fp.FromFloat(1.0f); e1.Hp = 40;
                e1.YawQuantized = -200; e1.AiType = 1; e1.CharId = 2; e1.Nickname = "Enemy";
                e1.AiTimer = 5; e1.AiState = 1;
                e1.AiTargetX = Fp.FromFloat(1.5f); e1.AiTargetZ = Fp.FromFloat(-2.5f);
                e1.PfTargetGx = 12; e1.PfTargetGz = 9; e1.PfPathLen = 3; e1.PfNextIdx = 1; e1.PfRecomputeCd = 2;
                var w1 = new ProxyGame.Systems.MsgpackWriter();
                ProxyGame.Systems.SnapshotSystem.PackEntity(w1, e1);
                Joy.Host.log("MPENT " + w1.Hex());

                var p1 = new S2CEntity().InitZero();
                p1.Id = 1; p1.Type = S2CType.Player;
                p1.PositionX = Fp.FromFloat(3.2f); p1.PositionY = Fp.FromFloat(0.6f); p1.PositionZ = Fp.FromFloat(2.3f);
                p1.Width = Fp.FromInt(1); p1.Height = Fp.FromInt(1); p1.Hp = 100;
                p1.PlayerConv = 11; p1.YawQuantized = 100; p1.Nickname = "Hero";
                var e2 = new S2CEntity().InitZero();
                e2.Id = 2; e2.Type = S2CType.Enemy;
                e2.PositionX = Fp.FromFloat(1.0f); e2.PositionZ = Fp.FromFloat(2.0f);
                e2.Width = Fp.FromFloat(0.5f); e2.Height = Fp.FromFloat(1.0f); e2.Hp = 40;
                e2.AiType = 1; e2.CharId = 1; e2.Nickname = "E";
                var arr = new S2CEntity[] { p1, e2 };
                Joy.Host.log("MPWORLD " + ProxyGame.Systems.SnapshotSystem.PackWorld(10000, arr, 2));
            }

            var state = new GameState();
            int i = 0;
            var p = state.Players[i];
            p.Id = 1;
            p.Kind = EntityKind.Player;
            p.Alive = true;
            p.CharId = GameConst.CharWarrior;
            p.X = Fp.FromFloat(3.2f);
            p.Y = Fp.FromFloat(0.6f);
            p.Z = Fp.FromFloat(2.3f);
            p.SpawnX = Fp.FromFloat(3.2f);
            p.SpawnY = Fp.FromFloat(0.6f);
            p.SpawnZ = Fp.FromFloat(2.3f);
            p.Hp = GameConst.PlayerMaxHp;       // C++ harness 用 PLAYER_MAX_HP=100（非角色表 150）
            p.MaxHp = GameConst.PlayerMaxHp;
            p.OnGround = true;
            state.Players[i] = p;

            for (int t = 0; t < 30; ++t)
            {
                p = state.Players[i];
                p.MoveBits = 0;
                p.JumpRequested = false;
                if (t < 10) p.MoveBits |= INPUT_UP;
                else if (t < 20) p.MoveBits |= INPUT_RIGHT;
                else { p.MoveBits |= INPUT_UP; p.JumpRequested = true; }
                // t==5 触发一次击退（沿 +X，6 tick；与 C++ harness 同步）
                if (t == 5) {
                    p.KnockVx = Fp.FromFloat(0.08f);
                    p.KnockVz = Fp.FromFloat(0.04f);
                    p.KnockTicksLeft = 6;
                }
                state.Players[i] = p;

                ProxyGame.Systems.PlayerDeathSystem.Update(state);
                ProxyGame.Systems.MovementSystem.Update(state);
                ProxyGame.Systems.KnockbackSystem.Update(state);
                ProxyGame.Systems.CleanupSystem.Update(state);

                p = state.Players[i];
                Joy.Host.log(string.Format("TICK {0} P1 {1} {2} {3} {4} {5} {6} {7}",
                    t, p.X, p.Y, p.Z, p.Hp, p.Dead ? 1 : 0, p.OnGround ? 1 : 0, p.VerticalVelocity));
                // cleanup 后的输入状态（应为 0，验证帧尾清理）
                Joy.Host.log(string.Format("INPUT {0} {1} {2}",
                    p.MoveBits, p.YawQuantized, p.PitchQuantized));
                // checksum 拼接串（与 C++ checksum_entries_string 同格式，用于 diff 公式一致性）
                Joy.Host.log("ENTRIES " + Checksum.EntriesString(state));
            }
        }
    }
}
