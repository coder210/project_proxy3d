using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 复刻 shared/systems/wave_system.h 的敌人生成部分（手绘地图路径）：
    ///   wave_prepare_enemies → 决定每只敌人的出生格 tile 与 spawn 类型索引 si
    ///   wave_spawn_enemy_range → 计算世界坐标并重置敌人实体
    ///   enemy_reset → 写入位置/血量/AI 参数/SpawnIdx
    /// 兜底的程序化随机采样路径（enemy_n==0）当前未迁（手绘关卡都有 E 格）。
    /// 纯确定性：出生格来自地图解析，坐标用定点 pf_grid_to_world。
    /// </summary>
    public static class WaveSpawnSystem
    {
        // 调试开关：限制每关最多生成 1 个敌人（原版每关 8 个）。
        // 设为 0 可完全关闭敌人生成；改回 64 恢复原版行为。
        public const int DebugMaxEnemies = 1;

        /// <summary>准备整波敌人的出生格与类型索引（不创建实体），返回生成数量。</summary>
        public static int PrepareEnemies(GameState state, int level, int[] tiles, int[] si)
        {
            int count = (level >= 1 && level <= MapTables.LevelCount)
                ? MapConst.LevelEnemyCount[level - 1] : 2;
            if (count < SpawnDefs.EnemySpawnCount) count = SpawnDefs.EnemySpawnCount;
            if (count > MapConst.WaveMaxEnemies) count = MapConst.WaveMaxEnemies;

            var ll = MapData.BuildLayout(level);
            int got = 0;
            if (ll.EnemyN > 0)
            {
                // 前 ENEMY_SPAWN_COUNT 只覆盖所有类型；多余名额用 seed+wave+序号确定性补齐；
                // E 格不足时循环复用同一批出生点。
                for (int i = 0; i < count; ++i)
                {
                    tiles[got++] = ll.EnemyCells[i % ll.EnemyN];
                    si[i] = (i < SpawnDefs.EnemySpawnCount)
                        ? i
                        : (state.MapSeed + state.Wave * 7 + i * 13) % SpawnDefs.EnemySpawnCount;
                }
            }
            return got;
        }

        /// <summary>把敌人实体重置为"刚出生"状态（复刻 enemy_reset 的逻辑字段部分）。</summary>
        private static void SpawnEnemy(GameState state, int slot, int tile, int si)
        {
            SpawnDef sd = SpawnDefs.EnemySpawns[si];
            int gx = tile % MapConst.GridN;
            int gz = tile / MapConst.GridN;
            // 出生点世界坐标 = 格心（纯定点）
            long fx = MapData.GridToWorld(gx);
            long fz = MapData.GridToWorld(gz);

            state.Enemies[slot] = new Entity {
                Id = state.AllocId(),
                Kind = EntityKind.Enemy,
                Alive = true,
                X = fx, Y = 0, Z = fz,
                Hp = GameConst.EnemyMaxHp,
                MaxHp = GameConst.EnemyMaxHp,
                SpawnIdx = si,
                SpawnX = fx, SpawnY = 0, SpawnZ = fz,
                // ColliderComponent（对齐 C++ enemy_reset：BOX 0.35×1.8×0.35）
                ColliderType = ColliderType.Box,
                ColHx = Fp.FromFloat(0.35f), ColHy = Fp.FromFloat(0.9f), ColHz = Fp.FromFloat(0.35f),
                // AI：home = 实际出生点，半径由 SpawnDef 转 fp
                PatrolCenterX = fx, PatrolCenterZ = fz,
                TargetX = fx, TargetZ = fz,
                PatrolRadius = Fp.FromFloat(sd.PatrolRadius),
                DetectRadius = Fp.FromFloat(sd.DetectRadius),
                AttackRadius = Fp.FromFloat(sd.AttackRadius),
                GuardRadius = Fp.FromFloat(sd.GuardRadius),
                AiState = 0,
                AiTimer = 0,
            };
        }

        /// <summary>为指定关卡生成整波敌人（清空既有敌人后填入）。返回生成数量。</summary>
        public static int SpawnLevel(GameState state, int level)
        {
            state.Level = level;
            state.MapSeed = level;
            Array.Clear(state.Enemies, 0, state.Enemies.Length);

            var tiles = new int[MapConst.WaveMaxEnemies];
            var si = new int[MapConst.WaveMaxEnemies];
            int got = PrepareEnemies(state, level, tiles, si);
            if (got > DebugMaxEnemies) got = DebugMaxEnemies;
            for (int i = 0; i < got; ++i) SpawnEnemy(state, i, tiles[i], si[i]);
            state.Wave = 1;
            return got;
        }
    }
}
