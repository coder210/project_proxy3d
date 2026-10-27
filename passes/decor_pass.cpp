#include "decor_pass.h"
#include <joy/joy.h>
#include "../context.h"
#include "../shared/components/global_state_component.h"
#include "../shared/systems/level_maps.h"
#include "../shared/systems/procedural_map.h"
#include "../shared/components/pathfinding_component.h"
#include <vector>

// 地图装饰层（纯渲染）：只读固定布局派生 → 确定性、各端一致。
// 不进 ECS / 无 Collider / 不参与 sim 与 checksum，纯粹给"盒子空腔"加视觉层次与方向感。
//   1) 出口金门：立在出口(X)处，醒目暖金，给"告一段落"落点；
//   2) 房间焦点柱：每个竖长廊房间中央一根暖金矮立柱，让空腔有焦点。

enum { MAX_PILLARS = 8 };
struct DecorData {
    int  level = -1;              // 缓存的关卡
    int  pillar_n = 0;            // 焦点柱数量
    float px[MAX_PILLARS], pz[MAX_PILLARS];
};

static DecorData        g_decor;
static render_mesh_p    g_cube = NULL;

static void ensure_mesh(Context* ctx) {
    if (!g_cube) g_cube = r3d_mesh_cube(ctx->r3d, 1.0f, 1.0f, 1.0f);
}

// 在世界坐标 (wx,wy,wz) 画中心对称盒，尺寸 sx/ sy/ sz
static void draw_box(Context* ctx, float wx, float wy, float wz,
                     float sx, float sy, float sz,
                     uint8_t fr, uint8_t fg, uint8_t fb,
                     uint8_t sr, uint8_t sg, uint8_t sb)
{
    mat44_t m = mat44_identity();
    m.m0 = sx; m.m5 = sy; m.m10 = sz;
    m.m12 = wx; m.m13 = wy; m.m14 = wz;
    jcolor_t fill = { fr, fg, fb, 255 };
    jcolor_t wire = { sr, sg, sb, 255 };
    r3d_draw_mesh_solid_basis(ctx->r3d, g_cube, &m, fill);
    r3d_draw_wire_basis(ctx->r3d, g_cube, &m, wire);
}

// 从固定布局派生装饰数据（首次按 level 缓存）
static void build_decor(Context* ctx, int level)
{
    if (g_decor.level == level) return;
    g_decor = DecorData();
    g_decor.level = level;
    ensure_mesh(ctx);

    int N = PF_GRID_N;
    // 障碍栅格化位图
    std::vector<unsigned char> blocked((size_t)N * N, 0);
    PmLayout L; int pc = -1; int ecs[WAVE_MAX_ENEMIES]; int en = 0;
    if (!level_build_layout(level, &L, pc, ecs, en)) return;
    for (int k = 0; k < L.count; k++) {
        const PmObstacle& o = L.obs[k];
        for (int gx = o.gx0; gx < o.gx0 + o.gw && gx < N; gx++)
            for (int gz = o.gz0; gz < o.gz0 + o.gh && gz < N; gz++)
                if (gx >= 0 && gz >= 0) blocked[(size_t)gz * N + gx] = 1;
    }
    // 房间内部列（避开竖直隔墙所在列：隔墙约每 21 列一枚）
    const int cols[] = { 10, 30, 50, 70, 90, 110 };
    for (int ci = 0; ci < 6; ci++) {
        int gx = cols[ci];
        int gz = -1;
        // 从图中央行向上下找最近可站立的开格
        for (int d = 0; d < N / 2; d++) {
            int r = N / 2 + d;
            if (r >= 0 && r < N && !blocked[(size_t)r * N + gx]) { gz = r; break; }
            r = N / 2 - d;
            if (r >= 0 && r < N && !blocked[(size_t)r * N + gx]) { gz = r; break; }
        }
        if (gz < 0) continue;
        g_decor.pz[g_decor.pillar_n] = (float)gz - (N / 2.0f);
        g_decor.px[g_decor.pillar_n] = (float)gx - (N / 2.0f);
        g_decor.pillar_n++;
    }
}

// 出口金门（黄金暖调，显眼）——立在出口格，玩家穿过去即通关
static void draw_exit_door(Context* ctx, int level)
{
    fp_t ex, ez;
    level_exit_position(level, &ex, &ez);   // void 返回：出口格心
    float cx = fp_to_float(ex);
    float cz = fp_to_float(ez);
    // 门槛基座
    draw_box(ctx, cx, 0.10f, cz, 3.4f, 0.20f, 1.2f, 120, 96, 60, 176, 144, 92);
    // 门板（略高于地面，中间留缝：底部略缩，做"门"的造型）
    draw_box(ctx, cx, 0.75f, cz, 2.6f, 1.30f, 0.30f, 214, 168, 76, 150, 112, 40);
    // 左右门柱
    draw_box(ctx, cx - 1.55f, 1.25f, cz, 0.30f, 2.50f, 0.40f, 150, 116, 60, 90, 70, 40);
    draw_box(ctx, cx + 1.55f, 1.25f, cz, 0.30f, 2.50f, 0.40f, 150, 116, 60, 90, 70, 40);
    // 横梁
    draw_box(ctx, cx, 2.62f, cz, 3.6f, 0.30f, 0.50f, 198, 154, 84, 128, 96, 50);
}

void decor_pass(Context* ctx)
{
    if (!ctx->r3d) return;
    if (!ctx->ecs.ctx().contains<GlobalState>()) return;
    int level = ctx->ecs.ctx().get<GlobalState>().level;
    build_decor(ctx, level);

    // 房间焦点柱（暖金立柱）
    for (int i = 0; i < g_decor.pillar_n; i++)
        draw_box(ctx, g_decor.px[i], 0.70f, g_decor.pz[i],
                 0.9f, 1.4f, 0.9f, 168, 132, 72, 214, 182, 118);

    // 出口金门
    draw_exit_door(ctx, level);
}