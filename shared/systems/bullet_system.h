#ifndef BULLET_SYSTEM_H
#define BULLET_SYSTEM_H

#include "../components/bullet_component.h"
#include "../components/player_component.h"
#include "../components/enemy_component.h"
#include "../components/health_component.h"
#include "../components/id_component.h"
#include "../components/logic_position_component.h"
#include "../components/collider_component.h"
#include "../components/static_mesh_component.h"
#include "../components/global_state_component.h"
#include "../components/hit_flash_component.h"
#include "../components/knockback_component.h"
#include "knockback_system.h"
#include <joy/joy.h>

struct Context;

// 子弹碰撞半径（米，球体）
static const fp_t BULLET_RADIUS = fp_from_float(0.15f);

// 球体 (c, r) 与轴对齐盒 [bc-ths, bc+ths] 的定点 O(1) 判定：
// 求球心到盒的最短点，比较距离平方 ≤ r²。替代昂贵的 GJK（玩家/敌人/地图障碍均为轴对齐盒体）。
// 全程定点运算，两端结果一致（确定性），无网络同步需要。
static inline bool bullet_hit_box(const fp_t c[3], fp_t r,
                                  const fp_t bc[3], const fp_t ths[3])
{
    fp_t closest[3];
    for (int k = 0; k < 3; k++) {
        fp_t lo = fp_sub(bc[k], ths[k]);
        fp_t hi = fp_add(bc[k], ths[k]);
        closest[k] = c[k] < lo ? lo : (c[k] > hi ? hi : c[k]);
    }
    fp_t dx = fp_sub(c[0], closest[0]);
    fp_t dy = fp_sub(c[1], closest[1]);
    fp_t dz = fp_sub(c[2], closest[2]);
    fp_t d2 = fp_add(fp_add(fp_mul(dx, dx), fp_mul(dy, dy)), fp_mul(dz, dz));
    return d2 <= fp_mul(r, r);
}

// 子弹逻辑 system（敌我共用）：
// - 沿 (dx,dz) 以 speed 米/tick 移动，距离耗尽即销毁
// - 用定点数 GJK3D 球 vs 目标盒体 判定：玩家子弹命中敌人、敌人子弹命中玩家
// - 命中造成伤害并销毁；命中静态障碍（StaticMeshComponent）也销毁（子弹不穿墙）
// - 全程定点数 + 确定性派生，两端结果一致，无需网络同步
// 注册顺序：melee_attack_system 之后（子弹在攻击结算时创建），cleanup_system 之前。
static void bullet_system(entt::registry& registry, Context* ctx, float dt)
{
    (void)dt;
    auto bullet_view = registry.view<BulletComponent>();

    // 目标视图（player/enemy + 位置/血量）
    auto player_v = registry.view<PlayerComponent, LogicPositionComponent, HealthComponent>();
    auto enemy_v  = registry.view<EnemyComponent,  LogicPositionComponent, HealthComponent>();
    // 静态障碍（StaticMeshComponent 独有的边角箱子/墙）
    auto static_v = registry.view<StaticMeshComponent, ColliderComponent, LogicPositionComponent>();

    std::vector<entt::entity> to_destroy;

    for (auto b : bullet_view) {
        auto& bl = bullet_view.get<BulletComponent>(b);

        // 1) 移动
        bl.x = fp_add(bl.x, fp_mul(bl.dx, bl.speed));
        bl.z = fp_add(bl.z, fp_mul(bl.dz, bl.speed));
        bl.range_left = fp_sub(bl.range_left, bl.speed);
        if (bl.range_left <= fp_zero()) { to_destroy.push_back(b); continue; }

        // 子弹球体（顶点/半宽均为定点）
        gjk3df_collider_t bshape;
        vec3f_t bpos = { bl.x, bl.y, bl.z };
        gjk3df_init_sphere(&bshape, bpos, BULLET_RADIUS);

        bool hit = false;

        // 2) 命中目标（按子弹来源选目标侧；player_v / enemy_v 类型不同，用模板统一处理）
        // 目标均为轴对齐盒体 → O(1) 定点球-盒判定（替代逐目标 GJK，显著降低每 tick 开销）
        auto try_hit_targets = [&](const auto& target_v) -> bool {
            for (auto t : target_v) {
                auto& h  = target_v.template get<HealthComponent>(t);
                if (h.dead) continue;
                // 敌我共用同一套伤害协议：敌人子弹不伤害射手自身（点射时的贴脸自伤）
                if (registry.all_of<IdComponent>(t) &&
                    registry.get<IdComponent>(t).id == bl.owner_id) continue;
                // 玩家无敌保护期内免疫
                if (bl.from_enemy && h.spawn_invuln > 0) continue;

                auto& tp = target_v.template get<LogicPositionComponent>(t);
                vec3f_t ths = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
                if (registry.all_of<ColliderComponent>(t)) {
                    auto& tc = registry.get<ColliderComponent>(t);
                    if (tc.type == ColliderComponent::BOX) ths = tc.half_size; // 目标均为盒体
                }
                fp_t bc[3] = { tp.x, tp.y, tp.z };
                fp_t hs[3] = { ths.x, ths.y, ths.z };
                fp_t cc[3] = { bl.x, bl.y, bl.z };
                if (bullet_hit_box(cc, BULLET_RADIUS, bc, hs)) {
                    h.hp -= bl.damage; if (h.hp < 0) h.hp = 0;
                    // 受击闪烁（仅客户端渲染表现）
                    if (!ctx->is_server) {
                        registry.emplace_or_replace<HitFlashComponent>(t, HitFlashComponent{ 0.12f });
                    }
                    bool kill = false;
                    if (h.hp <= 0 && !h.dead) {
                        h.dead = true;
                        // 仅玩家子弹击杀敌人计入击杀；敌人子弹杀伤玩家不计
                        if (!bl.from_enemy) ++registry.ctx().get<GlobalState>().kills;
                        kill = true;
                        // 击杀反馈（仅客户端本地表现）：2 秒窗口内连续击杀递增连杀
                        if (!bl.from_enemy && !ctx->is_server) {
                            uint64_t now = get_ticks();
                            if (now - ctx->last_kill_ms <= 2000) ctx->kill_combo++;
                            else ctx->kill_combo = 1;
                            ctx->last_kill_ms = now;
                            ctx->kill_events.push_back({ now, ctx->kill_combo });
                            ctx->sfx_requests.push_back({ SFX_KILL });
                        }
                    }
                    // 受击击退：沿子弹飞行方向小跳（仅存活实体，帧同步定点）
                    if (!kill) {
                        registry.emplace_or_replace<KnockbackComponent>(t, KnockbackComponent{
                            fp_mul(bl.dx, KNOCKBACK_SPEED), fp_mul(bl.dz, KNOCKBACK_SPEED), KNOCKBACK_TICKS
                        });
                    }
                    // 玩家子弹命中敌人：伤害飘字 + 命中音效（仅客户端看；渲染 pass 消费）
                    if (!bl.from_enemy && !ctx->is_server) {
                        ctx->damage_texts.push_back({ tp.x, tp.z, bl.damage, kill });
                        ctx->sfx_requests.push_back({ SFX_HIT });
                    }
                    return true; // 已命中，销毁本子弹
                }
            }
            return false;
        };

        if (bl.from_enemy) {
            if (try_hit_targets(player_v)) { to_destroy.push_back(b); continue; }
        } else {
            if (try_hit_targets(enemy_v))  { to_destroy.push_back(b); continue; }
        }

        // 3) 命中静态障碍 → 销毁（子弹不穿墙；两端同一障碍布局 → 确定性一致）
        for (auto o : static_v) {
            auto& och = static_v.get<ColliderComponent>(o);
            auto& opos = static_v.get<LogicPositionComponent>(o);
            bool blocked = false;
            if (och.type == ColliderComponent::BOX) {
                // 常见障碍均为轴对齐盒体 → O(1) 定点球-盒判定
                fp_t bc[3] = { opos.x, opos.y, opos.z };
                fp_t hs[3] = { och.half_size.x, och.half_size.y, och.half_size.z };
                fp_t cc[3] = { bl.x, bl.y, bl.z };
                blocked = bullet_hit_box(cc, BULLET_RADIUS, bc, hs);
            }
            else if (och.type == ColliderComponent::SPHERE) {
                // AABB 粗筛：超出粗盒范围直接跳过，避免无关 GJK
                fp_t bc[3] = { opos.x, opos.y, opos.z };
                fp_t hs[3] = { och.radius, och.radius, och.radius };
                fp_t cc[3] = { bl.x, bl.y, bl.z };
                if (!bullet_hit_box(cc, BULLET_RADIUS, bc, hs)) { blocked = false; }
                else {
                    gjk3df_collider_t oshape;
                    vec3f_t oc = { opos.x, opos.y, opos.z };
                    gjk3df_init_sphere(&oshape, oc, och.radius);
                    gjk3df_contact_t contact;
                    vec3f_t init_dir = { bl.dx, fp_zero(), bl.dz };
                    blocked = gjk3df_collide(&bshape, &oshape, init_dir, &contact) ? true : false;
                }
            }
            else if (och.type == ColliderComponent::OBB) {
                gjk3df_collider_t oshape;
                vec3f_t oc = { opos.x, opos.y, opos.z };
                vec3f_t ohs = { och.half_size.x, och.half_size.y, och.half_size.z };
                gjk3df_init_obb(&oshape, oc, ohs, och.rotation);
                gjk3df_contact_t contact;
                vec3f_t init_dir = { bl.dx, fp_zero(), bl.dz };
                blocked = gjk3df_collide(&bshape, &oshape, init_dir, &contact) ? true : false;
            }
            else if (och.type == ColliderComponent::MESH) {
                gjk3df_collider_t oshape;
                vec3f_t oc = { opos.x, opos.y, opos.z };
                gjk3df_init_mesh(&oshape, och.verts, och.verts_num);
                gjk3df_contact_t contact;
                vec3f_t init_dir = { bl.dx, fp_zero(), bl.dz };
                blocked = gjk3df_collide(&bshape, &oshape, init_dir, &contact) ? true : false;
            }
            if (blocked) { hit = true; break; }
        }

        if (hit) to_destroy.push_back(b);
    }

    for (auto e : to_destroy) registry.destroy(e);
}

#endif