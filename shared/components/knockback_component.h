#ifndef KNOCKBACK_COMPONENT_H
#define KNOCKBACK_COMPONENT_H
#include <joy/jmath.h>

// 受击击退（帧同步逻辑）：命中时由攻击结算系统挂上，knockback_system 每 tick
// 沿 (vx,vz) 位移并递减 ticks_left，归零后移除。全程定点数，两端结果一致。
struct KnockbackComponent {
    fp_t vx;          // 击退速度 X（米/tick）
    fp_t vz;          // 击退速度 Z（米/tick）
    int  ticks_left;  // 剩余 tick 数
};

#endif
