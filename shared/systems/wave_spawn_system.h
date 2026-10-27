#ifndef WAVE_SPAWN_SYSTEM_H
#define WAVE_SPAWN_SYSTEM_H
#include <joy/joy.h>
#include "context.h"
#include "components/wave_advance_component.h"
#include "procedural_map.h"
#include "wave_system.h"   // 复用 wave_prepare_enemies / wave_spawn_enemy_range

// 波次敌人生成系统：扫描 WaveAdvanceComponent。
// 每个逻辑帧递减 delay_ticks，归零后每帧按 WAVE_SPAWN_PER_FRAME 配额生成一批敌人，
// 直到本波全部生成，再移除组件并销毁实体。分帧错峰生成摊平首次 A* 与网格创建开销。
// 双端（服务端权威 + 客户端）运行同一系统与同一确定性队列，从而保证敌人生成完全一致，
// 无需通过网络同步实体数组，维持帧同步确定性。
static void wave_spawn_system(entt::registry& registry, Context* ctx, float)
{
    auto view = registry.view<WaveAdvanceComponent>();
    // 先收集到期的标记再统一处理，避免遍历时销毁实体导致迭代器失效
    std::vector<entt::entity> ready;
    for (auto e : view) {
        auto& w = view.get<WaveAdvanceComponent>(e);
        // 换波音效：WaveAdvanceComponent 刚创建（delay 尚未递减）时触发一次。
        // 双端运行（服务端权威 + 客户端），服务端不播；首波进入游戏不建该组件，不触发。
        if (w.delay_ticks == WAVE_SPAWN_DELAY_TICKS && !ctx->is_server) {
            ctx->sfx_requests.push_back({ SFX_WAVE });
        }
        if (w.delay_ticks > 0) { w.delay_ticks--; continue; }
        ready.push_back(e);
    }
    if (ready.empty()) return;

    for (auto e : ready) {
        auto& w = view.get<WaveAdvanceComponent>(e);
        int start = w.spawned;
        int end = start + WAVE_SPAWN_PER_FRAME;
        if (end > w.total) end = w.total;
        wave_spawn_enemy_range(ctx, w.tiles, w.si, start, end);
        w.spawned = end;
        if (w.spawned >= w.total) {
            registry.remove<WaveAdvanceComponent>(e);
            registry.destroy(e);
        }
    }
}

#endif