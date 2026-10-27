// Headless 逻辑系统性能基准：隔离测量 AI/A*/子弹等逻辑系统在多种场景下的单帧耗时。
// 只构造 entt::registry 数据，不创建窗口/渲染，用于定位"加功能后帧率不稳"的 CPU 热点。
#include <chrono>
#include <cstdio>
#include "entt.hpp"
#include <joy/joy.h>
#include "../shared/components/global_state_component.h"
#include "../shared/components/enemy_component.h"
#include "../shared/components/ai_component.h"
#include "../shared/components/player_component.h"
#include "../shared/components/logic_position_component.h"
#include "../shared/components/health_component.h"
#include "../shared/components/id_component.h"
#include "../shared/components/action_state_component.h"
#include "../shared/components/pathfinding_component.h"
#include "../shared/components/bullet_component.h"
#include "../shared/components/player_event_input.h"
#include "../shared/components/static_mesh_component.h"
#include "../shared/components/collider_component.h"
#include "../shared/systems/ai_system.h"
#include "../shared/systems/pathfinding_system.h"
#include "../shared/systems/bullet_system.h"

struct Context;

using Clock = std::chrono::steady_clock;

// 世界坐标 → 世界坐标（float，用于布置场景）
static entt::entity make_obstacle(entt::registry& r, float cx, float cz, float hx, float hz, int id)
{
    entt::entity e = r.create();
    r.emplace<IdComponent>(e, id);
    r.emplace<LogicPositionComponent>(e, fp_from_float(cx), fp_from_float(1.2f), fp_from_float(cz));
    ColliderComponent col;
    col.type = ColliderComponent::BOX;
    col.half_size = { fp_from_float(hx), fp_from_float(hx), fp_from_float(hz) };
    r.emplace<ColliderComponent>(e, col);
    r.emplace<StaticMeshComponent>(e); // 不建渲染网格
    return e;
}

static entt::entity make_enemy(entt::registry& r, float x, float z, int id)
{
    entt::entity e = r.create();
    r.emplace<IdComponent>(e, id);
    r.emplace<EnemyComponent>(e);
    r.emplace<LogicPositionComponent>(e, fp_from_float(x), fp_from_float(0.5f), fp_from_float(z));
    r.emplace<ActionStateComponent>(e);
    r.emplace<HealthComponent>(e, HealthComponent(50, 50));
    r.emplace<PathComponent>(e);
    AIComponent ai;
    ai.patrol_center_x = fp_from_float(x);
    ai.patrol_center_z = fp_from_float(z);
    ai.patrol_radius = fp_from_float(3.0f);
    ai.detect_radius = fp_from_float(8.0f);
    ai.attack_radius = fp_from_float(1.2f);
    ai.guard_radius = fp_zero();          // 全局型：全图追击
    ai.state = 0;
    ai.timer = 120;
    r.emplace<AIComponent>(e, ai);
    return e;
}

static entt::entity make_player(entt::registry& r, float x, float z, int conv)
{
    entt::entity e = r.create();
    r.emplace<PlayerComponent>(e, conv);
    r.emplace<LogicPositionComponent>(e, fp_from_float(x), fp_from_float(0.5f), fp_from_float(z));
    r.emplace<HealthComponent>(e, HealthComponent(100, 100));
    return e;
}

static entt::entity make_bullet(entt::registry& r, float x, float z, int owner)
{
    entt::entity e = r.create();
    BulletComponent bl;
    bl.x = fp_from_float(x); bl.y = fp_from_float(0.5f); bl.z = fp_from_float(z);
    bl.dx = fp_from_float(0.0f); bl.dz = fp_from_float(1.0f);
    bl.speed = fp_from_float(0.4f);
    bl.range_left = fp_from_float(12.0f);
    bl.damage = 10; bl.from_enemy = true; bl.owner_id = owner;
    r.emplace<BulletComponent>(e, bl);
    return e;
}

// 计时并打印某个系统在 N 次调用下的平均耗时 (us)
template <typename Fn>
static void bench(const char* name, int N, Fn fn)
{
    // 预热
    for (int i = 0; i < 200; i++) fn();
    auto t0 = Clock::now();
    for (int i = 0; i < N; i++) fn();
    auto t1 = Clock::now();
    double avg_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / (double)N;
    printf("[Bench] %-28s avg=%8.1f us/tick\n", name, avg_us);
}

int main()
{
    Context* ctx = nullptr;

    // ============ 场景 0：空场景（基线） ============
    {
        entt::registry r;
        r.ctx().emplace<GlobalState>();
        bench("0-empty ai+pf+bullet", 5000, [&] {
            ai_system(r, ctx, 0.0f);
            pathfinding_system(r, ctx, 0.0f);
            bullet_system(r, ctx, 0.0f);
        });
    }

    // ============ 场景 1：60 障碍 + 40 敌人追击 + 玩家移动（A* 密集重算） ============
    {
        entt::registry r;
        r.ctx().emplace<GlobalState>();
        // 稀疏 60 障碍墙（x 方向排列），障碍高度 map，敌人目标远端
        for (int i = 0; i < 15; i++) {
            make_obstacle(r, -6.0f + i * 1.2f, -4.0f, 0.5f, 0.5f, 10000 + i);
            make_obstacle(r, -6.0f + i * 1.2f,  4.0f, 0.5f, 0.5f, 20000 + i);
            make_obstacle(r, -4.0f, -6.0f + i * 1.2f, 0.5f, 0.5f, 30000 + i);
            make_obstacle(r,  4.0f, -6.0f + i * 1.2f, 0.5f, 0.5f, 40000 + i);
        }
        entt::registry& rr = r;
        auto player = make_player(rr, 6.0f, 6.0f, 1);
        int ecount = 40;
        entt::registry* pr = &r;
        for (int i = 0; i < ecount; i++)
            make_enemy(r, -6.0f + (i % 8), 0.5f + (i / 8), 50000 + i);

        // 玩家每 tick 移动来触发 dirty → A* 持续重算
        bench("1-40enemies chase (A* heavy)", 3000, [&] {
            auto& pl = pr->get<LogicPositionComponent>(player);
            // 轻微移动换格（摇摆在两个格中心），驱动 dirty 重算
            static int flip = 0;
            float ox = fp_to_float(pl.x);
            pl.x = fp_from_float(ox < 7.0f ? 7.0f : 6.0f);
            ai_system(*pr, ctx, 0.0f);
            pathfinding_system(*pr, ctx, 0.0f);
            bullet_system(*pr, ctx, 0.0f);
        });
    }

    // ============ 场景 2：同场景 + 50 发在飞子弹 ============
    {
        entt::registry r;
        r.ctx().emplace<GlobalState>();
        for (int i = 0; i < 15; i++) {
            make_obstacle(r, -6.0f + i * 1.2f, -4.0f, 0.5f, 0.5f, 10000 + i);
            make_obstacle(r, -6.0f + i * 1.2f,  4.0f, 0.5f, 0.5f, 20000 + i);
            make_obstacle(r, -4.0f, -6.0f + i * 1.2f, 0.5f, 0.5f, 30000 + i);
            make_obstacle(r,  4.0f, -6.0f + i * 1.2f, 0.5f, 0.5f, 40000 + i);
        }
        entt::registry& rr = r;
        entt::registry* pr = &r;
        auto player = make_player(rr, 6.0f, 6.0f, 2);
        for (int i = 0; i < 40; i++)
            make_enemy(r, -6.0f + (i % 8), 0.5f + (i / 8), 50000 + i);
        std::vector<entt::entity> bullets;
        for (int i = 0; i < 50; i++)
            bullets.push_back(make_bullet(r, 0.0f + (i % 5), 0.0f + (i / 5) * 0.5f, 1));

        bench("2-40enemies+50bullets", 3000, [&] {
            auto& pl = pr->get<LogicPositionComponent>(player);
            float ox = fp_to_float(pl.x);
            pl.x = fp_from_float(ox < 7.0f ? 7.0f : 6.0f);
            ai_system(*pr, ctx, 0.0f);
            pathfinding_system(*pr, ctx, 0.0f);
            bullet_system(*pr, ctx, 0.0f);
        });
    }

    // ============ 场景 3：大量障碍 + 大量子弹（仅子弹系统压力） ============
    {
        entt::registry r;
        r.ctx().emplace<GlobalState>();
        int obs = 200;
        for (int i = 0; i < obs; i++)
            make_obstacle(r, -8.0f + (i % 16) * 1.1f, -6.0f + (i / 16) * 1.1f, 0.5f, 0.5f, 90000 + i);
        std::vector<entt::entity> bullets;
        for (int i = 0; i < 100; i++)
            bullets.push_back(make_bullet(r, 2.0f, -3.0f + (i % 10) * 0.3f, 1));
        entt::registry* pr = &r;
        bench("3-200obs+100bullets (bullet only)", 3000, [&] { bullet_system(*pr, ctx, 0.0f); });
    }

    printf("bench done\n");
    return 0;
}