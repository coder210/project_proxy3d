#ifndef PLAYER_PHYSICS_COMPONENT_H
#define PLAYER_PHYSICS_COMPONENT_H
#include <joy/jmath.h>

// 玩家垂直物理（重力/跳跃/落地状态）
struct PlayerPhysicsComponent {
    fp_t vertical_velocity;  // 垂直速度
    bool on_ground;          // 是否在地面
    bool jump_prev;          // 上一帧跳跃键状态（上升沿检测）
    bool jump_requested;     // 本帧是否请求跳跃

    PlayerPhysicsComponent()
        : vertical_velocity(fp_zero()), on_ground(true), jump_prev(false), jump_requested(false) {}
};

#endif
