#ifndef PLAYER_EVENT_INPUT_H
#define PLAYER_EVENT_INPUT_H
#include <vector>
#include <joy/jmath.h>

struct PlayerEventInput {
    // 动作类型（攻击事件：由输入/乘积系统推入，攻击结算系统据类型分发）
    enum ActionType : uint8_t {
        ATTACK_MELEE  = 1,   // 近战
        ATTACK_BULLET = 2,   // 射击（直线飞行的子弹）
        ATTACK_RAY    = 3,   // 发射射线（hitscan 瞬时命中，沿朝向 yaw 直线，只伤首个敌人）
    };

    // 单个动作定义
    struct Action {
        uint32_t client_seq;   // 客户端序列号（用于调试/防重放）
        uint8_t type;          // ActionType：近战/射击（敌我共用同一套类型）
        // 可扩展：如目标位置、角度等
    };

    // 待处理的事件队列（FIFO）
    std::vector<Action> pending_actions;

    // 方便清空
    void clear() { pending_actions.clear(); }

};

#endif
