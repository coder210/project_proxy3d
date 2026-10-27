#pragma once

struct ActionStateComponent {
    enum State : int { IDLE = 0, WALK = 1, ATTACK = 2 };
    State state = IDLE;
    float yaw = 0.0f;       // 水平朝向（弧度）

    // 近战攻击冷却（tick 数）：攻击事件结算后置满，由攻击结算系统每 tick 递减，
    // 防止按住攻击键 / 敌人每 tick 产生攻击事件导致高频结算。
    int melee_cooldown = 0;
    static constexpr int MELEE_COOLDOWN = 12;   // 0.4s @ 30Hz

    // 射击冷却（tick 数）：发射子弹后置满，敌我共用节流发率
    int bullet_cooldown = 0;
    static constexpr int BULLET_COOLDOWN = 30;   // 1s @ 30Hz

    // 射线冷却（tick 数）：发射射线（hitscan）后置满，仅玩家使用
    int ray_cooldown = 0;
    static constexpr int RAY_COOLDOWN = 45;   // 1.5s @ 30Hz，比子弹稍长（瞬发高穿透）
};