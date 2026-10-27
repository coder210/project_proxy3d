#ifndef AI_COMPONENT_H
#define AI_COMPONENT_H
#include <joy/jmath.h>

struct AIComponent {
    fp_t patrol_center_x, patrol_center_z; // 巡逻中心点（XZ 平面，即出生点/领地中心）
    fp_t patrol_radius;   // 巡逻范围
    fp_t detect_radius;   // 检测玩家范围（触发追击的最大距离）
    fp_t attack_radius;   // 攻击范围
    fp_t guard_radius;    // 领地半径：0=全局型（全图追踪）；>0=守卫型（玩家逃出该范围即放弃追击，返回出生点）
    int  state;           // 0=巡逻, 1=追击, 2=攻击, 3=返回出生点
    int  timer;           // 计时器
    fp_t target_x, target_z; // 当前移动目标（XZ 平面）

    AIComponent() = default;
    AIComponent(fp_t cx, fp_t cz, fp_t pr, fp_t dr, fp_t ar, fp_t gr)
        : patrol_center_x(cx), patrol_center_z(cz), patrol_radius(pr),
          detect_radius(dr), attack_radius(ar), guard_radius(gr),
          state(0), timer(0), target_x(cx), target_z(cz) {}
};

#endif
