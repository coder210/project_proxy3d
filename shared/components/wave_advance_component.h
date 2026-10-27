#ifndef WAVE_ADVANCE_COMPONENT_H
#define WAVE_ADVANCE_COMPONENT_H

// 波次推进标记：换波时由双方共同创建（服务端权威端 & 收到换波信号的客户端）。
// 存在该组件的实体表示"本波敌人尚未生成"，由 wave_spawn_system 按逻辑帧定时生成，
// 生成完毕后移除组件并销毁实体。双端共用同一确定性生成逻辑（map_seed + wave），
// 因此无需在网络上同步具体的敌人实体数组，维持帧同步确定性。

// 换波后延迟多少个逻辑帧再开始生成敌人（让波次过渡有节奏感；20Hz 下 40 tick = 2 秒）
#ifndef WAVE_SPAWN_DELAY_TICKS
#define WAVE_SPAWN_DELAY_TICKS 40
#endif

// 每逻辑帧最多生成的敌人数。整波一次性生成会让所有敌人同时做首次 A* 寻路、
// 客户端还要同时建 GPU 网格，造成换波瞬间卡顿；按此配额分帧错峰生成可摊平开销。
// 双端共用同一确定性队列与配额，帧同步不受影响。
#ifndef WAVE_SPAWN_PER_FRAME
#define WAVE_SPAWN_PER_FRAME 4
#endif

#define WAVE_MAX_ENEMIES 64   // 与 wave_enemy_count 上限一致，存放确定性生成目标

struct WaveAdvanceComponent {
    int wave = 1;                                   // 目标波次（应等于 GlobalState.wave）
    int delay_ticks = WAVE_SPAWN_DELAY_TICKS;       // 剩余延迟逻辑帧数，由逻辑帧递减
    int total    = 0;                               // 本波待生成敌人总数
    int spawned  = 0;                               // 已生成数量
    int tiles[WAVE_MAX_ENEMIES];                    // 确定性出生格索引（pm_sample_open_tiles 输出）
    int si[WAVE_MAX_ENEMIES];                       // 确定性敌人类型索引
};

#endif