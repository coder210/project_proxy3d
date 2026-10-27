#ifndef COLLISION_SYSTEM_H
#define COLLISION_SYSTEM_H
#include "../components/enemy_component.h"
#include "../components/logic_position_component.h"
#include "../components/collider_component.h"
#include "../components/player_component.h"
#include "../components/health_component.h"
#include "../components/static_mesh_component.h"
#include <joy/joy.h>

struct Context;

// 3D 碰撞处理 system
// 在 OnUpdate 阶段运行，在 movement_system 之后
// 检测所有玩家与地图碰撞层的碰撞，修正玩家位置（XZ 平面推开，Y 保持贴地）

// 贴合皮肤余量（米）：推开墙时额外留一点点余量，让玩家彻底脱离碰撞体接触面，
// 避免离散推进+瞬时驳回在"恰好接触"位形上每帧反复进-退造成的抖动。
#ifndef COLLISION_PAD
#define COLLISION_PAD 0.03f
#endif

static void collision_system(entt::registry& registry, Context* ctx, float dt) {
    (void)ctx;
    (void)dt;

    auto player_view = registry.view<PlayerComponent, ColliderComponent, LogicPositionComponent>();
    auto static_view = registry.view<ColliderComponent, LogicPositionComponent>();

    for (auto player_entity : player_view) {
        auto& player_col = player_view.get<ColliderComponent>(player_entity);
        auto& player_pos = player_view.get<LogicPositionComponent>(player_entity);

        if (player_col.type != ColliderComponent::BOX) continue;

        gjk3df_collider_t player_shape;
        vec3f_t ppos = { player_pos.x, player_pos.y, player_pos.z };
        vec3f_t phs  = { player_col.half_size.x, player_col.half_size.y, player_col.half_size.z };
        gjk3df_init_box(&player_shape, ppos, phs);

        for (auto other_entity : static_view) {
            if (other_entity == player_entity) continue;
            // 玩家之间、玩家与敌人之间也互相推开，避免相互穿透
            if (registry.all_of<PlayerComponent>(other_entity)) {
                // 仅当 other 的 entity id 大于当前玩家时才处理，
                // 保证帧同步两端按一致顺序两两分离，避免双推导致顺序依赖
                if ((uint32_t)other_entity <= (uint32_t)player_entity) continue;
            }
            if (registry.all_of<EnemyComponent>(other_entity)) {
                // 敌人视为不可推动的障碍，仅把玩家推开
                // 死亡敌人不参与碰撞（波次结束时统一销毁，避免尸体在换图前短暂挡路）
                if (registry.all_of<HealthComponent>(other_entity) &&
                    registry.get<HealthComponent>(other_entity).dead) continue;
            }

            auto& other_col = static_view.get<ColliderComponent>(other_entity);
            auto& other_pos = static_view.get<LogicPositionComponent>(other_entity);

            if (other_col.type != ColliderComponent::BOX &&
                other_col.type != ColliderComponent::SPHERE &&
                other_col.type != ColliderComponent::OBB &&
                other_col.type != ColliderComponent::MESH) continue;

            gjk3df_collider_t other_shape;
            vec3f_t opos = { other_pos.x, other_pos.y, other_pos.z };
            if (other_col.type == ColliderComponent::SPHERE) {
                gjk3df_init_sphere(&other_shape, opos, other_col.radius);
            } else if (other_col.type == ColliderComponent::OBB) {
                vec3f_t ohs  = { other_col.half_size.x, other_col.half_size.y, other_col.half_size.z };
                gjk3df_init_obb(&other_shape, opos, ohs, other_col.rotation);
            } else if (other_col.type == ColliderComponent::MESH) {
                // 真实世界顶点（与渲染几何一致），GJK 直接对凸包检测
                gjk3df_init_mesh(&other_shape, other_col.verts, other_col.verts_num);
            } else {
                vec3f_t ohs  = { other_col.half_size.x, other_col.half_size.y, other_col.half_size.z };
                gjk3df_init_box(&other_shape, opos, ohs);
            }

            // 初判方向：用玩家到该碰撞体的水平方向，贴近真实分离方向，
            // 让 GJK 首轮支撑点就落到正确面上，收敛更快、normal 更不易被数值位形扰动。
            vec3f_t d0 = { other_pos.x - ppos.x, fp_zero(), other_pos.z - ppos.z };
            fp_t d0len2 = fp_add(fp_mul(d0.x, d0.x), fp_mul(d0.z, d0.z));
            vec3f_t init_dir;
            if (d0len2 > fp_from_float(1e-10f)) {
                fp_t inv = fp_div(fp_one(), fp_sqrt(d0len2));
                init_dir = { fp_mul(d0.x, inv), fp_zero(), fp_mul(d0.z, inv) };
            } else {
                init_dir = { fp_one(), fp_zero(), fp_zero() };
            }

            gjk3df_contact_t contact;
            if (gjk3df_collide(&player_shape, &other_shape, init_dir, &contact)) {
                // gjk3df_collide(c1=player, c2=other) 返回的 normal 方向为 player→other，
                // 需沿 -normal（other→player）把玩家推离静态物，否则会越推越进去导致穿透。
                fp_t depth = contact.depth;
                if (depth < fp_zero()) depth = fp_sub(fp_zero(), depth);

                // 只在 XZ 水平平面推开（Y 交由贴地逻辑/垂直物理管理），
                // 避免法线含微小的 Y 分量把玩家抬离地面 → 参考方向被反复拉回造成上下捣动。
                fp_t n2 = fp_add(fp_mul(contact.normal.x, contact.normal.x),
                                 fp_mul(contact.normal.z, contact.normal.z));
                if (n2 <= fp_from_float(1e-8f)) continue;   // 法线近乎竖直（顶/底面），不水平推开

                fp_t inv = fp_div(fp_one(), fp_sqrt(n2));
                fp_t dxf = fp_mul(contact.normal.x, inv);   // 水平归一化法线（player→other）
                fp_t dzf = fp_mul(contact.normal.z, inv);

                bool is_wall = registry.all_of<StaticMeshComponent>(other_entity);
                fp_t pad = is_wall ? fp_from_float(COLLISION_PAD) : fp_zero();
                fp_t push = fp_add(depth, pad);

                player_pos.x = fp_sub(player_pos.x, fp_mul(dxf, push));
                player_pos.z = fp_sub(player_pos.z, fp_mul(dzf, push));

                // 更新玩家形状以继续检测
                ppos.x = player_pos.x;
                ppos.y = player_pos.y;
                ppos.z = player_pos.z;
                gjk3df_init_box(&player_shape, ppos, phs);
            }
        }
    }
}

#endif
