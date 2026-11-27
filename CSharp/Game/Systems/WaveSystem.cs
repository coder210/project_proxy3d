using System;

namespace ProxyGame.Systems
{
    /// <summary>
    /// 复刻 shared/systems/wave_system.h 的单关波次状态机（权威端）：
    ///   apply_level_change → 销毁旧敌/障碍、重置全局状态、重建障碍、玩家重定位、刷整波敌
    ///   wave_system       → 开局建关；有存活敌人不动；全灭后置 LevelCleared（单关制不切关）
    /// 双端确定性：所有派生量都来自 level 与地图解析（纯函数），实体按下标升序处理。
    /// 单关制下 apply_level_change 只调用一次，故对象池恒为空、敌人全部新建（id 顺序与 C++ 一致）。
    /// </summary>
    public static class WaveSystem
    {
        /// <summary>真建关：重建布局、重定位玩家、刷本关全部敌人（对应 apply_level_change）。</summary>
        public static int ApplyLevelChange(GameState state, int newLevel)
        {
            // 全局状态重置（对应 gs.level/wave/level_cleared/map_wave_generated/map_seed）
            state.Level = newLevel;
            state.Wave = 0;
            state.LevelCleared = false;
            state.MapSeed = newLevel;

            // 重建障碍（oid 从 10000 起，与 spawn_obstacles_from_layout 一致）
            var ll = MapData.BuildLayout(newLevel);
            int nextId = 10000;
            state.ObstacleCount = 0;
            Pathfinding.BuildObstaclesFromLayout(state, ll.Layout, ref nextId);
            state.PfWalkableDirty = true;   // 障碍已重建，寻路栅格需重算

            // 玩家全部重定位到新关出生点（level_player_spawn，纯函数双端一致）
            MapData.PlayerSpawn(newLevel, out long fx, out long fz);
            for (int i = 0; i < state.Players.Length; ++i)
            {
                var pl = state.Players[i];
                if (pl.Kind != EntityKind.Player || !pl.Alive) continue;
                pl.X = fx; pl.Y = 0; pl.Z = fz;
                pl.SpawnX = fx; pl.SpawnY = 0; pl.SpawnZ = fz;
                state.Players[i] = pl;
            }

            // 刷第一批敌（单波清场，直接生成，不再走延迟）；内部会设置 Wave=1
            int spawned = WaveSpawnSystem.SpawnLevel(state, newLevel);
            Joy.Host.log("[wave] level=" + newLevel + " spawned=" + spawned + " obstacles=" + state.ObstacleCount);
            return spawned;
        }

        /// <summary>单关推进（对应 wave_system）：开局建关 / 战斗不动作 / 清场置位。</summary>
        public static void Update(GameState state)
        {
            // 首次：本关尚未开局 → 建图 + 刷第一批敌
            if (state.Wave == 0 && !state.LevelCleared)
            {
                ApplyLevelChange(state, state.Level);
                return;
            }

            // 战斗中：仍有存活敌人
            bool waveDone = true;
            for (int i = 0; i < state.Enemies.Length; ++i)
            {
                var e = state.Enemies[i];
                if (e.Kind == EntityKind.Enemy && e.Alive && !e.Dead)
                {
                    waveDone = false;
                    break;
                }
            }
            if (!waveDone) return;

            // 清场完成：置位标记；单关制，绝不推进下一关
            if (!state.LevelCleared) state.LevelCleared = true;
        }
    }
}
