#ifndef WAVE_SYSTEM_H
#define WAVE_SYSTEM_H
#include <joy/joy.h>
#include "context.h"
#include "components/global_state_component.h"
#include "components/enemy_component.h"
#include "components/health_component.h"
#include "components/id_component.h"
#include "components/logic_position_component.h"
#include "components/logic_rect_component.h"
#include "components/logic_velocity_component.h"
#include "components/transform_component.h"
#include "components/ai_component.h"
#include "components/mesh_component.h"
#include "components/action_state_component.h"
#include "components/spawn_point_component.h"
#include "components/player_component.h"
#include "components/static_mesh_component.h"
#include "components/collider_component.h"
#include "components/pathfinding_component.h"
#include "components/hit_flash_component.h"
#include "systems/spawn_def.h"
#include "procedural_map.h"
#include "level_maps.h"
#include "spawn_obstacles_system.h"
#include "components/wave_advance_component.h"

// 单关制波次系统（仅权威端：单机/Host/服务端）：
// - 开局一次性建图并刷出本关全部敌人（单波清场）
// - 战斗期间任何敌人存活则不动作
// - 清场后置 level_cleared，本关就此结束；不再自动切关、不再重生成地图。
//   通关/胜利由流程层检测 level_cleared 后进入通关界面。
// 注册在 player_death_system 之后：先完成本 tick 死亡判定，再结算波次。
// 注意：双端（服务端权威 + 客户端）都运行本系统以保证确定性模拟一致性。

// 敌人生成（一）：确定性准备整波敌人的出生格与类型索引（不创建实体）。
// 出生格优先取固定关卡地图的 E 出生点（level_maps.h，纯函数 → 双端逐位一致）；
// 仅当关卡无 E 标记时才回退到从布局开放格随机采样（防御性兜底，避免空出生点卡死）。
// 结果写入 tiles/si，双端逐位一致。
static int wave_prepare_enemies(const GlobalState& gs, int level, int* tiles, int* si)
{
    // 每关敌人数量 = 该关表值（单波清场，不再按波次递增）。钳制到上下限。
    int count = (level >= 1 && level <= LEVEL_COUNT) ? LEVEL_ENEMY_COUNT[level - 1] : 2;
    if (count < ENEMY_SPAWN_COUNT) count = ENEMY_SPAWN_COUNT;
    if (count > WAVE_MAX_ENEMIES) count = WAVE_MAX_ENEMIES;

    int player_cell = -1;
    int enemy_cells[WAVE_MAX_ENEMIES];
    int enemy_n = 0;
    PmLayout layout;
    level_build_layout(level, &layout, player_cell, enemy_cells, enemy_n);

    int got = 0;
    if (enemy_n > 0) {
        // 每波保证两种类型各出一个：前 ENEMY_SPAWN_COUNT 只覆盖所有类型，
        // 多余名额用 seed+波次+序号确定性补齐；E 格不足时循环复用同一批出生点
        //（horde 从固定出生点刷新，指数增长时位置仍确定）。
        for (int i = 0; i < count; i++) {
            tiles[got++] = enemy_cells[i % enemy_n];
            si[i] = (i < ENEMY_SPAWN_COUNT) ? i
                    : ((int)gs.map_seed + gs.wave * 7 + i * 13) % ENEMY_SPAWN_COUNT;
        }
    } else {
        // 兜底：从布局开放格随机采样（确定性 seed，不依赖共享随机流）
        random_num_p rng = utils_random_create(
            (uint32_t)gs.map_seed + 0x9E3779B9u * (uint32_t)gs.wave);
        got = pm_sample_open_tiles(rng, layout, count, WAVE_ENEMY_MIN_DIST10, tiles);
        utils_random_destroy(rng);
        for (int i = 0; i < got; i++)
            si[i] = (i < ENEMY_SPAWN_COUNT) ? i
                   : ((int)gs.map_seed + gs.wave * 7 + i * 13) % ENEMY_SPAWN_COUNT;
    }
    return got;
}

// 生成（三）：从对象池取一个死亡敌人复用于本轮出生（按 Id 最小取，双端确定性一致）。
// 依赖确定性模拟：双端的死亡敌人集合与 id 序逐位一致，故复用对象在两端完全对齐，
// 不会破坏实体 ID 计数同步（只有"新建"才调用 get_entity_id）。
static entt::entity enemy_take_dead(Context* ctx)
{
    entt::entity best = entt::null;
    auto view = ctx->ecs.view<EnemyComponent, HealthComponent, IdComponent>();
    for (auto e : view) {
        if (!view.get<HealthComponent>(e).dead) continue;
        auto& id = view.get<IdComponent>(e);
        if (best == entt::null || id.id < view.get<IdComponent>(best).id)
            best = e;
    }
    return best;
}

// 生成（四）：把敌人实体重置为"刚出生"状态（复用或新建共用）。
// 复用自上一波的实体可能残留 PathComponent/HitFlash 等瞬时组件，统一清除；核心组件逐一覆写，
// 两端经相同函数重置后状态逐位一致，帧同步不受影响。
static void enemy_reset(Context* ctx, entt::entity e, const SpawnDef& sd, int si, fp_t fx, fp_t fz)
{
    auto& reg = ctx->ecs;
    reg.emplace_or_replace<LogicRectComponent>(e, fp_from_float(0.5f), fp_from_float(1.0f));
    reg.emplace_or_replace<LogicPositionComponent>(e, fx, fp_zero(), fz);
    reg.emplace_or_replace<LogicVelocityComponent>(e, fp_zero(), fp_zero(), fp_zero());
    ColliderComponent ecol;
    ecol.type = ColliderComponent::BOX;
    ecol.half_size = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
    reg.emplace_or_replace<ColliderComponent>(e, ecol);
    reg.emplace_or_replace<TransformComponent>(e,
        vec3_t{ fp_to_float(fx), 0.0f, fp_to_float(fz) }, 0.0f,
        vec3_t{ 1.0f, 1.0f, 1.0f });
    reg.emplace_or_replace<ActionStateComponent>(e);
    reg.emplace_or_replace<HealthComponent>(e, ENEMY_MAX_HP, ENEMY_MAX_HP);
    reg.emplace_or_replace<SpawnPointComponent>(e, fx, fp_zero(), fz);
    MeshComponent mc;
    mc.mesh = ctx->mesh_enemy;
    mc.wire_mesh = ctx->mesh_enemy_wire;
    mc.color = { 22, 22, 26, 255 };            // 深填充
    mc.stroke_color = { 255, 255, 255, 255 };  // 白描边
    reg.emplace_or_replace<MeshComponent>(e, mc);
    reg.emplace_or_replace<SpawnIdxComponent>(e, si);
    reg.emplace_or_replace<AIComponent>(e,
        fx, fz,   // home = 实际出生点（客户端重建也用快照位置）
        fp_from_float(sd.patrol_radius),
        fp_from_float(sd.detect_radius),
        fp_from_float(sd.attack_radius),
        fp_from_float(sd.guard_radius));
    if (reg.all_of<PathComponent>(e))        reg.remove<PathComponent>(e);
    if (reg.all_of<HitFlashComponent>(e))    reg.remove<HitFlashComponent>(e);
}

// 敌人生成（二）：创建/复用 [start, end) 这批敌人实体。
// 优先重置对象池中的死亡敌人，池子不足时才新建并分配新 id；两端按相同 id 序逐位一致。
static void wave_spawn_enemy_range(Context* ctx, const int* tiles, const int* si,
                                   int start, int end)
{
    for (int i = start; i < end; i++) {
        const SpawnDef& sd = ENEMY_SPAWNS[si[i]];
        int gx = tiles[i] % PF_GRID_N, gz = tiles[i] / PF_GRID_N;
        // 出生点世界坐标 = 格心 (gx-OFFSET)+0.5。此值同时写入 LogicPosition/SpawnPoint/AI.home，
        // 双端（服务端权威 + 客户端）各自运行本系统，必须定点逐位一致（帧同步确定性），不许 float 运算。
        fp_t fx = fp_add(fp_from_int(gx - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        fp_t fz = fp_add(fp_from_int(gz - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        entt::entity e = enemy_take_dead(ctx);
        if (e == entt::null) {
            e = ctx->ecs.create();
            ctx->ecs.emplace<IdComponent>(e, get_entity_id(ctx));
            ctx->ecs.emplace<EnemyComponent>(e);
        }
        enemy_reset(ctx, e, sd, si[i], fx, fz);
    }
}

// 真销毁全部敌人实体（换关时清空，跨关不沿用对象池；双端同序 get_entity_id → ID 对齐）
static void destroy_all_enemies(entt::registry& registry)
{
    std::vector<entt::entity> to_destroy;
    auto view = registry.view<EnemyComponent>();
    for (auto e : view) to_destroy.push_back(e);
    for (auto e : to_destroy) registry.destroy(e);
}

// 确定性建关：输入仅 level，双端（服务端权威 + 客户端）跑同一流程 →
// 障碍/敌人/玩家位置逐位一致，不通过网络下发实体数组，保证帧同步。
static void apply_level_change(Context* ctx, entt::registry& reg, int new_level)
{
    destroy_all_enemies(reg);
    destroy_obstacles(reg);
    auto& gs = reg.ctx().get<GlobalState>();
    gs.level = new_level;
    gs.wave = 0;
    gs.level_cleared = false;
    gs.map_wave_generated = -1;
    gs.map_seed = new_level;

    // 重建障碍（10000+ 固定 ID 起，与 spawn_obstacles_from_layout 一致）。
    PmLayout layout;
    int player_cell = -1;
    int enemy_cells[WAVE_MAX_ENEMIES];
    int enemy_n = 0;
    if (!level_build_layout(new_level, &layout, player_cell, enemy_cells, enemy_n)) {
        log_info("[wave] level=%d 手绘建图失败，回退随机生成", new_level);
        pmap_generate(ctx->random, &layout);
    }
    int oid = 10000;
    spawn_obstacles_from_layout(reg, ctx, layout, oid);

    // 玩家全部重定位到新关出生点（level_player_spawn，纯函数双端一致）。
    fp_t fx, fz;
    level_player_spawn(new_level, &fx, &fz);
    auto pv = reg.view<PlayerComponent, LogicPositionComponent, TransformComponent>();
    for (auto e : pv) {
        auto& lpos = pv.get<LogicPositionComponent>(e);
        lpos.x = fx; lpos.z = fz;
        reg.emplace_or_replace<SpawnPointComponent>(e, fx, fp_zero(), fz);
        auto& tf = pv.get<TransformComponent>(e);
        tf.position = vec3_t{ fp_to_float(fx), fp_to_float(lpos.y), fp_to_float(fz) };
    }

    // 刷第一批敌（单波清场，直接生成，不再走 WaveAdvance 延迟）。
    int tiles[WAVE_MAX_ENEMIES], si[WAVE_MAX_ENEMIES];
    int got = wave_prepare_enemies(gs, new_level, tiles, si);
    wave_spawn_enemy_range(ctx, tiles, si, 0, got);
    gs.wave = 1;

    ctx->sfx_requests.push_back({ SFX_WAVE });
    log_info("[wave] level=%d map_seed=%d obstacles=%d enemies=%d", gs.level, gs.map_seed, layout.count, got);
}

// 单关推进（仅权威端：单机/Host/服务端）：
// - 开局：一次性刷出本关敌人（建图 + 生成）
// - 战斗：仍有存活敌人则不动
// - 清场：置 level_cleared，本关自此结束，不再自动切关（单关制）。
//   通关/胜利由流程层检测清场后进入通关界面，本系统不再推进。
static void wave_system(entt::registry& registry, Context* ctx, float)
{
    if (!ctx->is_authority) return;
    if (!ctx->random) return;
    auto& gs = registry.ctx().get<GlobalState>();

    // 首次：本关尚未开局 → 建图 + 刷第一批敌
    if (gs.wave == 0 && !gs.level_cleared) {
        apply_level_change(ctx, registry, gs.level);
        return;
    }

    // 战斗中：仍有存活敌人
    bool wave_done = true;
    auto eview = registry.view<EnemyComponent, HealthComponent>();
    for (auto e : eview) {
        if (!eview.get<HealthComponent>(e).dead) { wave_done = false; break; }
    }
    if (!wave_done) return;

    // 清场完成：本关已结束，置位标记；单关制，绝不推进下一关。
    if (!gs.level_cleared) {
        gs.level_cleared = true;
    }
}

#endif
