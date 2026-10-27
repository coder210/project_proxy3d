#ifndef AI_SYSTEM_H
#define AI_SYSTEM_H

#include "../components/ai_component.h"
#include "../components/enemy_component.h"
#include "../components/player_component.h"
#include "../components/logic_position_component.h"
#include "../components/action_state_component.h"
#include "../components/player_event_input.h"
#include "../components/health_component.h"
#include "../components/pathfinding_component.h"

struct Context;

// 敌人移动已统一交给 pathfinding_system 沿 A* 网格路径执行（巡逻/追击都走格），
// 此处不再做直线移动，故 AI_STEP / MIN_MOVE_DIST 已移除。

static void ai_system(entt::registry& registry, Context* ctx, float /*dt*/) {
    (void)ctx;

    auto view = registry.view<EnemyComponent, AIComponent, LogicPositionComponent, ActionStateComponent>();
    auto player_view = registry.view<PlayerComponent, LogicPositionComponent>();

    /* [DIAG] 确认 AI 系统是否在跑、处理了几个敌人（节流） */
    static int ai_diag_n = 0;
    if ((++ai_diag_n & 31) == 0)
        log_info("[DIAG-ai] view=%d players=%d", (int)view.size_hint(), (int)player_view.size_hint());

    for (auto e : view) {
        auto& ai  = view.get<AIComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);
        auto& act = view.get<ActionStateComponent>(e);

        // 死亡敌人不移动、不攻击
        if (registry.all_of<HealthComponent>(e) && registry.get<HealthComponent>(e).dead) {
            if (registry.all_of<PlayerEventInput>(e))
                registry.get<PlayerEventInput>(e).pending_actions.clear();
            continue;
        }

        // 寻找最近玩家（XZ 平面距离）
        bool found_player = false;
        fp_t nearest_dist_sq = fp_mul(ai.detect_radius, ai.detect_radius);
        fp_t nearest_px = fp_zero(), nearest_pz = fp_zero();

        for (auto p : player_view) {
            auto& ppos = player_view.get<LogicPositionComponent>(p);
            fp_t dx = fp_sub(ppos.x, pos.x);
            fp_t dz = fp_sub(ppos.z, pos.z);
            fp_t dist_sq = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
            if (dist_sq < nearest_dist_sq) {
                nearest_dist_sq = dist_sq;
                found_player = true;
                nearest_px = ppos.x;
                nearest_pz = ppos.z;
            }
        }

        // 攻击半径判断
        fp_t attack_dist_sq = fp_mul(ai.attack_radius, ai.attack_radius);
        bool in_attack_range = found_player && (nearest_dist_sq < attack_dist_sq);

        // 领地判定（守卫型）：玩家是否在领地内（距 home <= guard_radius）；全局型始终在领地内
        bool is_guard = ai.guard_radius > fp_zero();
        fp_t hdx = is_guard ? fp_sub(nearest_px, ai.patrol_center_x) : fp_zero();
        fp_t hdz = is_guard ? fp_sub(nearest_pz, ai.patrol_center_z) : fp_zero();
        bool in_territory = !is_guard ||
            fp_add(fp_mul(hdx, hdx), fp_mul(hdz, hdz)) <= fp_mul(ai.guard_radius, ai.guard_radius);
        // 敌人到出生点的距离（守卫返回判定）
        fp_t ehx = fp_sub(pos.x, ai.patrol_center_x);
        fp_t ehz = fp_sub(pos.z, ai.patrol_center_z);
        fp_t dist_home_sq = fp_add(fp_mul(ehx, ehx), fp_mul(ehz, ehz));
        fp_t HOME_EPS = fp_div(fp_from_int(4), fp_from_int(5)); // 0.8m：到家阈值

        /* [DIAG] 运行时实况（节流，配合 stderr 重定向采集）
           默认关闭：每帧刷屏影响测试；排查寻路/AI 时置 1 重新开启 */
#if 0
        static int ai_diag_n = 0;
        if ((++ai_diag_n & 31) == 0)
            log_info("[ai] e=%d state=%d found=%d attack=%d guard=%d terr=%d p=%.2f,%.2f e=%.2f,%.2f",
                     (int)e, ai.state, (int)found_player, (int)in_attack_range,
                     (int)(ai.guard_radius > fp_zero()), (int)in_territory,
                     fp_to_float(nearest_px), fp_to_float(nearest_pz),
                     fp_to_float(pos.x), fp_to_float(pos.z));
#endif

        // 设置追击/返回目标格并触发 A* 重算
        auto set_target = [&](fp_t tx, fp_t tz) {
            if (!registry.all_of<PathComponent>(e)) registry.emplace<PathComponent>(e);
            auto& pc = registry.get<PathComponent>(e);
            int pgx = pf_world_to_grid(tx), pgz = pf_world_to_grid(tz);
            if (pgx != pc.target_gx || pgz != pc.target_gz) { pc.target_gx = pgx; pc.target_gz = pgz; pc.dirty = true; }
        };

        if (in_attack_range && in_territory) {
            // 状态 2：攻击（近战 + 射击，两套冷却分别节流）
            ai.state = 2;
            act.state = ActionStateComponent::ATTACK;
            if (!registry.all_of<PlayerEventInput>(e)) registry.emplace<PlayerEventInput>(e);
            auto& events = registry.get<PlayerEventInput>(e);
            events.pending_actions.push_back({ 0, PlayerEventInput::ATTACK_MELEE });
            // 丧尸全部为近战：不再推送 ATTACK_BULLET（去掉敌人远程攻击，见设计决定）
        }
        else if (found_player && in_territory) {
            // 状态 1：追击（走 A* 寻路）；追击中近战（靠近后由 attack 态触发），不远程射击
            ai.state = 1;
            act.state = ActionStateComponent::WALK;
            set_target(nearest_px, nearest_pz);
            ai.timer = 0;
        }
        else if (is_guard && !in_territory && (ai.state == 1 || ai.state == 2)) {
            // 守卫被激怒后玩家逃出领地：放弃追击，返回出生点
            ai.state = 3;
            act.state = ActionStateComponent::WALK;
            set_target(ai.patrol_center_x, ai.patrol_center_z);
        }
        else if (ai.state == 3) {
            // 守卫返回途中：继续往出生点走；已到家则恢复巡逻
            if (dist_home_sq < fp_mul(HOME_EPS, HOME_EPS)) {
                ai.state = 0;
                ai.timer = 120;
            } else {
                act.state = ActionStateComponent::WALK;
                set_target(ai.patrol_center_x, ai.patrol_center_z);
            }
        }
        else {
            // 状态 0：巡逻（守卫型也是在其出生点周边；目标钳到可通行格，由 A* 执行）
            ai.state = 0;
            act.state = ActionStateComponent::WALK;
            ai.timer--;
            if (ai.timer <= 0) {
                ai.target_x = fp_sub(ai.patrol_center_x, ai.patrol_radius);
                ai.target_z = fp_sub(ai.patrol_center_z, ai.patrol_radius);
                ai.timer = 120;
            }
            set_target(ai.target_x, ai.target_z);
        }
    }
}

#endif