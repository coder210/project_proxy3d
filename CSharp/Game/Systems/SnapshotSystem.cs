using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 逐位复刻 systems/snapshot_system.h：从当前实体状态生成世界快照 S2CEntity 列表，
    /// 并按 shared/protocol.h 的 pack_world 编码为 msgpack。
    /// 遍历顺序：玩家数组 → 敌人数组 → 障碍数组（C++ entt view 遍历顺序不确定，
    /// 但 pack_world 是针对同一份实体集合；parity 场景两端用同一构造顺序对齐）。
    /// 死亡敌人跳过（对象池冻结），玩家不跳过。
    /// </summary>
    public static class SnapshotSystem
    {
        /// <summary>生成世界快照实体列表（对应 snapshot_system 的实体遍历+填充）。</summary>
        public static int BuildEntities(GameState state, S2CEntity[] outEntities, int max)
        {
            int ec = 0;

            // 玩家：type=PLAYER，填 conv/char_id/nickname，血量
            for (int i = 0; i < state.Players.Length && ec < max; ++i)
            {
                var p = state.Players[i];
                if (!p.Alive) continue;
                var e = new S2CEntity().InitZero();
                e.Id = p.Id;
                e.Type = S2CType.Player;
                e.Width = Fp.FromFloat(1f); e.Height = Fp.FromFloat(1f); // LogicRect(1,1) join 时设定
                e.PositionX = p.X; e.PositionY = p.Y; e.PositionZ = p.Z;
                e.YawQuantized = (short)(p.Yaw / 3.14159265f * 32767.0f);
                e.Hp = p.Hp;
                e.PlayerConv = p.Conv;
                e.CharId = p.CharId;
                e.Nickname = p.Nickname ?? string.Empty;
                outEntities[ec++] = e;
            }

            // 敌人：跳过 dead；填 SpawnIdx→ai_type、AI 运行时、Path 状态
            for (int i = 0; i < state.Enemies.Length && ec < max; ++i)
            {
                var en = state.Enemies[i];
                if (!en.Alive || en.Dead) continue;
                var e = new S2CEntity().InitZero();
                e.Id = en.Id;
                e.Type = S2CType.Enemy;
                e.Width = Fp.FromFloat(0.5f); e.Height = Fp.FromFloat(1.0f); // enemy_reset LogicRect(0.5,1.0)
                e.PositionX = en.X; e.PositionY = en.Y; e.PositionZ = en.Z;
                e.YawQuantized = (short)(en.Yaw / 3.14159265f * 32767.0f);
                e.Hp = en.Hp;
                e.AiType = en.SpawnIdx;
                e.AiTimer = en.AiTimer;
                e.AiState = en.AiState;
                e.AiTargetX = en.TargetX; e.AiTargetZ = en.TargetZ;
                e.PfTargetGx = en.PathTargetGx; e.PfTargetGz = en.PathTargetGz;
                e.PfPathLen = en.PathLen; e.PfNextIdx = en.NextIdx; e.PfRecomputeCd = en.RecomputeCooldown;
                outEntities[ec++] = e;
            }

            // 障碍：type=PROP，XZ 半尺寸×2 = 宽/深，Sy 全高
            for (int i = 0; i < state.ObstacleCount && ec < max; ++i)
            {
                var o = state.Obstacles[i];
                var e = new S2CEntity().InitZero();
                e.Id = o.Id;
                e.Type = S2CType.Prop;
                e.Width = Fp.Mul(o.Hx, Fp.FromInt(2));
                e.Height = Fp.Mul(o.Hz, Fp.FromInt(2));
                e.PositionX = o.X; e.PositionY = o.Y; e.PositionZ = o.Z;
                e.Hp = o.Sy;
                outEntities[ec++] = e;
            }

            return ec;
        }

        /// <summary>pack_entity：22 项数组（对应 protocol.h pack_entity）。</summary>
        public static void PackEntity(MsgpackWriter w, S2CEntity e)
        {
            w.PackArray(22);
            w.PackInt(e.Id); w.PackInt(e.Type);
            w.PackInt64(e.PositionX); w.PackInt64(e.PositionY); w.PackInt64(e.PositionZ);
            w.PackInt64(e.Width); w.PackInt64(e.Height);
            w.PackInt64(e.Hp); w.PackInt(e.PlayerConv);
            w.PackInt(e.YawQuantized);
            w.PackInt64(e.AiType);
            w.PackInt(e.CharId);
            w.PackStr(e.Nickname ?? string.Empty);
            w.PackInt(e.AiTimer); w.PackInt(e.AiState);
            w.PackInt64(e.AiTargetX); w.PackInt64(e.AiTargetZ);
            w.PackInt(e.PfTargetGx); w.PackInt(e.PfTargetGz);
            w.PackInt(e.PfPathLen); w.PackInt(e.PfNextIdx);
            w.PackInt(e.PfRecomputeCd);
        }

        /// <summary>pack_world：[world_eid, [entity...]]（对应 protocol.h pack_world）。</summary>
        public static string PackWorld(int worldEid, S2CEntity[] entities, int count)
        {
            var w = new MsgpackWriter();
            w.PackArray(2);
            w.PackInt(worldEid);
            w.PackArray(count);
            for (int i = 0; i < count; ++i) PackEntity(w, entities[i]);
            return w.Hex();
        }
    }
}
