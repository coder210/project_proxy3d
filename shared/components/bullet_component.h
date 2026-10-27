#ifndef BULLET_COMPONENT_H
#define BULLET_COMPONENT_H
#include <joy/jmath.h>

// 子弹（逻辑层组件，敌我共用）：
// - 由攻击结算系统（melee_attack_system 的射击分支）确定性地创建
// - 由 bullet_system 每逻辑 tick 移动/碰撞/老化并销毁
// - 两端（客户端/服务端）同一帧同一输入派生同一批子弹 → 帧同步确定
// - 渲染 pass 只读本组件位置绘制，不参与判定
struct BulletComponent {
    fp_t x, y, z;          // 当前位置（世界坐标，XZ 移动、Y 贴地）
    fp_t dx, dz;           // 归一化朝向（XZ 平面单位向量）
    fp_t speed;            // 飞行速度（米/逻辑 tick）
    fp_t range_left;       // 剩余可飞行距离（米），耗尽即销毁
    int  damage;           // 命中伤害
    bool from_enemy;       // true=敌人射出（命中玩家）；false=玩家射出（命中敌人）
    int  owner_id;         // 射手 IdComponent.id（命中结算跳过射手自身）
};

#endif