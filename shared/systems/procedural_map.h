#ifndef PROCEDURAL_MAP_H
#define PROCEDURAL_MAP_H

#include "../components/pathfinding_component.h"
#include <joy/joy.h>

// 程序化地图生成（泛洪聚团版）—— 薄封装层。
//
// 核心算法（锚点聚簇生长 + 多源 BFS 连通性修复 + 高度随机）已下沉到 joy 引擎层
// 的 jprocmap（jprocmap.c / joy.h），本文件仅负责：
//   - 保留游戏侧业务常量（PM_*）与结构体（PmObstacle / PmLayout），保证上游
//     （wave_system / spawn_obstacles_system / tools）调用点不变；
//   - 把 PM_* 常量组织成 jpm_config_t，调 joy 的 jpm_* 接口；
//   - 在 PmLayout（400 格）与 jpm_layout_t（JPM_MAX_CELLS 容量）之间做内存转换。
//
// 布局只由 seed 决定 → 完全确定性，可用作帧同步依据；波次换图用确定性派生 seed
// （pmap_wave_seed），服务端生成后经 S2C_CMD_MAP_RELOAD 同步给客户端。

#define PM_GRID_N    PF_GRID_N   // 64（与寻路网格一致）
#define PM_CLEAR     22          // 出生点安全区半径 2.2m（十分之一米）
#define PM_PAD10     6           // 障碍外扩边距 0.6m（与 PF_PAD 一致）
#define PM_OFFSET10  (PF_GRID_OFFSET * 10)   // 世界坐标 → 网格坐标平移（64×64 → 320）
#define PM_MAX_OBS   4096        // 障碍槽位容量，容纳程序化地图生成的障碍实体
#define PM_DENSITY   25          // 目标障碍密度（%）：400 格 × 25% = 100 次撒点
#define PM_HEIGHT_MIN 5          // 障碍最低高度 0.5m（十分之一米）
#define PM_HEIGHT_MAX 18         // 障碍最高高度 1.8m（十分之一米）
#define PM_CLUSTER_MIN 5         // 每簇最小格数
#define PM_CLUSTER_MAX 10        // 每簇最大格数
#define PM_PLAYER_GX 10          // 玩家出生点所在格 (0,0)
#define PM_PLAYER_GZ 10
#define PM_ENEMY_GX  19          // 敌人出生点所在格 (10,10)
#define PM_ENEMY_GZ  19
#define PM_ENEMY_CX10 100        // 敌人安全区圆心世界坐标（十分之一米，约 10m）
#define PM_ENEMY_CZ10 100

struct PmObstacle {
    int x10, z10;    // 中心坐标（十分之一米，格子对齐）
    int hx10, hz10;  // XZ 半尺寸（十分之一米，单格 = 5）
    int sy10;        // 高度（十分之一米，随机 PM_HEIGHT_MIN..PM_HEIGHT_MAX）
    int gx0, gz0;    // 占用格子（左/下角格）
    int gw, gh;      // 占用格子数（宽/深，恒 1）
};

struct PmLayout {
    PmObstacle obs[PM_MAX_OBS];
    int  count = 0;
    int  seed  = 0;              // 实际使用的生成 seed（调试用）
    bool walkable[PF_CELLS];     // 生成后的可通行栅格（含 0.6m 外扩，镜像寻路）
};

/* 由 PM_* 常量构建引擎配置（与旧算法参数完全一致，保证确定性不变） */
static inline void pm_build_config(jpm_config_t* cfg)
{
    cfg->grid_n       = PM_GRID_N;
    cfg->offset10     = PM_OFFSET10;
    cfg->pad10        = PM_PAD10;
    cfg->density      = PM_DENSITY;
    cfg->max_obs      = PM_MAX_OBS;
    cfg->height_min10 = PM_HEIGHT_MIN;
    cfg->height_max10 = PM_HEIGHT_MAX;
    cfg->cluster_min  = PM_CLUSTER_MIN;
    cfg->cluster_max  = PM_CLUSTER_MAX;
    cfg->spawn_count  = 0;
    jpm_config_add_spawn(cfg, 0, 0,          PM_CLEAR);   // 玩家安全区
    jpm_config_add_spawn(cfg, PM_ENEMY_CX10, PM_ENEMY_CZ10, PM_CLEAR); // 敌人安全区
}

/* PmLayout（400 格）→ jpm_layout_t（大容量），仅转换与调用相关的首部 */
static inline void pm_to_jlayout(const jpm_config_t* cfg, const PmLayout& src, jpm_layout_t* dst)
{
    dst->count = src.count;
    dst->seed  = src.seed;
    for (int k = 0; k < src.count && k < JPM_MAX_OBS; k++) {
        jpm_obstacle_t* o = &dst->obs[k];
        o->x10 = src.obs[k].x10; o->z10 = src.obs[k].z10;
        o->hx10 = src.obs[k].hx10; o->hz10 = src.obs[k].hz10;
        o->sy10 = src.obs[k].sy10;
        o->gx0 = src.obs[k].gx0; o->gz0 = src.obs[k].gz0;
        o->gw = src.obs[k].gw;   o->gh = src.obs[k].gh;
    }
    int cells = cfg->grid_n * cfg->grid_n;
    if (cells > JPM_MAX_CELLS) cells = JPM_MAX_CELLS;
    for (int i = 0; i < cells; i++) dst->walkable[i] = src.walkable[i];
}

/* jpm_layout_t → PmLayout（仅取 gam 关心的前 PM_GRID_N 格） */
static inline void pm_from_jlayout(const jpm_config_t* cfg, const jpm_layout_t& src, PmLayout* dst)
{
    dst->count = src.count;
    dst->seed  = src.seed;
    for (int k = 0; k < src.count && k < PM_MAX_OBS; k++) {
        dst->obs[k].x10 = src.obs[k].x10; dst->obs[k].z10 = src.obs[k].z10;
        dst->obs[k].hx10 = src.obs[k].hx10; dst->obs[k].hz10 = src.obs[k].hz10;
        dst->obs[k].sy10 = src.obs[k].sy10;
        dst->obs[k].gx0 = src.obs[k].gx0; dst->obs[k].gz0 = src.obs[k].gz0;
        dst->obs[k].gw = src.obs[k].gw;   dst->obs[k].gh = src.obs[k].gh;
    }
    int cells = cfg->grid_n * cfg->grid_n;
    if (cells > PF_CELLS) cells = PF_CELLS;
    for (int i = 0; i < cells; i++) dst->walkable[i] = src.walkable[i];
}

// 波次换图用 seed：由初始 seed 与波次号确定性派生（纯函数，服务端/客户端不共享随机流）
static inline uint32_t pmap_wave_seed(uint32_t base_seed, int wave)
{
    return jpm_wave_seed(base_seed, wave);
}

// 4 方向 BFS：返回从起点可达的格子总数（用于连通性校验）
static inline int pm_bfs(const bool walkable[PF_CELLS], int sx, int sz)
{
    return jpm_bfs(walkable, PM_GRID_N, sx, sz);
}

// 整数栅格化（镜像 pf_build_walkable 的 BOX 分支；本层已改用引擎实现，保留兼容入口）
static inline void pm_rasterize(bool walkable[PF_CELLS], const PmObstacle* obs, int n)
{
    jpm_config_t cfg; pm_build_config(&cfg);
    bool tmp[JPM_MAX_CELLS];
    jpm_rasterize(&cfg, tmp, (const jpm_obstacle_t*)obs, n);
    for (int i = 0; i < PF_CELLS; i++) walkable[i] = tmp[i];
}

// 核心生成：给定 seed，锚点泛洪聚簇生长 + 连通性修复 + 高度随机（引擎实现）
static void pmap_generate_seed(uint32_t seed, PmLayout* out)
{
    jpm_config_t cfg; pm_build_config(&cfg);
    jpm_layout_t jlt;
    jpm_generate_seed(&cfg, seed, &jlt);
    pm_from_jlayout(&cfg, jlt, out);
}

// 从上层随机流抽取 1 个 seed 派生布局（初始生成用）
static void pmap_generate(random_num_p src_rng, PmLayout* out)
{
    jpm_config_t cfg; pm_build_config(&cfg);
    jpm_layout_t jlt;
    jpm_generate(&cfg, src_rng, &jlt);
    pm_from_jlayout(&cfg, jlt, out);
}

// 从布局开放格中随机采样 count 个格子索引（洗牌后取前 count 个），
// 返回实际采样数；min_dist10 限制距玩家安全区圆心（spawns[0]）的最小距离。
static int pm_sample_open_tiles(random_num_p rng, const PmLayout& L, int count,
                                int min_dist10, int* out_tiles)
{
    jpm_config_t cfg; pm_build_config(&cfg);
    jpm_layout_t jlt;
    pm_to_jlayout(&cfg, L, &jlt);
    return jpm_sample_open_tiles(rng, &cfg, &jlt, count, min_dist10, out_tiles);
}

#endif // PROCEDURAL_MAP_H