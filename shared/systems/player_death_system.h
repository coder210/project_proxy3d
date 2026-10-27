#ifndef PLAYER_DEATH_SYSTEM_H
#define PLAYER_DEATH_SYSTEM_H

#include "../components/health_component.h"
#include "../components/spawn_point_component.h"
#include "../components/logic_position_component.h"
#include "../components/player_component.h"
#include "../components/enemy_component.h"
#include "../components/action_state_component.h"
#include "../components/player_state_input.h"
#include "../components/transform_component.h"

struct Context;

// 玩家 / 敌人死亡与重生（帧同步确定性：两端同一 tick 判定与传送，无需网络同步）
// 注册在 melee_attack_system 之后：扣血 → 死亡判定 → 重生计时
static void player_death_system(entt::registry& registry, Context* ctx, float /*dt*/)
{
    (void)ctx;

    // ---- 玩家 ----
    auto view = registry.view<PlayerComponent, HealthComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& h   = view.get<HealthComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);

        // 死亡判定：HP 归零（melee_attack_system 已把 HP 钳制在 0）
        if (!h.dead && h.hp <= 0) {
            h.dead = true;
            h.respawn_timer = PLAYER_RESPAWN_TICKS;
        }

        // 无敌保护倒计时（玩家可能未被判定死亡，但重生后有无敌窗口）
        if (h.spawn_invuln > 0) h.spawn_invuln -= 1;

        // 重生计时与传送回出生点
        if (h.dead) {
            h.respawn_timer -= 1;   // 每逻辑 tick 减 1
            if (h.respawn_timer <= 0) {
                h.dead = false;
                h.hp = h.max_hp;
                h.spawn_invuln = SPAWN_INVULN_TICKS;  // 重生无敌，防出生点被围殴"重生即死"
                if (registry.all_of<SpawnPointComponent>(e)) {
                    const auto& sp = registry.get<SpawnPointComponent>(e);
                    pos.x = sp.x; pos.y = sp.y; pos.z = sp.z;
                }
                // 清重生前残留输入/动作状态：
                // 防"死亡时按着方向键/出招中，重生瞬间自动冲出或摆出攻击姿势"
                if (registry.all_of<PlayerStateInput>(e))
                    registry.get<PlayerStateInput>(e).move_bits = 0;
                if (registry.all_of<ActionStateComponent>(e)) {
                    auto& act = registry.get<ActionStateComponent>(e);
                    act.state           = ActionStateComponent::IDLE;
                    act.melee_cooldown  = 0;   // 清近战冷却，防重生瞬间立刻挥击
                }
                // 同步渲染坐标，避免插值缓慢漂移
                if (registry.all_of<TransformComponent>(e)) {
                    auto& t = registry.get<TransformComponent>(e);
                    t.position.x = fp_to_float(pos.x);
                    t.position.y = fp_to_float(pos.y);
                    t.position.z = fp_to_float(pos.z);
                }
            }
        }
    }

    // 敌人不做自动重生：死亡后保留 dead 标记，由 wave_system 在波次结束时统一销毁重建
}

#endif
