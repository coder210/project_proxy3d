#ifndef ATTACK_RAY_EFFECT_COMPONENT_H
#define ATTACK_RAY_EFFECT_COMPONENT_H

// 3D 攻击特效：玩家挥击时在身前生成一块短暂显示的地面扇形/矩形区域
// dir_x/dir_z：刀光朝向的单位方向（逻辑帧由定点数 fp_t 归一化得出，保证帧同步一致）
struct AttackRayEffectComponent {
        float x, z;       // 攻击者中心（XZ 平面）
        float dir_x, dir_z;  // 刀光朝向（单位向量，由逻辑帧定点数计算）
        float range;      // 攻击距离（米）
        float width;      // 攻击弧宽（米）
        float lifetime;   // 剩余显示时长（秒）
        bool  is_enemy;    // 是否敌方攻击（水墨刀光用色区分，仅渲染）
};

#endif
