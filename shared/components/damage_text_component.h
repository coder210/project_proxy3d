#ifndef DAMAGE_TEXT_COMPONENT_H
#define DAMAGE_TEXT_COMPONENT_H

// 伤害飘字渲染实体：由 damage_pass 消费玩家命中请求后创建（纯渲染，不参与帧同步）。
// 世界空间 billboard 文字，向上漂浮并在生命周期内淡出。
struct DamageTextComponent {
        float x, z;           // 目标（被命中敌人）中心 XZ
        float lifetime;       // 剩余显示时长（秒）
        float base_lifetime;  // 总时长（用于计算进度/高度）
        int   amount;         // 伤害数值
        bool  kill;           // 是否击杀（爆字加粗加大打红/橙）
};

#endif