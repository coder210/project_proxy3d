#ifndef SPAWN_OBSTACLES_SYSTEM_H
#define SPAWN_OBSTACLES_SYSTEM_H
#include "context.h"
#include "components/static_mesh_component.h"
#include "procedural_map.h"
#include <joy/joy.h>

// 静态障碍实体辅助：由布局生成实体 / 销毁全部障碍。
// 波次换图时重建地图即"销毁全部 + 按新布局重建"，权威端与客户端共用同一套实体格式
// （LogicPosition 中心坐标 Y = 高度一半，LogicRect 记录 XZ 全尺寸，供快照/重建还原）。

// 由布局创建障碍实体（水墨黑白配色：深灰填充 + 浅描边）。
// next_id 传入当前实体 ID 起始值（每波重置为 10000，保证跨端/跨波确定性），函数内自增。
static void spawn_obstacles_from_layout(entt::registry& registry, Context* ctx,
                                        const PmLayout& layout, int& next_id)
{
    if (!ctx->r3d) return;
    for (int k = 0; k < layout.count; k++) {
        const PmObstacle& o = layout.obs[k];
        // 全部定点数运算，杜绝 float×float（帧同步确定性）。o 的 *_10 为"十分之一米"整数：
        //   中心 x = x10/10，半宽 = hx10/10，全宽 = 半宽×2，全高 = sy10/10，中心 Y = 全高/2
        fp_t fx  = fp_div(fp_from_int(o.x10), fp_from_int(10));
        fp_t fz  = fp_div(fp_from_int(o.z10), fp_from_int(10));
        fp_t fhx = fp_div(fp_from_int(o.hx10), fp_from_int(10));   // 半宽
        fp_t fhz = fp_div(fp_from_int(o.hz10), fp_from_int(10));   // 半深
        fp_t fsx = fp_mul(fhx, fp_from_int(2));                    // 全宽（逻辑矩形/线上）
        fp_t fsz = fp_mul(fhz, fp_from_int(2));                    // 全深
        fp_t fsy = fp_div(fp_from_int(o.sy10), fp_from_int(10));   // 全高
        fp_t fy  = fp_div(fsy, fp_from_int(2));                    // 中心 Y = 全高的一半

        // 观察层配色（仅渲染，确定性、双端一致，不影响 sim/寻路）：
        // 打破"千篇一律灰立方"，按墙体的角色分层（边界 / 隔墙 / 巨石掩体 / 小柱），
        // 再按空间位置轻抖明度，去掉满是同色大块的呆板感，保持水墨低饱和基调。
        // 纯由布局 o 的网格位置派生 → 端点解析一致，帧同步安全。
        bool is_border   = (o.gz0 == 0) || (o.gz0 + o.gh == PF_GRID_N) ||
                           (o.gx0 == 0) || (o.gx0 + o.gw == PF_GRID_N);
        bool is_vertical = (o.gh > o.gw * 2) && o.gh > 8;   // 隔墙/高墙竖条
        bool is_large    = o.gw >= 4 && o.gh >= 4;          // 房间内巨石掩体
        int  shade       = (((o.gx0 + o.gz0) & 1) ? 5 : -5);// 轻微抖动，破整齐
        int  fr, fg, fb, sr, sg, sb;
        if (is_border)            { fr=56; fg=58; fb=70; sr=88;  sg=90;  sb=104; } // 边界：深墨
        else if (is_vertical)     { fr=72; fg=74; fb=82; sr=104; sg=105; sb=114; } // 隔墙：暖灰
        else if (is_large)        { fr=48; fg=52; fb=62; sr=84;  sg=88;  sb=98;  } // 巨石：冷深灰
        else                      { fr=45; fg=45; fb=50; sr=200; sg=200; sb=206; } // 小柱/普通：亮灰
        fr = fr + shade; fr = fr < 30 ? 30 : fr;
        fg = fg + shade; fg = fg < 30 ? 30 : fg;
        fb = fb + shade; fb = fb < 30 ? 30 : fb;

        entt::entity e = registry.create();
        registry.emplace<IdComponent>(e, next_id++);
        registry.emplace<LogicPositionComponent>(e, fx, fy, fz);
        registry.emplace<LogicRectComponent>(e, fsx, fsz);

        ColliderComponent col;
        col.type = ColliderComponent::BOX;
        col.half_size = { fhx, fy, fhz };   // 半宽/半深/半高，客户端用 全宽/2(线上) 重建，逐位一致
        registry.emplace<ColliderComponent>(e, col);
        // Transform 属渲染数据，允许用定点转 float 填充
        registry.emplace<TransformComponent>(e,
            vec3_t{ fp_to_float(fx), fp_to_float(fy), fp_to_float(fz) }, 0.0f,
            vec3_t{ fp_to_float(fsx), fp_to_float(fsy), fp_to_float(fsz) });
        registry.emplace<StaticMeshComponent>(e, StaticMeshComponent{
            ctx->mesh_prop, ctx->mesh_prop_wire,
            { (uint8_t)fr, (uint8_t)fg, (uint8_t)fb, 255 },
            { (uint8_t)sr, (uint8_t)sg, (uint8_t)sb, 255 }, 0.0f });
    }
}

// 销毁全部障碍实体（障碍是唯一带 StaticMeshComponent 的实体；只销毁实体，不销毁共享 mesh）
static void destroy_obstacles(entt::registry& registry)
{
    std::vector<entt::entity> to_destroy;
    auto view = registry.view<StaticMeshComponent>();
    for (auto e : view) to_destroy.push_back(e);
    for (auto e : to_destroy) registry.destroy(e);
}

#endif
