#ifndef GLOBAL_STATE_COMPONENT_H
#define GLOBAL_STATE_COMPONENT_H
#include <joy/jmath.h>
#include "pathfinding_component.h"

// 全局单例状态（通过 registry.ctx().get<GlobalState>() 访问）
struct GlobalState {
    // 帧同步
    int next_entity_id = 1;
    int next_frame_id  = 1;

    // 随机种子（loading 时同步给客户端，保证双方随机行为一致）
    int random_seed = 0;

    // 波次与程序化地图（波次换图：每波结束重生成地图）
    int wave = 0;                // 当前波次（0=尚未开始；1 起为实际波次）
    int map_seed = 0;            // 初始地图 seed（首波从随机流抽取，之后经 pmap_wave_seed 派生）
    int map_wave_generated = -1; // 已物化障碍实体的波次（与 wave 不等则需重建地图）
    int level = 1;               // 当前固定地图关卡索引（1 基，level_maps.h；推进由外部场景决定）
    bool level_cleared = false;  // 本关敌人是否已全部清空（仅权威端驱动流转，等待玩家走到出口）
    int kills = 0;               // 玩家累计击杀数（帧同步确定性：双端各自累计，结果一致）

    // 摄像机
    vec3_t camera_position = { 0.0f, 0.0f, 0.0f };
    vec3_t camera_target = { 0.0f, 0.0f, 0.0f };
    float camera_yaw = 0.0f;  // 相机水平朝向（弧度），移动方向相对此方向
    int camera_follow_conv = -1;  // 要跟随的玩家 conv，-1 表示不跟随

    // A* 寻路工作区（3D）：PF_GRID_N × PF_GRID_N 网格，存放 pathfind_astar 搜索临时缓冲
    // 与地图可通行性缓存。仅作临时计算缓冲，不是帧同步状态、不参与 checksum。
    struct AstarWork {
        static const int MAX_CELLS = PF_CELLS;   // PF_GRID_N * PF_GRID_N

        int   gscore[MAX_CELLS];
        int   fscore[MAX_CELLS];
        int   came[MAX_CELLS];
        bool  closed[MAX_CELLS];
        bool  in_open[MAX_CELLS];
        int   heap[MAX_CELLS];              // 二叉堆：按 fscore 最小堆
        bool  walkable[MAX_CELLS];          // 网格可通行性缓存（障碍栅格化结果）
        bool  walkable_init = false;
    } astar_work;
};

#endif
