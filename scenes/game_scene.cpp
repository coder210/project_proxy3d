#include <cstring>
#include <cstdio>
#include <ctime>
#include <SDL3/SDL_timer.h>
#include <joy/joy.h>
#include "../context.h"
#include "game_scene.h"
#include "../game_mode.h"
#include "../layers/gameplay_controls_layer.h"
#include "../passes/render3d_pass.h"
#include "systems/wave_system.h"
#include "systems/wave_spawn_system.h"
#include "systems/input_consume_join_system.h"
#include "systems/input_consume_leave_system.h"
#include "systems/input_consume_input_system.h"
#include "systems/movement_system.h"
#include "systems/knockback_system.h"
#include "systems/cleanup_system.h"
#include "systems/collision_system.h"
#include "systems/ai_system.h"
#include "systems/pathfinding_system.h"
#include "systems/melee_attack_system.h"
#include "systems/bullet_system.h"
#include "systems/player_death_system.h"
#include "components/global_state_component.h"
#include "components/net_component.h"
#include "components/enemy_component.h"
#include "components/health_component.h"
#include "victory_scene.h"

static GameModeInterface* g_mode = NULL;

// [PROFILE] 每帧逻辑/渲染耗时统计（临时期望定位 40~56 帧波动）
static double g_logic_ms = 0, g_render_ms = 0;
static int    g_profile_cnt = 0;
static uint32_t g_profile_t0 = 0;

GameScene::GameScene(Context* ctx) : Scene(ctx, "game_scene") {
    set_fixed_step(ctx->FIXED_TIMESTEP);
}

void GameScene::Load() {
    Context* ctx = ctx_;
    log_info("[game scene] start...");

    // 选择模式接口
    switch (ctx->mode) {
    case MODE_SINGLE: g_mode = &g_single_mode; break;
    case MODE_HOST:   g_mode = &g_host_mode; break;
    case MODE_CLIENT: g_mode = &g_client_mode; break;
    case MODE_SERVER: g_mode = &g_server_mode; break;
    default:          g_mode = &g_client_mode; break;
    }

    // ECS 单例
    ctx->ecs.ctx().emplace<NetComponent>();
    auto& gs = ctx->ecs.ctx().emplace<GlobalState>();
    gs.level = ctx->selected_level;   // 应用选关页选定的固定地图关卡（双端以存量的 S2C loading 为准）
    if (ctx->is_server) {
        gs.random_seed = (int)time(nullptr);
        ctx->random = utils_random_create((uint32_t)gs.random_seed);
    }

    // 模式特有初始化
    g_mode->init(this);

    // 虚拟操作层（单机/Host/客户端有本地玩家才显示；独立服务端不显示）
    if (!ctx->is_server) {
        g_gameplay_controls = gameplay_controls_layer_create(ctx);
    }

    // 公共 systems
    add_system(ctx, PreUpdate, "input_consume_join", input_consume_join_system);
    add_system(ctx, PreUpdate, "input_consume_leave", input_consume_leave_system);
    add_system(ctx, PreUpdate, "input_consume_input", input_consume_input_system);
    add_system(ctx, OnUpdate, "ai_system", ai_system);
    add_system(ctx, OnUpdate, "pathfinding", pathfinding_system);
    add_system(ctx, OnUpdate, "melee_attack", melee_attack_system);
    add_system(ctx, OnUpdate, "bullet", bullet_system);
    add_system(ctx, OnUpdate, "player_death", player_death_system);
    add_system(ctx, OnUpdate, "wave", wave_system);
    add_system(ctx, OnUpdate, "wave_spawn", wave_spawn_system);
    add_system(ctx, OnUpdate, "movement", movement_system);
    add_system(ctx, OnUpdate, "knockback", knockback_system);
    add_system(ctx, OnUpdate, "collision", collision_system);
    add_system(ctx, OnUpdate, "cleanup", cleanup_system);

    // 执行 OnStart 阶段
    run_phase(ctx, OnStart, 0);
}

void GameScene::HandleEvent(const void* ev) {
    // 虚拟操作层优先处理鼠标（方向键/攻击键）
    gameplay_controls_layer_handle_event(g_gameplay_controls, ev);
    if (g_mode) g_mode->handle_event(this, ev);
}

// [FixedUpdate] 引擎按固定步长(fixed_step)驱动；委托给当前模式的每 tick 逻辑
void GameScene::FixedUpdate(float step) {
    if (g_mode && g_mode->fixed_update) g_mode->fixed_update(this, step);
}

void GameScene::Update(float dt) {
    Context* ctx = ctx_;

    // 统计逻辑帧率
    uint32_t now = SDL_GetTicks();
    if (now - fps_measure_time >= 1000) {
        int cur_frame = ctx->ecs.ctx().get<GlobalState>().next_frame_id;
        int fps = cur_frame - fps_measure_frame;
        ctx->logic_fps = fps;
        log_info("logic fps: %d", fps);
        fps_measure_time = now;
        fps_measure_frame = cur_frame;
    }

    uint64_t t_logic0 = SDL_GetPerformanceCounter();
    if (g_mode) g_mode->update(this, dt);
    uint64_t t_logic1 = SDL_GetPerformanceCounter();
    double logic_ms = (double)(t_logic1 - t_logic0) / SDL_GetPerformanceFrequency() * 1000.0;
    g_logic_ms += logic_ms;
    // 逻辑掉帧检测：单次逻辑超过 33ms 说明逻辑层卡顿（AI/碰撞/寻路/子弹）
    if (logic_ms > 33.0) {
        log_info("[spike-logic] logic=%.1fms", logic_ms);
    }

    // 单关制通关检测（仅渲染端触发一次）：全部敌人肃清后延时切入通关界面。
    // 优先取权威端 level_cleared；兜底用本地实体状态推导（帧同步实体同步，跨端确定性一致）。
    if (!victory_started && ctx->ecs.ctx().contains<GlobalState>()) {
        auto& gsv = ctx->ecs.ctx().get<GlobalState>();
        bool cleared = gsv.level_cleared;
        if (!cleared && gsv.wave > 0) {
            cleared = true;
            auto ev = ctx->ecs.view<EnemyComponent, HealthComponent>();
            for (auto e : ev)
                if (!ev.get<HealthComponent>(e).dead) { cleared = false; break; }
        }
        if (cleared) {
            victory_delay += dt;
            if (victory_delay >= 1.2f) {
                victory_started = true;
                log_info("[victory] level %d cleared, switch to victory scene", gsv.level);
                ctx_->scene_mgr->change(new VictoryScene(ctx_));
            }
        }
    }
}

void GameScene::Render() {
    Context* ctx = ctx_;
    if (ctx->r3d) {
        // 渲染帧间隔（相机跟随平滑插值需要真实 dt；钳制防跳变）
        uint32_t now = SDL_GetTicks();
        float dt = (last_render_ms > 0) ? (float)(now - last_render_ms) / 1000.0f : 0.016f;
        if (dt < 0.0f || dt > 0.1f) dt = 0.016f;
        last_render_ms = now;
        uint64_t t_ren0 = SDL_GetPerformanceCounter();
        render3d_pass(ctx, dt);
        uint64_t t_ren1 = SDL_GetPerformanceCounter();
        g_render_ms += (double)(t_ren1 - t_ren0) / SDL_GetPerformanceFrequency() * 1000.0;

        // [PROFILE] 每 60 帧输出一次逻辑/渲染平均耗时
        g_profile_cnt++;
        if (g_profile_cnt == 1) g_profile_t0 = SDL_GetTicks();
        if (g_profile_cnt >= 60) {
            uint32_t el = (SDL_GetTicks() - g_profile_t0);
            if (el == 0) el = 1;
            double real_fps = 60000.0 / el;
            log_info("[profile] logic=%.3fms/f render=%.3fms/f real_fps=%.0f",
                g_logic_ms / 60.0, g_render_ms / 60.0, real_fps);
            g_logic_ms = 0; g_render_ms = 0; g_profile_cnt = 0;
        }
    }
}

void GameScene::Destroy() {
    Context* ctx = ctx_;
    if (g_mode) g_mode->destroy(this);
    g_mode = NULL;
    if (g_gameplay_controls) {
        gameplay_controls_layer_destroy(g_gameplay_controls);
        g_gameplay_controls = NULL;
    }

    // 退出时统一落盘帧同步校验记录（server/host 与 client 分别写文件，便于 diff）
    if (ctx && !ctx->checksum_records.empty()) {
        const char* log_name = (ctx->mode == MODE_SERVER || ctx->mode == MODE_HOST)
            ? "checksum_log_server.txt" : "checksum_log.txt";
        FILE* fp = fopen(log_name, "w");
        if (fp) {
            fprintf(fp, "frame\tchecksum\tinputs\tentities\n");
            for (auto& r : ctx->checksum_records) {
                fprintf(fp, "%d\t%s\t%s\t%s\n",
                    r.frame_id, r.checksum,
                    r.inputs.c_str(), r.entities.c_str());
            }
            fclose(fp);
        }
    }
}
