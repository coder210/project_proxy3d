#ifndef SPAWN_POINT_COMPONENT_H
#define SPAWN_POINT_COMPONENT_H
#include <joy/jmath.h>

// 出生点：玩家/敌人死亡重生时传送回该位置（帧同步确定性：两端同一 tick 传送）
struct SpawnPointComponent {
    fp_t x, y, z;
};

#endif
