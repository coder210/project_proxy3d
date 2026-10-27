#ifndef SPAWN_DEF_H
#define SPAWN_DEF_H

// 敌人生成位置和参数（服务端和客户端共享，保证初始条件一致）
struct SpawnDef {
    float x, z;           // 位置（XZ 平面，Y 由地面决定）
    float patrol_radius;  // 巡逻半径
    float detect_radius;  // 检测范围（触发追击的最大距离）
    float attack_radius;  // 攻击范围
    float guard_radius;   // 领地半径：0=全局型（全图追踪）；>0=守卫型（玩家逃出该范围即放弃追击，返回出生点）
};

inline const SpawnDef ENEMY_SPAWNS[] = {
    // [0] 全局刺客：detect 覆盖整张 64m 级大图（>对角线），真正全图锁定追踪，无领地限制
    { 10.0f, 10.0f, 3.0f, 400.0f, 2.0f, 0.0f },
    // [1] 守卫：detect/guard 对齐"房间级"尺度（约 24m，约等于房间短边），玩家进房即追击，
    //     仅当玩家远离其出生点 24m 外才放弃回巢 → 恢复"守住一片区域"的压迫感
    { 8.0f,  8.0f,  6.0f, 400.0f, 2.0f, 24.0f },
};
inline constexpr int ENEMY_SPAWN_COUNT = 2;

// 波次敌人出生参数（波次换图用）
// 敌人在程序化地图的开放格中随机出生，需离玩家至少 3.0m（十分之一米）
inline constexpr int WAVE_ENEMY_MIN_DIST10 = 30;
// 每波敌人数量：每波至少 2 只（保证全局刺客与领地守卫各出一只），之后每波 +1，上限 8
inline constexpr int wave_enemy_count(int wave) {
    int n = wave;
    if (n < 2) n = 2;
    if (n > 8) n = 8;
    return n;
}

#endif