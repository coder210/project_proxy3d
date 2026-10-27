#ifndef PATHFINDING_SYSTEM_H
#define PATHFINDING_SYSTEM_H

#include "../components/pathfinding_component.h"
#include "../components/ai_component.h"
#include "../components/enemy_component.h"
#include "../components/logic_position_component.h"
#include "../components/action_state_component.h"
#include "../components/collider_component.h"
#include "../components/static_mesh_component.h"
#include "../components/global_state_component.h"
#include <joy/joy.h>
#include <cmath>

struct Context;

// 敌人沿路径移动步长：与 ai_system 一致 2.5 m/s × 1/30 = 1/12 m/帧（纯定点）
static const fp_t PF_STEP = fp_div(fp_from_int(1), fp_from_int(12));

// 路径重算节拍（tick 数）：追击时最多每 10 tick（0.33s @30Hz）重算一次，
// 玩家连续移动也只需周期性刷新路径，不必每 tick 全量 A*，避免寻路成为 CPU 热点。
static const int PF_RECOMPUTE_INTERVAL = 10;

/* ============================================================
 * 静态障碍物 → 网格可通行性表（仅初始化一次，缓存进 AstarWork.walkable）
 * 遍历所有带 StaticMeshComponent + ColliderComponent 的静态碰撞体，
 * 取其在 XZ 平面的包围盒（旋转体取旋转后 AABB），外扩 PF_PAD 边距，
 * 覆盖到的格子标记为不可通行。两端由同一份障碍数据生成 → 帧同步确定。
 * ============================================================ */
static void pf_build_walkable(GlobalState::AstarWork& work, entt::registry& registry)
{
    if (work.walkable_init) return;
    for (int i = 0; i < PF_CELLS; i++) work.walkable[i] = true;

    auto view = registry.view<StaticMeshComponent, ColliderComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& col  = view.get<ColliderComponent>(e);
        auto& lpos = view.get<LogicPositionComponent>(e);
        float cx = fp_to_float(lpos.x), cz = fp_to_float(lpos.z);
        float hx = 0.0f, hz = 0.0f;
        switch (col.type) {
        case ColliderComponent::BOX:
            hx = fp_to_float(col.half_size.x);
            hz = fp_to_float(col.half_size.z);
            break;
        case ColliderComponent::SPHERE:
            hx = hz = fp_to_float(col.radius);
            break;
        case ColliderComponent::OBB: {
            // 绕 Y 轴旋转后的 XZ 投影半宽
            float rx = fp_to_float(col.half_size.x), rz = fp_to_float(col.half_size.z);
            float a  = fp_to_float(col.rotation.y);
            hx = fabsf(rx * cosf(a)) + fabsf(rz * sinf(a));
            hz = fabsf(rx * sinf(a)) + fabsf(rz * cosf(a));
            break;
        }
        case ColliderComponent::MESH: {
            float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
            for (int i = 0; i < col.verts_num; i++) {
                float vx = fp_to_float(col.verts[i].x);
                float vz = fp_to_float(col.verts[i].z);
                if (vx < minx) minx = vx;
                if (vx > maxx) maxx = vx;
                if (vz < minz) minz = vz;
                if (vz > maxz) maxz = vz;
            }
            cx = (minx + maxx) * 0.5f;
            cz = (minz + maxz) * 0.5f;
            hx = (maxx - minx) * 0.5f;
            hz = (maxz - minz) * 0.5f;
            break;
        }
        default: continue;
        }

        float pad = fp_to_float(PF_PAD);
        int gx0 = (int)floorf(cx - hx - pad + PF_GRID_OFFSET);
        int gx1 = (int)floorf(cx + hx + pad + PF_GRID_OFFSET);
        int gz0 = (int)floorf(cz - hz - pad + PF_GRID_OFFSET);
        int gz1 = (int)floorf(cz + hz + pad + PF_GRID_OFFSET);
        if (gx0 < 0) gx0 = 0;
        if (gx1 > PF_GRID_N - 1) gx1 = PF_GRID_N - 1;
        if (gz0 < 0) gz0 = 0;
        if (gz1 > PF_GRID_N - 1) gz1 = PF_GRID_N - 1;
        for (int gz = gz0; gz <= gz1; gz++)
            for (int gx = gx0; gx <= gx1; gx++)
                work.walkable[gz * PF_GRID_N + gx] = false;
    }
    work.walkable_init = true;
}

/* ============================================================
 * 二叉堆 A*（起点/终点格子坐标 → 路径格子索引序列，不含起点含终点）
 * 纯整数运算：曼哈顿启发 + 4 方向移动 + O(log N) 二叉堆，帧同步确定。
 * 边界处理：坐标钳到网格范围；起点/终点所在格不可通行时回退到最近可行格。
 * ============================================================ */

// 找离 (cx,cz) 最近的可行格（Chebyshev 环形扩展），找不到返回 -1
static int pf_nearest_walkable(const bool* walkable, int cx, int cz)
{
    for (int r = 0; r < PF_GRID_N; r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                int gx = cx + dx, gz = cz + dz;
                if (gx < 0 || gz < 0 || gx >= PF_GRID_N || gz >= PF_GRID_N) continue;
                if (walkable[gz * PF_GRID_N + gx]) return gz * PF_GRID_N + gx;
            }
        }
    }
    return -1;
}

static int pathfind_astar(GlobalState::AstarWork& work, entt::registry& registry,
                          int sx, int sz, int tx, int tz, int* out, int max_out)
{
    pf_build_walkable(work, registry);
    const int N = PF_CELLS;
    const bool* pf_walkable = work.walkable;

    // 钳到合法网格范围（出生点/巡逻点可能正好落在世界边界上，如 (10,10)）
    if (sx < 0) sx = 0; else if (sx >= PF_GRID_N) sx = PF_GRID_N - 1;
    if (sz < 0) sz = 0; else if (sz >= PF_GRID_N) sz = PF_GRID_N - 1;
    if (tx < 0) tx = 0; else if (tx >= PF_GRID_N) tx = PF_GRID_N - 1;
    if (tz < 0) tz = 0; else if (tz >= PF_GRID_N) tz = PF_GRID_N - 1;

    int start = sz * PF_GRID_N + sx;
    int goal  = tz * PF_GRID_N + tx;

    // 起点/终点所在格不可通行（玩家贴障碍 / 出生在障碍外扩区）→ 回退最近可行格
    if (!pf_walkable[start]) {
        int ns = pf_nearest_walkable(pf_walkable, sx, sz);
        if (ns < 0) return 0;
        start = ns; sx = ns % PF_GRID_N; sz = ns / PF_GRID_N;
    }
    if (!pf_walkable[goal]) {
        int ng = pf_nearest_walkable(pf_walkable, tx, tz);
        if (ng < 0) return 0;
        goal = ng; tx = ng % PF_GRID_N; tz = ng / PF_GRID_N;
    }

    int* gscore = work.gscore;
    int* fscore = work.fscore;
    int* came   = work.came;
    bool* closed  = work.closed;
    bool* in_open = work.in_open;
    int* heap = work.heap;
    int heap_len = 0;

    for (int i = 0; i < N; i++) {
        gscore[i] = 0x3fffffff;
        fscore[i] = 0x3fffffff;
        came[i] = -1;
        closed[i] = false;
        in_open[i] = false;
    }

    gscore[start] = 0;
    fscore[start] = (sx > tx ? sx - tx : tx - sx) + (sz > tz ? sz - tz : tz - sz);

    auto heap_up = [&](int i) {
        while (i > 0) {
            int p = (i - 1) >> 1;
            if (fscore[heap[p]] <= fscore[heap[i]]) break;
            int t = heap[i]; heap[i] = heap[p]; heap[p] = t;
            i = p;
        }
    };
    auto heap_down = [&](int i) {
        for (;;) {
            int l = i * 2 + 1, r = i * 2 + 2, m = i;
            if (l < heap_len && fscore[heap[l]] < fscore[heap[m]]) m = l;
            if (r < heap_len && fscore[heap[r]] < fscore[heap[m]]) m = r;
            if (m == i) break;
            int t = heap[i]; heap[i] = heap[m]; heap[m] = t;
            i = m;
        }
    };

    heap[heap_len++] = start;
    in_open[start] = true;

    const int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    while (heap_len > 0) {
        int cur = heap[0];
        in_open[cur] = false;
        heap[0] = heap[--heap_len];
        if (heap_len > 0) heap_down(0);

        if (cur == goal) {
            int len = 0;
            int node = cur;
            while (node != start && node >= 0) {
                if (len < max_out) out[len++] = node;
                node = came[node];
            }
            for (int i = 0, j = len - 1; i < j; i++, j--) {
                int t = out[i]; out[i] = out[j]; out[j] = t;
            }
            return len;
        }

        closed[cur] = true;
        int cx = cur % PF_GRID_N, cy = cur / PF_GRID_N;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
            if (nx < 0 || ny < 0 || nx >= PF_GRID_N || ny >= PF_GRID_N) continue;
            int ni = ny * PF_GRID_N + nx;
            if (!pf_walkable[ni]) continue;
            if (closed[ni]) continue;

            int ng = gscore[cur] + 1;
            if (ng < gscore[ni]) {
                gscore[ni] = ng;
                fscore[ni] = ng + (nx > tx ? nx - tx : tx - nx) + (ny > tz ? ny - tz : tz - ny);
                came[ni] = cur;
                if (!in_open[ni]) {
                    in_open[ni] = true;
                    heap[heap_len++] = ni;
                    heap_up(heap_len - 1);
                } else {
                    for (int i = 0; i < heap_len; i++) {
                        if (heap[i] == ni) { heap_up(i); break; }
                    }
                }
            }
        }
    }
    return 0;
}

/* ============================================================
 * 敌人 A* 寻路系统：
 *  - ai_system 追击（state==1）时写 PathComponent(target_gx/gz, dirty=true)
 *  - 本系统按节拍重算路径，并沿路径逐格中心直线逼近移动（不强制锁格子）
 *  - 非追击状态：清空路径，移动由 ai_system（巡逻）负责
 * ============================================================ */
static void pathfinding_system(entt::registry& registry, Context* ctx, float /*dt*/)
{
    (void)ctx;
    auto view = registry.view<EnemyComponent, AIComponent, PathComponent,
                              LogicPositionComponent, ActionStateComponent>();
    auto& work = registry.ctx().get<GlobalState>().astar_work;

    for (auto e : view) {
        auto& ai  = view.get<AIComponent>(e);
        auto& pc  = view.get<PathComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);

        // 对象池中死亡未复用的敌人仍带 AIComponent+PathComponent：跳过，避免给它算路
        //（其位置已冻结，移动/AI 由 ai_system 统一跳过，此处仅省去不必要的 A* 开销）
        if (registry.all_of<HealthComponent>(e) &&
            registry.get<HealthComponent>(e).dead) continue;

        /* [DIAG] pathfinding 内部状态实况（节流）
           默认关闭：每帧刷屏影响测试；排查寻路时置 1 重新开启 */
#if 0
        static int pf_diag = 0;
        if ((++pf_diag & 15) == 0)
            log_info("[pf-state] e=%d state=%d dirty=%d cd=%d len=%d ni=%d pos=%.1f,%.1f tgt=%d,%d",
                     (int)e, ai.state, (int)pc.dirty, pc.recompute_cooldown, pc.path_len, pc.next_idx,
                     fp_to_float(pos.x), fp_to_float(pos.z), pc.target_gx, pc.target_gz);
#endif

        // 仅追击(1)/巡逻(0)/返回出生点(3)需要 A* 移动；攻击(2)/待机等其他状态清空路径原地不动
        if (ai.state != 1 && ai.state != 0 && ai.state != 3) {
            pc.path_len = 0;
            pc.next_idx = 0;
            pc.dirty = false;
            continue;
        }

        // 节流：cooldown 每 tick 递减；仅当 dirty 且冷却归零时才重算，
        // 玩家连续移动时约每 PF_RECOMPUTE_INTERVAL tick 刷新一次路径，避免每 tick 全量 A*。
        // 障碍静态固定，路径不会凭空失效，只随目标移动做周期性刷新即可保持避障正确。
        if (pc.recompute_cooldown > 0) pc.recompute_cooldown--;
        if (pc.dirty) {
            if (pc.recompute_cooldown == 0) {
                pc.recompute_cooldown = PF_RECOMPUTE_INTERVAL;
                int sx = pf_world_to_grid(pos.x);
                int sz = pf_world_to_grid(pos.z);
                pc.path_len = pathfind_astar(work, registry, sx, sz,
                                             pc.target_gx, pc.target_gz,
                                             pc.path, (int)(sizeof(pc.path) / sizeof(pc.path[0])));
                pc.next_idx = 0;
                /* [DIAG] 结果日志节流到每 64 次重算一次，避免写日志拖慢帧率 */
                static int pf_log_n = 0;
                if ((++pf_log_n & 63) == 0)
                    log_info("[pf] enemy %d path len=%d start=(%d,%d) target=(%d,%d)",
                             (int)e, pc.path_len, sx, sz, pc.target_gx, pc.target_gz);
            }
            pc.dirty = false;
        }

        // 沿路径移动：直线逼近当前路径点（格中心），到点推进下一格
        if (pc.path_len > 0 && pc.next_idx < pc.path_len) {
            int idx = pc.path[pc.next_idx];
            int gx = idx % PF_GRID_N;
            int gz = idx / PF_GRID_N;
            fp_t tx = pf_grid_to_world(gx);
            fp_t tz = pf_grid_to_world(gz);

            fp_t dx = fp_sub(tx, pos.x);
            fp_t dz = fp_sub(tz, pos.z);

            if (fp_abs(dx) <= PF_STEP && fp_abs(dz) <= PF_STEP) {
                pos.x = tx;
                pos.z = tz;
                pc.next_idx++;
            } else {
                // 轴对齐逼近（与 ai_system 巡逻一致，沿格边移动不斜穿障碍）
                if (fp_abs(dx) > fp_abs(dz))
                    pos.x = dx > fp_zero() ? fp_add(pos.x, PF_STEP) : fp_sub(pos.x, PF_STEP);
                else
                    pos.z = dz > fp_zero() ? fp_add(pos.z, PF_STEP) : fp_sub(pos.z, PF_STEP);
            }
        } else {
            // 路径耗尽 / 玩家已在目标格内：不再直线穿墙，原地待命等待玩家换格触发重算。
            // 仅攻击/待机等非移动状态置 IDLE。
            if (ai.state != 1) {
                auto& act = view.get<ActionStateComponent>(e);
                act.state = ActionStateComponent::IDLE;
            }
        }
    }
}

#endif // PATHFINDING_SYSTEM_H
