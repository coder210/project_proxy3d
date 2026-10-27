#ifndef PATHFINDING_COMPONENT_H
#define PATHFINDING_COMPONENT_H
#include <joy/jmath.h>

// 3D 敌人 A* 寻路网格（XZ 平面）：
//   世界 [-32,32]×[-32,32]，1m 一格 → 64x64 格，格子索引 = gz*PF_GRID_N + gx
static const int  PF_GRID_N      = 64;
static const int  PF_GRID_MIN    = -32;
static const int  PF_GRID_OFFSET = -PF_GRID_MIN;   // 世界坐标 → 格子坐标平移量
static const int  PF_CELLS       = PF_GRID_N * PF_GRID_N;

// 障碍外扩边距（纯定点）：0.6m，敌人半宽 0.5，留安全间隙避免贴墙被 GJK 顶住
static const fp_t PF_PAD = fp_div(fp_from_int(3), fp_from_int(5));
// 格子中心到格子边缘的距离 0.5m（纯定点）
static const fp_t PF_HALF = fp_div(fp_from_int(1), fp_from_int(2));

// 世界坐标 → 格子坐标（含平移，fp 算术右移即 floor，负数向负无穷，纯整数确定）
static inline int pf_world_to_grid(fp_t v)
{
    return (int)((v >> FP_FRACTIONAL_PLACES) + PF_GRID_OFFSET);
}
// 格子坐标 → 格子中心世界坐标
static inline fp_t pf_grid_to_world(int g)
{
    return fp_add(fp_from_int(g - PF_GRID_OFFSET), PF_HALF);
}

// 敌人寻路状态（纯数据）：ai_system 追击时写 target_gx/gz 并置 dirty，
// pathfinding_system 按节拍重算 A* 并沿路径移动
struct PathComponent {
    int  target_gx = 0;        // 目标格子坐标（玩家所在格）
    int  target_gz = 0;
    bool dirty = false;        // 需要重算路径
    int  path[4096];           // 路径格子索引序列 (gz*PF_GRID_N + gx)；64×64 大图长路径预留（4096 格上限安全容纳全图）
    int  path_len = 0;
    int  next_idx = 0;         // 下一个要走的路径点下标
    int  recompute_cooldown = 0; // 重算节拍（帧数）
};

#endif // PATHFINDING_COMPONENT_H
