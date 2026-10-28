#ifndef KNOCKBACK_SYSTEM_H
#define KNOCKBACK_SYSTEM_H

#include "../components/knockback_component.h"
#include "../components/logic_position_component.h"
#include "../components/collider_component.h"
#include "../components/static_mesh_component.h"
#include <joy/joy.h>

struct Context;

// 击退参数（纯定点，帧同步确定性）
static const fp_t KNOCKBACK_SPEED = fp_from_float(0.08f);  // 米/tick（约 2.4 m/s @ 30Hz）
static const int  KNOCKBACK_TICKS = 6;                     // 0.2 秒，总位移约 0.48 米（小跳）

// 检测 (nx,nz) 位置是否与静态障碍碰撞（GJK，复用 collision_system 的构建逻辑）。
// static_view 仅含 StaticMeshComponent（静态障碍），玩家/敌人无该组件，天然被排除。
template <typename StaticView>
static bool knockback_blocked(entt::registry& registry, StaticView& static_view,
                              entt::entity e, const LogicPositionComponent& pos,
                              fp_t nx, fp_t nz, const vec3f_t& hs)
{
    (void)registry;
    gjk3df_collider_t shape;
    vec3f_t cpos = { nx, pos.y, nz };
    gjk3df_init_box(&shape, cpos, hs);

    for (auto o : static_view) {
        if (o == e) continue;
        auto& och = static_view.template get<ColliderComponent>(o);
        auto& opos = static_view.template get<LogicPositionComponent>(o);
        gjk3df_collider_t other_shape;
        vec3f_t oposv = { opos.x, opos.y, opos.z };
        if (och.type == ColliderComponent::SPHERE) {
            gjk3df_init_sphere(&other_shape, oposv, och.radius);
        } else if (och.type == ColliderComponent::OBB) {
            gjk3df_init_obb(&other_shape, oposv, och.half_size, och.rotation);
        } else if (och.type == ColliderComponent::MESH) {
            gjk3df_init_mesh(&other_shape, och.verts, och.verts_num);
        } else {
            gjk3df_init_box(&other_shape, oposv, och.half_size);
        }
        gjk3df_contact_t contact;
        vec3f_t init_dir = { fp_one(), fp_zero(), fp_zero() };
        if (gjk3df_collide(&shape, &other_shape, init_dir, &contact)) return true;
    }
    return false;
}

// 受击击退 system：沿 (vx,vz) 每 tick 位移，分轴碰撞检测——被墙挡住的轴不移动，
// 另一轴仍可移动（贴墙沿墙滑动），避免击退穿墙。tick 数耗尽后移除组件。
// 注册顺序：movement_system 之后（叠加在玩家/敌人移动之上）、collision_system 之前。
static void knockback_system(entt::registry& registry, Context* /*ctx*/, float /*dt*/)
{
    auto view = registry.view<KnockbackComponent, LogicPositionComponent>();
    auto static_view = registry.view<StaticMeshComponent, ColliderComponent, LogicPositionComponent>();

    for (auto e : view) {
        auto& kb = view.get<KnockbackComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);

        // 实体碰撞体尺寸（玩家/敌人均为 BOX）
        vec3f_t hs = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
        if (registry.all_of<ColliderComponent>(e)) {
            auto& col = registry.get<ColliderComponent>(e);
            if (col.type == ColliderComponent::BOX) hs = col.half_size;
        }

        // 分轴检测：X 轴被挡则不移动 X，Z 轴被挡则不移动 Z
        fp_t nx = fp_add(pos.x, kb.vx);
        if (!knockback_blocked(registry, static_view, e, pos, nx, pos.z, hs)) pos.x = nx;
        fp_t nz = fp_add(pos.z, kb.vz);
        if (!knockback_blocked(registry, static_view, e, pos, pos.x, nz, hs)) pos.z = nz;

        kb.ticks_left--;
        if (kb.ticks_left <= 0) registry.remove<KnockbackComponent>(e);
    }
}

#endif
