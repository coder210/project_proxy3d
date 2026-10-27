#ifndef MELEE_ATTACK_SYSTEM_H
#define MELEE_ATTACK_SYSTEM_H

#include "../components/player_event_input.h"
#include "../components/action_state_component.h"
#include "../components/logic_position_component.h"
#include "../components/player_component.h"
#include "../components/enemy_component.h"
#include "../components/ai_component.h"
#include "../components/health_component.h"
#include "../components/id_component.h"
#include "../components/bullet_component.h"
#include "../components/global_state_component.h"
#include "../components/hit_flash_component.h"
#include "../components/knockback_component.h"
#include "../components/static_mesh_component.h"
#include "../components/collider_component.h"
#include "knockback_system.h"
#include "character_def.h"
#include <joy/jmath.h>
#include <joy/joy.h>

struct Context;

// 近战攻击参数（玩家侧数值由角色定义表 character_def 提供；此处仅敌人常量）
static constexpr int   ENEMY_MELEE_DAMAGE      = 1;      // 敌人贴身 DPS 单次伤害
static constexpr int   ENEMY_MELEE_DPS_COOLDOWN = 4;     // 敌人贴身咬合冷却（tick，短 → 高频 DPS，≈0.2s@20Hz）
static constexpr int   ENEMY_POUNCE_DAMAGE     = 3;      // 敌人重扑额外伤害（间隔触发）
static constexpr int   ENEMY_POUNCE_COOLDOWN   = 20;     // 重扑间隔（tick，≈1.0s@20Hz）

// 子弹参数（敌我共用）
static const fp_t BULLET_SPEED = fp_from_float(0.4f);   // 米/逻辑 tick（12 m/s @ 30Hz）
static const fp_t BULLET_RANGE = fp_from_float(12.0f);  // 最大飞行距离（米）
static const int  ENEMY_BULLET_DAMAGE  = 5;             // 玩家 100 血 → 20 发击杀

// 攻击结算：消费 PlayerEventInput.pending_actions，按动作类型分发（敌我共用同一套路径）
// - 近战（type=ATTACK_MELEE）：
//     玩家：自动锁敌，用定点数 GJK3D 以朝向 yaw 张开的凸半圆柱（180°半圆盘）与敌人盒体做
//           碰撞检测，锁定最接近命中的存活敌人并朝其出招（必中），无目标则空挥；特效方向跟随
//           锁定目标（离屏仅写渲染缓冲，不建渲染实体）
//     敌人：以 AI attack_radius 距离命中第一个玩家
// - 射击（type=ATTACK_BULLET）：
//     玩家沿朝向 yaw 飞出子弹；敌人锁定最近存活玩家并沿其方向飞出子弹。
//     子弹实体为逻辑组件（BulletComponent），由 bullet_system 移动/碰撞/销毁，两端确定。
// 帧同步确定性：攻击事件与命中计算均为确定性，两端结果一致，无需网络同步。
// 注册顺序：ai_system 之后（敌人推事件）、bullet_system 之前。
static void melee_attack_system(entt::registry& registry, Context* ctx, float /*dt*/)
{
    auto view = registry.view<PlayerEventInput, ActionStateComponent, LogicPositionComponent>();
    auto enemy_view  = registry.view<EnemyComponent, LogicPositionComponent, HealthComponent>();
    auto player_view = registry.view<PlayerComponent, LogicPositionComponent, HealthComponent>();

    for (auto attacker : view) {
        auto& events = view.get<PlayerEventInput>(attacker);
        auto& act    = view.get<ActionStateComponent>(attacker);
        auto& apos   = view.get<LogicPositionComponent>(attacker);

        // 冷却自然流逝；无事件直接跳过（敌人每 tick 推事件，靠各自的冷却节流攻击频率）
        if (act.melee_cooldown > 0)  act.melee_cooldown  -= 1;
        if (act.bullet_cooldown > 0) act.bullet_cooldown -= 1;
        if (act.ray_cooldown > 0)    act.ray_cooldown    -= 1;
        if (events.pending_actions.empty()) continue;

        // 死亡实体不攻击，丢弃事件
        if (registry.all_of<HealthComponent>(attacker) &&
            registry.get<HealthComponent>(attacker).dead) {
            events.pending_actions.clear();
            continue;
        }

        // 归类：是否有近战 / 射击欲望（敌我共用同一动作类型枚举）
        bool want_melee = false, want_bullet = false, want_ray = false;
        for (auto& a : events.pending_actions) {
            if (a.type == PlayerEventInput::ATTACK_BULLET) want_bullet = true;
            else if (a.type == PlayerEventInput::ATTACK_RAY) want_ray = true;
            else want_melee = true;
        }

        bool is_player = registry.all_of<PlayerComponent>(attacker);
        // 玩家攻击属性按角色定义表取（帧同步确定性：两端同一 char_id 查同一张表）
        const CharacterDef& cd = character_def(
            is_player ? registry.get<PlayerComponent>(attacker).char_id : CHAR_WARRIOR);
        const fp_t R = cd.melee_range;

        // ================= 近战 =================
        if (want_melee && act.melee_cooldown <= 0) {
            // 玩家：标准近战冷却；敌人：短冷却形成贴身高频 DPS
            act.melee_cooldown = is_player
                ? ActionStateComponent::MELEE_COOLDOWN
                : ENEMY_MELEE_DPS_COOLDOWN;

            fp_t dir_fx = fp_zero(), dir_fz = fp_zero();   // 刀光方向（定点，仅渲染用）

            if (is_player) {
                // ---- 玩家近战：半圆 GJK3D 判定 ----
                fp_t yaw_fp = fp_from_float(act.yaw);
                fp_t fx = fp_sin(yaw_fp);   // 定点 sin/cos（LUT 整数实现，替代 float sinf/cosf）
                fp_t fz = fp_cos(yaw_fp);

                static const int SEMI_SEGS   = 8;
                static const int SEMI_CENTER = 2;
                static const int SEMI_ARC    = (SEMI_SEGS + 1) * 2;
                static const int SEMI_TOTAL  = SEMI_CENTER + SEMI_ARC;
                vec3f_t semi_verts[SEMI_TOTAL];
                const fp_t deg2r = fp_from_float(3.14159265f / 180.0f);
                const fp_t yb    = fp_sub(apos.y, fp_from_float(0.5f));
                const fp_t yt    = fp_add(apos.y, fp_from_float(1.2f));

                int k = 0;
                semi_verts[k].x = apos.x; semi_verts[k].y = yb; semi_verts[k].z = apos.z; k++;
                semi_verts[k].x = apos.x; semi_verts[k].y = yt; semi_verts[k].z = apos.z; k++;
                for (int i = 0; i <= SEMI_SEGS; ++i) {
                    fp_t t = fp_div(fp_from_float(i), fp_from_float(SEMI_SEGS));
                    fp_t ang = fp_sub(yaw_fp, fp_mul(fp_from_float(90.0f), deg2r));
                    ang = fp_add(ang, fp_mul(fp_from_float(180.0f), fp_mul(deg2r, t)));
                    fp_t px = fp_mul(R, fp_sin(ang));
                    fp_t pz = fp_mul(R, fp_cos(ang));
                    semi_verts[k].x = fp_add(apos.x, px); semi_verts[k].y = yb; semi_verts[k].z = fp_add(apos.z, pz); k++;
                    semi_verts[k].x = fp_add(apos.x, px); semi_verts[k].y = yt; semi_verts[k].z = fp_add(apos.z, pz); k++;
                }

                gjk3df_collider_t semi_shape;
                gjk3df_init_mesh(&semi_shape, semi_verts, k);

                entt::entity lock = entt::null;
                fp_t lock_dist = fp_from_float(1e9f);
                vec3f_t init_dir = { fx, fp_zero(), fz };
                for (auto target : enemy_view) {
                    auto& h = enemy_view.get<HealthComponent>(target);
                    if (h.dead) continue;
                    auto& tp = enemy_view.get<LogicPositionComponent>(target);
                    fp_t dx = fp_sub(tp.x, apos.x);
                    fp_t dz = fp_sub(tp.z, apos.z);
                    fp_t dist_sq = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    gjk3df_collider_t enemy_shape;
                    vec3f_t epos = { tp.x, tp.y, tp.z };
                    vec3f_t ehs  = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
                    gjk3df_init_box(&enemy_shape, epos, ehs);
                    gjk3df_contact_t contact;
                    if (gjk3df_collide(&semi_shape, &enemy_shape, init_dir, &contact)) {
                        if (dist_sq < lock_dist) { lock = target; lock_dist = dist_sq; }
                    }
                }

                dir_fx = fx; dir_fz = fz;
                if (lock != entt::null) {
                    auto& hp = enemy_view.get<HealthComponent>(lock);
                    auto& tp = enemy_view.get<LogicPositionComponent>(lock);
                    fp_t ldx = fp_sub(tp.x, apos.x);
                    fp_t ldz = fp_sub(tp.z, apos.z);
                    fp_t dist = fp_sqrt(fp_add(fp_mul(ldx, ldx), fp_mul(ldz, ldz)));
                    dir_fx = fp_div(ldx, dist);
                    dir_fz = fp_div(ldz, dist);
                    hp.hp -= cd.melee_damage; if (hp.hp < 0) hp.hp = 0;
                    // 受击闪烁（仅客户端渲染表现）
                    if (!ctx->is_server) {
                        registry.emplace_or_replace<HitFlashComponent>(lock, HitFlashComponent{ 0.12f });
                    }
                    bool kill = false;
                    if (hp.hp <= 0 && !hp.dead) {
                        hp.dead = true;
                        ++registry.ctx().get<GlobalState>().kills;  // 玩家近战击杀
                        kill = true;
                        // 击杀反馈（仅客户端本地表现）：2 秒窗口内连续击杀递增连杀
                        if (!ctx->is_server) {
                            uint64_t now = get_ticks();
                            if (now - ctx->last_kill_ms <= 2000) ctx->kill_combo++;
                            else ctx->kill_combo = 1;
                            ctx->last_kill_ms = now;
                            ctx->kill_events.push_back({ now, ctx->kill_combo });
                            ctx->sfx_requests.push_back({ SFX_KILL });
                        }
                    }
                    // 受击击退：往远离攻击者方向小跳（仅存活实体，帧同步定点）
                    if (!kill && dist > fp_zero()) {
                        registry.emplace_or_replace<KnockbackComponent>(lock, KnockbackComponent{
                            fp_mul(dir_fx, KNOCKBACK_SPEED), fp_mul(dir_fz, KNOCKBACK_SPEED), KNOCKBACK_TICKS
                        });
                    }
                    // 玩家近战伤害飘字（仅客户端看；渲染 pass 消费）
                    if (!ctx->is_server) {
                        ctx->damage_texts.push_back({ tp.x, tp.z, cd.melee_damage, kill });
                        ctx->sfx_requests.push_back({ SFX_HIT });
                    }
                }
            }
            else {
                // ---- 敌近战：attack_radius 距离命中玩家 ----
                fp_t range = registry.get<AIComponent>(attacker).attack_radius;
                fp_t ndx = fp_zero(), ndz = fp_zero();   // 最近存活玩家方向（供刀光朝向）
                fp_t best = fp_from_float(1e9f);
                bool hit = false;
                for (auto target : player_view) {
                    auto& h = player_view.get<HealthComponent>(target);
                    if (h.dead) continue;
                    if (h.spawn_invuln > 0) continue;
                    auto& tp = player_view.get<LogicPositionComponent>(target);
                    fp_t dx = fp_sub(tp.x, apos.x);
                    fp_t dz = fp_sub(tp.z, apos.z);
                    fp_t dist_sq = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (dist_sq < best) { best = dist_sq; ndx = dx; ndz = dz; }
                    if (dist_sq < fp_mul(range, range)) {
                        // 贴身 DPS + 间隔重扑：重扑复用敌人已闲置的 bullet_cooldown 计时
                        // （改1后敌人不再射子弹，该字段对敌人空闲）；重扑命中在贴身上叠加高额伤害
                        int dmg = ENEMY_MELEE_DAMAGE;
                        if (act.bullet_cooldown <= 0) {
                            dmg += ENEMY_POUNCE_DAMAGE;
                            act.bullet_cooldown = ENEMY_POUNCE_COOLDOWN;
                        }
                        h.hp -= dmg; if (h.hp < 0) h.hp = 0;
                        // 受击闪烁（仅客户端渲染表现）
                        if (!ctx->is_server) {
                            registry.emplace_or_replace<HitFlashComponent>(target, HitFlashComponent{ 0.12f });
                        }
                        // 受击击退：往远离攻击者方向小跳（帧同步定点）
                        fp_t kdist = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                        if (kdist > fp_zero()) {
                            registry.emplace_or_replace<KnockbackComponent>(target, KnockbackComponent{
                                fp_mul(fp_div(dx, kdist), KNOCKBACK_SPEED),
                                fp_mul(fp_div(dz, kdist), KNOCKBACK_SPEED),
                                KNOCKBACK_TICKS
                            });
                        }
                        hit = true;
                        ndx = dx; ndz = dz;   // 命中玩家方向
                        break;
                    }
                }
                // 刀光朝向：命中玩家优先，其次最近存活玩家，无目标兜底 +Z
                if (hit || best < fp_from_float(1e9f)) {
                    fp_t dist = fp_sqrt(fp_add(fp_mul(ndx, ndx), fp_mul(ndz, ndz)));
                    if (dist > fp_zero()) { dir_fx = fp_div(ndx, dist); dir_fz = fp_div(ndz, dist); }
                }
                if (dir_fx == fp_zero() && dir_fz == fp_zero()) { dir_fz = fp_one(); }
                // 敌近战刀光特效（与玩家同套渲染路径，仅客户端）
                if (!ctx->is_server) {
                    ctx->attack_flashes.push_back({ apos.x, apos.z, dir_fx, dir_fz, range, true });
                }
            }

            // 玩家近战：不在此建渲染实体，仅把挥击特效请求写入渲染缓冲（仅客户端）。
            if (is_player && !ctx->is_server) {
                ctx->attack_flashes.push_back({ apos.x, apos.z, dir_fx, dir_fz, R, false });
                ctx->sfx_requests.push_back({ SFX_MELEE });
            }
        }

        // ================= 射击 =================
        if (want_bullet && act.bullet_cooldown <= 0) {
            act.bullet_cooldown = ActionStateComponent::BULLET_COOLDOWN;

            fp_t sdx = fp_zero(), sdz = fp_zero();
            if (is_player) {
                // 玩家射：发射瞬间锁定最近存活敌人为朝向（自动瞄准，水平方向）
                // 与敌射逻辑对称，完全确定性：最近者由 enemy_view 迭代 + 距离比较得出，双端一致
                entt::entity lock = entt::null;
                fp_t best = fp_from_float(1e9f);
                for (auto tg : enemy_view) {
                    auto& h = enemy_view.get<HealthComponent>(tg);
                    if (h.dead) continue;
                    auto& tp = enemy_view.get<LogicPositionComponent>(tg);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (d2 < best) { best = d2; lock = tg; }
                }
                if (lock != entt::null) {
                    auto& tp = enemy_view.get<LogicPositionComponent>(lock);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t dist = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                    if (dist > fp_zero()) { sdx = fp_div(dx, dist); sdz = fp_div(dz, dist); }
                }
                if (sdx == fp_zero() && sdz == fp_zero()) {
                    // 无存活敌人可瞄准时，退回沿玩家当前朝向发射
                    fp_t yaw_fp = fp_from_float(act.yaw);
                    sdx = fp_sin(yaw_fp);
                    sdz = fp_cos(yaw_fp);
                }
            } else {
                // 敌射：锁定最近存活玩家为朝向（自动瞄准，水平方向）
                entt::entity lock = entt::null;
                fp_t best = fp_from_float(1e9f);
                for (auto p : player_view) {
                    auto& h = player_view.get<HealthComponent>(p);
                    if (h.dead || h.spawn_invuln > 0) continue;
                    auto& tp = player_view.get<LogicPositionComponent>(p);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (d2 < best) { best = d2; lock = p; }
                }
                if (lock != entt::null) {
                    auto& tp = player_view.get<LogicPositionComponent>(lock);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t dist = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                    if (dist > fp_zero()) { sdx = fp_div(dx, dist); sdz = fp_div(dz, dist); }
                }
                if (sdx == fp_zero() && sdz == fp_zero()) { sdz = fp_one(); } // 兜底朝 +Z
            }

            auto e = registry.create();
            registry.emplace<BulletComponent>(e, BulletComponent{
                apos.x, apos.y, apos.z,
                sdx, sdz,
                BULLET_SPEED,
                BULLET_RANGE,
                is_player ? cd.bullet_damage : ENEMY_BULLET_DAMAGE,
                !is_player,   // from_enemy
                registry.all_of<IdComponent>(attacker)
                    ? registry.get<IdComponent>(attacker).id : -1
            });
            // 玩家射击音效（仅客户端本地表现）
            if (is_player && !ctx->is_server) {
                ctx->sfx_requests.push_back({ SFX_BULLET });
            }
        }

        // ================= 射线（hitscan，仅玩家） =================
        if (is_player && want_ray && act.ray_cooldown <= 0) {
            act.ray_cooldown = ActionStateComponent::RAY_COOLDOWN;

            // 朝yaw单位方向(定点)
            fp_t yaw_fp = fp_from_float(act.yaw);
            fp_t fx = fp_sin(yaw_fp);
            fp_t fz = fp_cos(yaw_fp);

            // 自动锁敌：优先锁定射程内最近的存活敌人，射线朝它打
            const fp_t RAY_RANGE  = cd.ray_range;

            entt::entity lock = entt::null;
            fp_t lock_t = fp_from_float(1e9f);
            fp_t lock_x = fp_zero(), lock_z = fp_zero();
            {
                fp_t best_r2 = fp_mul(RAY_RANGE, RAY_RANGE);
                for (auto tg : enemy_view) {
                    auto& h = enemy_view.get<HealthComponent>(tg);
                    if (h.dead) continue;
                    auto& tp = enemy_view.get<LogicPositionComponent>(tg);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t r2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (r2 < best_r2) { best_r2 = r2; lock = tg; lock_x = tp.x; lock_z = tp.z; }
                }
                // 锁到目标则把射线方向改向它（沿XZ重置方向，保持水平）
                if (lock != entt::null) {
                    fp_t dx = fp_sub(lock_x, apos.x), dz = fp_sub(lock_z, apos.z);
                    fp_t len = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                    if (len > fp_zero()) { fx = fp_div(dx, len); fz = fp_div(dz, len); }
                    lock_t = len;
                }
            }

            // 障碍阻挡：取射线方向上最近命中障碍的距离，截断 beam_reach（全程定点，双端一致）
            fp_t beam_reach = RAY_RANGE;
            {
                auto ob_view = registry.view<StaticMeshComponent,
                                             LogicPositionComponent, ColliderComponent>();
                ray3df_t oray = { { apos.x, apos.y, apos.z }, { fx, fp_zero(), fz } };
                for (auto ob : ob_view) {
                    auto& obp = ob_view.get<LogicPositionComponent>(ob);
                    auto& obc = ob_view.get<ColliderComponent>(ob);
                    if (obc.type != ColliderComponent::BOX
                            && obc.type != ColliderComponent::OBB) continue;
                    vec3f_t pos = { obp.x, obp.y, obp.z };
                    ray3d_collisionf_t c = collision3df_get_ray_obb(
                        oray, pos, obc.half_size, obc.rotation);
                    if (c.hit && c.distance > fp_zero() && c.distance < beam_reach)
                        beam_reach = c.distance;
                }
            }

            // 锁定目标被障碍完全遮挡(t ≥ beam_reach)则不命中
            if (lock != entt::null && lock_t >= beam_reach) lock = entt::null;

            if (lock != entt::null) {
                auto& hp = enemy_view.get<HealthComponent>(lock);
                hp.hp -= cd.ray_damage; if (hp.hp < 0) hp.hp = 0;
                // 受击闪烁（仅客户端渲染表现）
                if (!ctx->is_server) {
                    registry.emplace_or_replace<HitFlashComponent>(lock, HitFlashComponent{ 0.12f });
                }
                bool kill = false;
                if (hp.hp <= 0 && !hp.dead) {
                    hp.dead = true;
                    ++registry.ctx().get<GlobalState>().kills;  // 玩家射线击杀
                    kill = true;
                    if (!ctx->is_server) {
                        uint64_t now = get_ticks();
                        if (now - ctx->last_kill_ms <= 2000) ctx->kill_combo++;
                        else ctx->kill_combo = 1;
                        ctx->last_kill_ms = now;
                        ctx->kill_events.push_back({ now, ctx->kill_combo });
                        ctx->sfx_requests.push_back({ SFX_KILL });
                    }
                }
                // 受击击退：沿射线方向推（仅存活实体）
                if (!kill) {
                    registry.emplace_or_replace<KnockbackComponent>(lock, KnockbackComponent{
                        fp_mul(fx, KNOCKBACK_SPEED), fp_mul(fz, KNOCKBACK_SPEED), KNOCKBACK_TICKS
                    });
                }
                // 伤害飘字（仅客户端渲染）
                if (!ctx->is_server) {
                    ctx->damage_texts.push_back({ lock_x, lock_z, cd.ray_damage, kill });
                    ctx->sfx_requests.push_back({ SFX_HIT });
                }
            }

            // 射线特效请求（仅客户端渲染；光束长度=射线可达范围，障碍截断；命中敌人再截断到命中点）
            if (!ctx->is_server) {
                fp_t beam_len = beam_reach;
                if (lock != entt::null) beam_len = lock_t;
                ctx->ray_effects.push_back({ apos.x, apos.z, fx, fz, beam_len });
                ctx->sfx_requests.push_back({ SFX_BULLET });
            }
        }

        events.pending_actions.clear();
    }
}

#endif