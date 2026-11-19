using System;

namespace ProxyGame
{
    /// <summary>复刻 spawn_def.h 的 SpawnDef（float 字段仅作定义表，实际生成时转 fp）。</summary>
    public struct SpawnDef
    {
        public float X, Z;
        public float PatrolRadius;
        public float DetectRadius;
        public float AttackRadius;
        public float GuardRadius;
    }

    /// <summary>复刻 spawn_def.h 的 ENEMY_SPAWNS 表与常量。</summary>
    public static class SpawnDefs
    {
        public const int EnemySpawnCount = 2;
        public const int WaveEnemyMinDist10 = 30;

        public static readonly SpawnDef[] EnemySpawns = {
            // [0] 全局刺客：detect 覆盖整图，无领地限制
            new SpawnDef { X = 10.0f, Z = 10.0f, PatrolRadius = 3.0f,
                           DetectRadius = 400.0f, AttackRadius = 2.0f, GuardRadius = 0.0f },
            // [1] 守卫：24m 领地
            new SpawnDef { X = 8.0f, Z = 8.0f, PatrolRadius = 6.0f,
                           DetectRadius = 400.0f, AttackRadius = 2.0f, GuardRadius = 24.0f },
        };
    }
}
