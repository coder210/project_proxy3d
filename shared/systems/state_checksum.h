#ifndef STATE_CHECKSUM_H
#define STATE_CHECKSUM_H
#include <joy/jmath.h>
#include <joy/joy.h>
#include "entt.hpp"
#include "../components/id_component.h"
#include "../components/logic_position_component.h"
#include <vector>
#include <algorithm>
#include <string>
#include <cstdio>
#include <cstring>

// 帧同步状态指纹：两端共用同一公式，保证 MD5 完全一致。
// 指纹来源：所有带 IdComponent + LogicPositionComponent 的实体（玩家 + 敌人 + 障碍物）。
struct ChecksumEntry
{
        int id;
        fp_t x, y, z;
};

// 收集当前 ECS 中所有参与指纹的实体（带 IdComponent + LogicPositionComponent）。
// 双端（服务端权威 + 客户端）在"模拟本帧之前"调用，保证集合与位置逐位一致。
static inline void collect_checksum_entries(entt::registry& registry, std::vector<ChecksumEntry>& entries)
{
        auto pos_view = registry.view<IdComponent, LogicPositionComponent>();
        for (auto e : pos_view) {
                auto& idc = pos_view.get<IdComponent>(e);
                auto& lpc = pos_view.get<LogicPositionComponent>(e);
                entries.push_back({ idc.id, lpc.x, lpc.y, lpc.z });
        }
}

// 计算状态 MD5（hex 32 字符 + '\0' 写入 out，out 需 >= 33 字节）。
// 公式（两端必须逐字节一致）：
//   1. 按 id 升序排序
//   2. 拼接 "%d:%lld:%lld:%lld,"（坐标按 long long 取整）
//   3. utils_md5x
// 注意：此函数在"模拟本帧之前"调用，与服务端 collect_input 的时机一致。
static inline void compute_full_md5(std::vector<ChecksumEntry>& entries, char* out, int out_cap)
{
        std::sort(entries.begin(), entries.end(),
                [](const ChecksumEntry& a, const ChecksumEntry& b) {
                        return a.id < b.id;
                });

        std::string s;
        char buf[64];
        for (auto& e : entries) {
                int n = snprintf(buf, sizeof(buf), "%d:%lld:%lld:%lld,",
                        e.id, (long long)e.x, (long long)e.y, (long long)e.z);
                s.append(buf, n);
        }

        if (out_cap <= 0) return;
        memset(out, 0, out_cap);
        utils_md5x(s.c_str(), s.size(), out);
}

// 把实体列表编成可读字符串 "id,x,y,z;id,x,y,z;..."（按 id 升序），用于校验记录落盘，
// 便于退出后 diff 出"多了/少了哪个实体、位置差多少"。
static inline std::string checksum_entries_string(std::vector<ChecksumEntry>& entries)
{
        std::sort(entries.begin(), entries.end(),
                [](const ChecksumEntry& a, const ChecksumEntry& b) {
                        return a.id < b.id;
                });
        std::string s;
        char buf[64];
        for (auto& e : entries) {
                int n = snprintf(buf, sizeof(buf), "%d,%lld,%lld,%lld;",
                        e.id, (long long)e.x, (long long)e.y, (long long)e.z);
                s.append(buf, n);
        }
        return s;
}

// 帧同步校验记录（缓存用，退出时落盘）。两端共用同一结构，字段对齐便于 diff：
//   frame_id  逻辑帧号
//   checksum  本帧校验值（客户端填本地重算值；服务端填自己算的权威值）
//   inputs    本帧输入指纹 "conv:dir:atk:yaw;..."（server 用本帧实际 inputs；client 用收到的 inputs）
//   entities  本帧所有带位置实体的 "id,x,y,z;..." 快照，用于定位集合/位置差异
// 注：不一致由客户端运行时用 printf 打到控制台，不进本记录；故只存一列 checksum。
struct ChecksumRecord {
        int      frame_id;
        char     checksum[64];
        std::string inputs;
        std::string entities;
};

#endif
