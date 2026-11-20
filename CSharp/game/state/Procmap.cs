using System;

namespace ProxyGame
{
    /// <summary>
    /// 复刻 shared/systems/procedural_map.h 的业务常量与薄封装层。
    /// 核心 BSP 生成算法在 joy 引擎（joy_scene_procmap_generate_seed），
    /// 项目侧只保留 PM_* 常量与配置构造；当前 5 张手绘地图都有 E 格，不触发兜底生成。
    /// </summary>
    public static class PmConst
    {
        public const int GridN = MapConst.GridN;              // 64
        public const int Clear = 22;                          // 出生安全区半径 2.2m
        public const int Pad10 = 6;                           // 障碍外扩 0.6m
        public const int Offset10 = MapConst.GridOffset * 10; // 320
        public const int MaxObs = MapConst.MaxObs;            // 4096
        public const int Density = 25;
        public const int HeightMin10 = 5, HeightMax10 = 18;
        public const int ClusterMin = 5, ClusterMax = 10;
        public const int EnemyCx10 = 100, EnemyCz10 = 100;
    }

    /// <summary>复刻 joy_pm_config_t（仅本项目用到的标量 + 出生安全区列表）。</summary>
    public sealed class PmConfig
    {
        public int GridN, Offset10, Pad10, Density, MaxObs, HeightMin10, HeightMax10, ClusterMin, ClusterMax;
        public int SpawnCount;
        public int[] SpawnCx10 = new int[8];
        public int[] SpawnCz10 = new int[8];
        public int[] SpawnRadius10 = new int[8];

        public static PmConfig Build()
        {
            var c = new PmConfig {
                GridN = PmConst.GridN, Offset10 = PmConst.Offset10, Pad10 = PmConst.Pad10,
                Density = PmConst.Density, MaxObs = PmConst.MaxObs,
                HeightMin10 = PmConst.HeightMin10, HeightMax10 = PmConst.HeightMax10,
                ClusterMin = PmConst.ClusterMin, ClusterMax = PmConst.ClusterMax,
                SpawnCount = 0,
            };
            c.AddSpawn(0, 0, PmConst.Clear);                        // 玩家安全区
            c.AddSpawn(PmConst.EnemyCx10, PmConst.EnemyCz10, PmConst.Clear); // 敌人安全区
            return c;
        }

        public void AddSpawn(int cx10, int cz10, int radius10)
        {
            if (SpawnCount >= 8) return;
            SpawnCx10[SpawnCount] = cx10;
            SpawnCz10[SpawnCount] = cz10;
            SpawnRadius10[SpawnCount] = radius10;
            SpawnCount++;
        }
    }

    /// <summary>procedural_map.h 的纯函数部分（确定性，可 parity）。</summary>
    public static class Procmap
    {
        /// <summary>pmap_wave_seed：base + wave*1000003（uint32 溢出回绕）。</summary>
        public static uint WaveSeed(uint baseSeed, int wave)
        {
            unchecked { return baseSeed + (uint)wave * 1000003u; }
        }
    }
}
