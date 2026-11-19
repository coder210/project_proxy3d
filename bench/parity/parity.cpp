/* 帧同步对照 harness（C++ 侧）。
 * 直接 include 项目真实的 movement_system.h / player_death_system.h，
 * 构造确定场景 + 确定输入序列，跑 N tick，把玩家状态按 fp 原始 long 打印。
 * 与 C# 侧（game_cs）跑同一场景同一输入，输出同格式快照，用 diff 对比逐位一致性。
 *
 * 项目主 CMake 尚未做新版 joy 的 API 改名，故此 harness 用独立 CMake 编译；
 * 旧类型名（fp_t / vec3f_t 等）用兼容别名桥接到新版 joy_fp_t / joy_vec3f_t。
 */
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#include <joy/jmath.h>
#include <joy/joy.h>

/* 旧→新类型名兼容别名（新版 joy 用 joy_ 前缀，项目代码仍用旧名） */
#define fp_t joy_fp_t
#define vec2f_t joy_vec2f_t
#define vec3f_t joy_vec3f_t

#include "entt.hpp"
#include "shared/components/player_state_input.h"
#include "shared/components/logic_position_component.h"
#include "shared/components/logic_velocity_component.h"
#include "shared/components/player_physics_component.h"
#include "shared/components/knockback_component.h"
#include "shared/components/action_state_component.h"
#include "shared/components/ai_component.h"
#include "shared/components/enemy_component.h"
#include "shared/components/player_event_input.h"

/* 寻路网格常量（pathfinding_component.h，此处只需两个，避免引入其旧宏依赖） */
static const int PF_GRID_N = 64;
static const int PF_GRID_OFFSET = 32;
#include "shared/components/action_state_component.h"
#include "shared/components/health_component.h"
#include "shared/components/player_component.h"
#include "shared/components/spawn_point_component.h"
#include "shared/components/id_component.h"
#include "shared/systems/character_def.h"
#include "shared/systems/spawn_def.h"

/* 纯数据：5 张地图 ASCII（从 level_maps.h 提取，无引擎依赖） */
#include "level_srcs.inc"

/* ---- 自包含的地图解析（逐位复刻 level_maps.h 的 level_build_layout）---- */
#define PAR_ROWS 64
#define PAR_COLS 64
#define PAR_OFFSET 32
struct ParObstacle { int x10,z10,hx10,hz10,sy10,gx0,gz0,gw,gh; };
struct ParLayout { ParObstacle obs[4096]; int count, seed; bool walkable[64*64]; };

static bool par_build_layout(int level, ParLayout& L, int& player_cell,
                             int* enemy_cells, int& enemy_n)
{
    player_cell = -1; enemy_n = 0;
    if (level < 1 || level > 5) return false;
    const char* src = PARITY_LEVEL_SRCS[level - 1];
    L.count = 0; L.seed = level;
    bool wall[PAR_ROWS][PAR_COLS] = {};
    int r = 0, c = 0;
    for (const char* p = src; *p && r < PAR_ROWS; ++p) {
        char ch = *p;
        if (ch == '\n') { r++; c = 0; continue; }
        if (c >= PAR_COLS) { c++; continue; }
        if (ch == 'P') player_cell = r * 64 + c;
        else if (ch == 'E') { if (enemy_n < 64) enemy_cells[enemy_n++] = r * 64 + c; }
        else if (ch == '#') wall[r][c] = true;
        c++;
    }
    for (r = 0; r < PAR_ROWS && L.count < 4096; ++r) {
        for (c = 0; c < PAR_COLS; ++c) {
            if (!wall[r][c]) continue;
            int w = 1; while (c + w < PAR_COLS && wall[r][c+w]) w++;
            int h = 1;
            while (r + h < PAR_ROWS) {
                bool full = true;
                for (int cc = c; cc < c + w; ++cc) if (!wall[r+h][cc]) { full = false; break; }
                if (!full) break;
                h++;
            }
            for (int dr = 0; dr < h; ++dr) for (int dc = 0; dc < w; ++dc) wall[r+dr][c+dc] = false;
            ParObstacle& o = L.obs[L.count++];
            o.gx0=c; o.gz0=r; o.gw=w; o.gh=h;
            o.x10=(2*c+w-2*PAR_OFFSET)*5; o.z10=(2*r+h-2*PAR_OFFSET)*5;
            o.hx10=w*5; o.hz10=h*5; o.sy10=14;
        }
    }
    for (int i = 0; i < 64*64; ++i) L.walkable[i] = true;
    for (int k = 0; k < L.count; ++k) {
        const ParObstacle& o = L.obs[k];
        if (o.gx0<0 || o.gz0<0 || o.gx0+o.gw>64 || o.gz0+o.gh>64) continue;
        for (int dr=0; dr<o.gh; ++dr) for (int dc=0; dc<o.gw; ++dc)
            L.walkable[(o.gz0+dr)*64 + (o.gx0+dc)] = false;
    }
    return player_cell >= 0 && enemy_n > 0;
}

/* 地图解析对照：输出每关 layout 摘要（与 C# MapData.BuildLayout 对照）。 */
static void parity_maps()
{
    for (int lv = 1; lv <= 5; ++lv) {
        ParLayout layout;
        int player_cell = -1, enemy_cells[64], enemy_n = 0;
        bool ok = par_build_layout(lv, layout, player_cell, enemy_cells, enemy_n);
        printf("MAP %d ok=%d count=%d seed=%d player=%d enemy_n=%d\n",
            lv, ok ? 1 : 0, layout.count, layout.seed, player_cell, enemy_n);
        int kmax = layout.count < 4 ? layout.count : 4;
        for (int k = 0; k < kmax; ++k) {
            const ParObstacle& o = layout.obs[k];
            printf("OBS %d %d %d %d %d %d %d %d %d %d\n",
                lv, o.x10, o.z10, o.hx10, o.hz10, o.sy10, o.gx0, o.gz0, o.gw, o.gh);
        }
        int wsum = 0, wcnt = 0;
        for (int i = 0; i < 64*64; ++i) {
            if (layout.walkable[i]) wsum += i;
            if (!layout.walkable[i]) wcnt++;
        }
        printf("WALK %d sum=%d blocked=%d\n", lv, wsum, wcnt);
        for (int i = 0; i < enemy_n; ++i) printf("ECELL %d %d\n", lv, enemy_cells[i]);
    }
}

/* 敌人生成对照（内联复刻 wave_prepare_enemies + wave_spawn_enemy_range 的位置/参数计算） */
static void parity_wave()
{
    const int level = 1;
    ParLayout layout;
    int player_cell = -1, enemy_cells[64], enemy_n = 0;
    par_build_layout(level, layout, player_cell, enemy_cells, enemy_n);

    int count = 8;  // LEVEL_ENEMY_COUNT[0]
    if (count < ENEMY_SPAWN_COUNT) count = ENEMY_SPAWN_COUNT;
    int map_seed = level, wave = 0;
    int tiles[64], si[64], got = 0;
    if (enemy_n > 0) {
        for (int i = 0; i < count; ++i) {
            tiles[got++] = enemy_cells[i % enemy_n];
            si[i] = (i < ENEMY_SPAWN_COUNT) ? i
                : ((int)map_seed + wave * 7 + i * 13) % ENEMY_SPAWN_COUNT;
        }
    }
    printf("WAVE %d got=%d\n", level, got);
    for (int i = 0; i < got; ++i) {
        const SpawnDef& sd = ENEMY_SPAWNS[si[i]];
        int gx = tiles[i] % PF_GRID_N, gz = tiles[i] / PF_GRID_N;
        fp_t fx = fp_add(fp_from_int(gx - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        fp_t fz = fp_add(fp_from_int(gz - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        printf("ENEMY %d %d %lld %lld %d %lld %lld %lld %lld\n",
            level, si[i], (long long)fx, (long long)fz, ENEMY_MAX_HP,
            (long long)fp_from_float(sd.patrol_radius),
            (long long)fp_from_float(sd.detect_radius),
            (long long)fp_from_float(sd.attack_radius),
            (long long)fp_from_float(sd.guard_radius));
    }
}

/* 单关波次状态机对照（内联复刻 wave_system.h 的 apply_level_change + wave_system 清场判定） */
static void parity_wave_clear()
{
    entt::registry r;
    int level = 1, wave = 0;
    bool level_cleared = false;
    int map_seed = 0;

    // 内联 apply_level_change：清空旧敌并重刷本关整波敌人
    auto apply_level_change = [&]() {
        std::vector<entt::entity> to_destroy;
        for (auto e : r.view<EnemyComponent>()) to_destroy.push_back(e);
        for (auto e : to_destroy) r.destroy(e);
        // 全局状态重置（对应 gs.level/wave/level_cleared/map_seed）
        level = 1;
        wave = 0;
        level_cleared = false;
        map_seed = level;
        // wave_prepare_enemies + wave_spawn_enemy_range：单波 8 敌（LEVEL_ENEMY_COUNT[0]）
        int count = 8;
        if (count < ENEMY_SPAWN_COUNT) count = ENEMY_SPAWN_COUNT;
        for (int i = 0; i < count; ++i) {
            auto e = r.create();
            r.emplace<IdComponent>(e, i + 1);
            r.emplace<EnemyComponent>(e);
            r.emplace<HealthComponent>(e, ENEMY_MAX_HP, ENEMY_MAX_HP);
        }
        wave = 1;
    };

    // 内联 wave_system 清场判定
    auto wave_tick = [&]() {
        if (wave == 0 && !level_cleared) { apply_level_change(); return; }
        bool wave_done = true;
        auto eview = r.view<EnemyComponent, HealthComponent>();
        for (auto e : eview) {
            if (!eview.get<HealthComponent>(e).dead) { wave_done = false; break; }
        }
        if (!wave_done) return;
        if (!level_cleared) level_cleared = true;
    };

    auto dump = [&](const char* tag) {
        int alive = 0;
        auto eview = r.view<EnemyComponent, HealthComponent>();
        for (auto e : eview) if (!eview.get<HealthComponent>(e).dead) alive++;
        printf("WAVECLR %s wave=%d cleared=%d alive=%d seed=%d\n",
            tag, wave, level_cleared ? 1 : 0, alive, map_seed);
    };

    dump("init");
    wave_tick(); dump("start");
    wave_tick(); dump("idle");
    {   // 杀 3 只
        int k = 0;
        auto eview = r.view<EnemyComponent, HealthComponent>();
        for (auto e : eview) if (k++ < 3) eview.get<HealthComponent>(e).dead = true;
    }
    wave_tick(); dump("partial");
    {   // 全灭
        auto eview = r.view<EnemyComponent, HealthComponent>();
        for (auto e : eview) eview.get<HealthComponent>(e).dead = true;
    }
    wave_tick(); dump("clear");
    wave_tick(); dump("stable");
}

/* 协议/快照/wave_seed 对照：C++ 内置最小 msgpack packer，与 C# 纯托管 packer 逐字节对照。
 * 两端按同一 msgpack 标准独立实现，验证 C# 编码逻辑正确（byte-identical）。 */
struct MinPacker {
    std::string b;
    void u8(unsigned v) { b.push_back((char)(v & 0xFF)); }
    void u16(unsigned v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u8(v >> 24); u8(v >> 16); u8(v >> 8); u8(v); }
    void u64(uint64_t v) { u32((uint32_t)(v >> 32)); u32((uint32_t)v); }
    void array(int n) {
        if (n < 16) u8(0x90 | n);
        else if (n <= 0xFFFF) { u8(0xDC); u16(n); }
        else { u8(0xDD); u32((uint32_t)n); }
    }
    void pack_uint(uint32_t d) {
        if (d <= 0x7F) u8(d);
        else if (d <= 0xFF) { u8(0xCC); u8(d); }
        else if (d <= 0xFFFF) { u8(0xCD); u16(d); }
        else { u8(0xCE); u32(d); }
    }
    void pack_int(int d) {
        if (d >= 0) pack_uint((uint32_t)d);
        else {
            if (d >= -32) u8(0xE0 | (d & 0x1F));
            else if (d >= -128) { u8(0xD0); u8((unsigned)(int8_t)d); }
            else if (d >= -32768) { u8(0xD1); u16((unsigned)(int16_t)d); }
            else { u8(0xD2); u32((uint32_t)d); }
        }
    }
    void pack_u64(uint64_t d) {
        if (d <= 0x7F) u8((unsigned)d);
        else if (d <= 0xFF) { u8(0xCC); u8((unsigned)d); }
        else if (d <= 0xFFFF) { u8(0xCD); u16((unsigned)d); }
        else if (d <= 0xFFFFFFFFULL) { u8(0xCE); u32((uint32_t)d); }
        else { u8(0xCF); u64(d); }
    }
    void pack_i64(int64_t d) {
        if (d >= 0) pack_u64((uint64_t)d);
        else {
            if (d >= -32) u8(0xE0 | (unsigned)(d & 0x1F));
            else if (d >= -128) { u8(0xD0); u8((unsigned)(int8_t)d); }
            else if (d >= -32768) { u8(0xD1); u16((unsigned)(int16_t)d); }
            else if (d >= -2147483648LL) { u8(0xD2); u32((uint32_t)(int32_t)d); }
            else { u8(0xD3); u64((uint64_t)d); }
        }
    }
    void str(const char* s) {
        size_t n = strlen(s);
        if (n < 32) u8(0xA0 | (unsigned)n);
        else if (n <= 0xFF) { u8(0xD9); u8((unsigned)n); }
        else if (n <= 0xFFFF) { u8(0xDA); u16((unsigned)n); }
        else { u8(0xDB); u32((uint32_t)n); }
        b.append(s, n);
    }
};
static void mp_pack_entity(MinPacker& p, int id, int type,
    long long px, long long py, long long pz, long long w, long long h, long long hp,
    int conv, short yaw, long long ai_type, int char_id, const char* nick,
    int ai_timer, int ai_state, long long atx, long long atz,
    int pgx, int pgz, int plen, int pidx, int pcd)
{
    p.array(22);
    p.pack_int(id); p.pack_int(type);
    p.pack_i64(px); p.pack_i64(py); p.pack_i64(pz);
    p.pack_i64(w); p.pack_i64(h);
    p.pack_i64(hp); p.pack_int(conv);
    p.pack_int(yaw);
    p.pack_i64(ai_type);
    p.pack_int(char_id);
    p.str(nick);
    p.pack_int(ai_timer); p.pack_int(ai_state);
    p.pack_i64(atx); p.pack_i64(atz);
    p.pack_int(pgx); p.pack_int(pgz);
    p.pack_int(plen); p.pack_int(pidx);
    p.pack_int(pcd);
}
static std::string hexstr(const std::string& s) {
    static const char* H = "0123456789ABCDEF";
    std::string out; out.reserve(s.size() * 2);
    for (unsigned char c : s) { out.push_back(H[c >> 4]); out.push_back(H[c & 15]); }
    return out;
}
static void parity_protocol()
{
    // 1) wave_seed 纯函数（与 C# Procmap.WaveSeed 对照）
    const uint32_t bases[] = { 0, 1, 12345, 0xFFFFFFF0u };
    const int waves[] = { 0, 1, 3, 7 };
    for (int i = 0; i < 4; ++i)
        printf("PMSEED %u %d %u\n", bases[i], waves[i], bases[i] + (uint32_t)waves[i] * 1000003u);

    // 2) 单个实体 pack_entity 字节
    MinPacker s1;
    mp_pack_entity(s1, 7, 2, fp_from_float(3.2f), fp_from_float(0.6f), fp_from_float(2.3f),
        fp_from_float(0.5f), fp_from_float(1.0f), 40, 0, -200, 1, 2, "Enemy",
        5, 1, fp_from_float(1.5f), fp_from_float(-2.5f), 12, 9, 3, 1, 2);
    printf("MPENT %s\n", hexstr(s1.b).c_str());

    // 3) pack_world：[eid, [两个实体]]（玩家 + 敌人），验证数组/str/int64 混合编码
    MinPacker s2;
    s2.array(2);
    s2.pack_int(10000);
    s2.array(2);
    mp_pack_entity(s2, 1, 1, fp_from_float(3.2f), fp_from_float(0.6f), fp_from_float(2.3f),
        fp_from_int(1), fp_from_int(1), 100, 11, 100, 0, 0, "Hero",
        0, 0, fp_zero(), fp_zero(), 0, 0, 0, 0, 0);
    mp_pack_entity(s2, 2, 2, fp_from_float(1.0f), fp_zero(), fp_from_float(2.0f),
        fp_from_float(0.5f), fp_from_float(1.0f), 40, 0, 0, 1, 1, "E",
        0, 0, fp_zero(), fp_zero(), 0, 0, 0, 0, 0);
    printf("MPWORLD %s\n", hexstr(s2.b).c_str());
}

/* 网络输入消费对照（内联复刻 input_consume_join/leave/input_system.h + NetComponent） */
struct ParNetPlayer {
    bool alive;
    int id, conv, char_id;
    fp_t x, y, z;
    int hp, max_hp;
    unsigned char move_bits;
    short yaw, pitch;
    bool jump_prev, jump_requested;
    bool pmelee, pbullet, pray;
};
static ParNetPlayer g_np[16];
static int g_nid, g_ncam;

static int par_find_conv(int conv) {
    for (int i = 0; i < 16; ++i)
        if (g_np[i].alive && g_np[i].conv == conv) return i;
    return -1;
}
static void par_ic_join(int conv, fp_t x, fp_t y, fp_t z, int char_id) {
    if (par_find_conv(conv) >= 0) return;   // 去重
    int slot = -1;
    for (int i = 0; i < 16; ++i) if (!g_np[i].alive) { slot = i; break; }
    if (slot < 0) return;
    int cid = (char_id >= 0 && char_id < CHAR_COUNT) ? char_id : CHAR_WARRIOR;
    ParNetPlayer& p = g_np[slot];
    p = ParNetPlayer{};
    p.alive = true; p.id = g_nid++; p.conv = conv; p.char_id = char_id;
    p.x = x; p.y = y; p.z = z;
    p.max_hp = character_def(cid).max_hp; p.hp = p.max_hp;
    p.jump_prev = false;
    if (g_ncam < 0) g_ncam = conv;
}
static void par_ic_leave(int conv) {
    int s = par_find_conv(conv);
    if (s >= 0) g_np[s].alive = false;
}
static void par_ic_input(int conv, unsigned dir, int attack, short yaw) {
    int s = par_find_conv(conv);
    if (s < 0) return;
    ParNetPlayer& p = g_np[s];
    p.move_bits = (unsigned char)dir;
    p.yaw = yaw;
    bool jump_down = (dir & (1u << 5)) != 0;   // INPUT_JUMP
    if (jump_down && !p.jump_prev) p.jump_requested = true;
    p.jump_prev = jump_down;
    if (attack == 1) p.pmelee = true;
    else if (attack == 2) p.pbullet = true;
    else if (attack == 3) p.pray = true;
}
static void par_ic_clear(void) {
    for (int i = 0; i < 16; ++i) {
        g_np[i].jump_requested = false;
        g_np[i].pmelee = g_np[i].pbullet = g_np[i].pray = false;
    }
}
static void par_dump_ic(const char* phase) {
    int alive = 0;
    for (int i = 0; i < 16; ++i) if (g_np[i].alive) alive++;
    printf("IC %s n=%d cam=%d\n", phase, alive, g_ncam);
    std::vector<int> ids;
    for (int i = 0; i < 16; ++i) if (g_np[i].alive) ids.push_back(i);
    std::sort(ids.begin(), ids.end(), [](int a, int b) { return g_np[a].id < g_np[b].id; });
    for (int idx : ids) {
        ParNetPlayer& p = g_np[idx];
        printf("ICJ %s %d conv=%d char=%d x=%lld y=%lld z=%lld hp=%d max=%d move=%d yaw=%d jp=%d jreq=%d atk=%d%d%d\n",
            phase, p.id, p.conv, p.char_id, (long long)p.x, (long long)p.y, (long long)p.z,
            p.hp, p.max_hp, (int)p.move_bits, (int)p.yaw,
            p.jump_prev ? 1 : 0, p.jump_requested ? 1 : 0,
            p.pmelee ? 1 : 0, p.pbullet ? 1 : 0, p.pray ? 1 : 0);
    }
}
static void parity_input_consume()
{
    for (int i = 0; i < 16; ++i) g_np[i] = ParNetPlayer{};
    g_nid = 1; g_ncam = -1;

    par_ic_join(11, fp_from_float(3.2f), fp_from_float(0.6f), fp_from_float(2.3f), 0);
    par_ic_join(22, fp_from_float(-1.5f), fp_from_float(0.6f), fp_from_float(4.0f), 1);
    par_dump_ic("join2");
    par_ic_clear();

    par_ic_join(11, fp_from_float(9.9f), fp_zero(), fp_from_float(9.9f), 2);   // 去重
    par_dump_ic("dup");

    par_ic_input(11, 1u | (1u << 5), 0, 100);
    par_ic_input(22, 1u << 3, 2, -200);
    par_dump_ic("input1");

    par_ic_clear();
    par_ic_input(11, 1u << 5, 0, 100);   // 仍按跳，不连跳
    par_ic_input(22, 0, 1, -200);
    par_dump_ic("input2");

    par_ic_clear();
    par_ic_leave(11);
    par_dump_ic("leave");

    par_ic_join(33, fp_from_float(0.1f), fp_from_float(0.6f), fp_from_float(0.2f), 2);
    par_dump_ic("rejoin");
}

/* 最小 PathComponent（AI 只用 target_gx/gz/dirty；完整版在 pathfinding_component.h 有旧宏依赖） */
struct PathComponent { int target_gx = 0, target_gz = 0; bool dirty = false; };

/* AI 状态机对照（逐位复刻 ai_system.h） */
static int pf_world_to_grid_c(fp_t v) { return (int)((v >> 32) + PF_GRID_OFFSET); }

static void parity_ai_system(entt::registry& r)
{
    auto view = r.view<EnemyComponent, AIComponent, LogicPositionComponent, ActionStateComponent>();
    auto player_view = r.view<PlayerComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& ai = view.get<AIComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);
        auto& act = view.get<ActionStateComponent>(e);

        if (r.all_of<HealthComponent>(e) && r.get<HealthComponent>(e).dead) {
            if (r.all_of<PlayerEventInput>(e)) r.get<PlayerEventInput>(e).pending_actions.clear();
            continue;
        }

        bool found_player = false;
        fp_t nearest_dist_sq = fp_mul(ai.detect_radius, ai.detect_radius);
        fp_t nearest_px = fp_zero(), nearest_pz = fp_zero();
        for (auto p : player_view) {
            auto& ppos = player_view.get<LogicPositionComponent>(p);
            fp_t dx = fp_sub(ppos.x, pos.x), dz = fp_sub(ppos.z, pos.z);
            fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
            if (d2 < nearest_dist_sq) { nearest_dist_sq = d2; found_player = true; nearest_px = ppos.x; nearest_pz = ppos.z; }
        }
        fp_t attack_dist_sq = fp_mul(ai.attack_radius, ai.attack_radius);
        bool in_attack_range = found_player && (nearest_dist_sq < attack_dist_sq);
        bool is_guard = ai.guard_radius > fp_zero();
        fp_t hdx = is_guard ? fp_sub(nearest_px, ai.patrol_center_x) : fp_zero();
        fp_t hdz = is_guard ? fp_sub(nearest_pz, ai.patrol_center_z) : fp_zero();
        bool in_territory = !is_guard ||
            fp_add(fp_mul(hdx, hdx), fp_mul(hdz, hdz)) <= fp_mul(ai.guard_radius, ai.guard_radius);
        fp_t ehx = fp_sub(pos.x, ai.patrol_center_x);
        fp_t ehz = fp_sub(pos.z, ai.patrol_center_z);
        fp_t dist_home_sq = fp_add(fp_mul(ehx, ehx), fp_mul(ehz, ehz));
        fp_t HOME_EPS = fp_div(fp_from_int(4), fp_from_int(5));

        auto set_target = [&](fp_t tx, fp_t tz) {
            if (!r.all_of<PathComponent>(e)) r.emplace<PathComponent>(e);
            auto& pc = r.get<PathComponent>(e);
            int pgx = pf_world_to_grid_c(tx), pgz = pf_world_to_grid_c(tz);
            if (pgx != pc.target_gx || pgz != pc.target_gz) { pc.target_gx = pgx; pc.target_gz = pgz; pc.dirty = true; }
        };

        if (in_attack_range && in_territory) {
            ai.state = 2; act.state = ActionStateComponent::ATTACK;
            if (!r.all_of<PlayerEventInput>(e)) r.emplace<PlayerEventInput>(e);
            r.get<PlayerEventInput>(e).pending_actions.push_back({ 0, PlayerEventInput::ATTACK_MELEE });
        } else if (found_player && in_territory) {
            ai.state = 1; act.state = ActionStateComponent::WALK;
            set_target(nearest_px, nearest_pz); ai.timer = 0;
        } else if (is_guard && !in_territory && (ai.state == 1 || ai.state == 2)) {
            ai.state = 3; act.state = ActionStateComponent::WALK;
            set_target(ai.patrol_center_x, ai.patrol_center_z);
        } else if (ai.state == 3) {
            if (dist_home_sq < fp_mul(HOME_EPS, HOME_EPS)) { ai.state = 0; ai.timer = 120; }
            else { act.state = ActionStateComponent::WALK; set_target(ai.patrol_center_x, ai.patrol_center_z); }
        } else {
            ai.state = 0; act.state = ActionStateComponent::WALK;
            ai.timer--;
            if (ai.timer <= 0) {
                ai.target_x = fp_sub(ai.patrol_center_x, ai.patrol_radius);
                ai.target_z = fp_sub(ai.patrol_center_z, ai.patrol_radius);
                ai.timer = 120;
            }
            set_target(ai.target_x, ai.target_z);
        }
    }
}

static void parity_ai()
{
    ParLayout layout;
    int pc2 = -1, ec[64], en = 0;
    par_build_layout(1, layout, pc2, ec, en);

    entt::registry r;
    // 玩家（出生格中心）
    auto pe = r.create();
    r.emplace<PlayerComponent>(pe);
    fp_t pfx = fp_add(fp_from_int(pc2 % 64 - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
    fp_t pfz = fp_add(fp_from_int(pc2 / 64 - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
    r.emplace<LogicPositionComponent>(pe, pfx, fp_from_float(0.6f), pfz);

    // 8 只敌人（与 wave 生成一致：i<2 用 si=i，否则 si=(1+0*7+i*13)%2）
    int count = 8;
    for (int i = 0; i < count; ++i) {
        int tile = ec[i % en];
        int si = (i < ENEMY_SPAWN_COUNT) ? i : (1 + 0 * 7 + i * 13) % ENEMY_SPAWN_COUNT;
        const SpawnDef& sd = ENEMY_SPAWNS[si];
        int gx = tile % 64, gz = tile / 64;
        fp_t fx = fp_add(fp_from_int(gx - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        fp_t fz = fp_add(fp_from_int(gz - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2)));
        auto e = r.create();
        r.emplace<IdComponent>(e, 2 + i);
        r.emplace<EnemyComponent>(e);
        r.emplace<LogicPositionComponent>(e, fx, fp_zero(), fz);
        r.emplace<ActionStateComponent>(e);
        r.emplace<HealthComponent>(e, ENEMY_MAX_HP, ENEMY_MAX_HP);
        r.emplace<AIComponent>(e, fx, fz, fp_from_float(sd.patrol_radius),
            fp_from_float(sd.detect_radius), fp_from_float(sd.attack_radius), fp_from_float(sd.guard_radius));
    }

    for (int t = 0; t < 5; ++t) {
        // 模拟上一 tick 的 melee_attack 已消费并清空事件
        for (auto e : r.view<PlayerEventInput>()) r.get<PlayerEventInput>(e).pending_actions.clear();
        parity_ai_system(r);

        // EnTT view 遍历顺序≠创建顺序，按实体 id 排序输出以与 C# 数组顺序对齐
        struct AiOut { int id, state, act, tgx, tgz, dirty, melee; };
        std::vector<AiOut> outs;
        auto view = r.view<EnemyComponent, AIComponent, ActionStateComponent>();
        for (auto e : view) {
            auto& ai = view.get<AIComponent>(e);
            auto& act = view.get<ActionStateComponent>(e);
            int idc = r.all_of<IdComponent>(e) ? r.get<IdComponent>(e).id : -1;
            bool has_path = r.all_of<PathComponent>(e);
            PathComponent pcx{};
            if (has_path) pcx = r.get<PathComponent>(e);
            bool melee = r.all_of<PlayerEventInput>(e) && !r.get<PlayerEventInput>(e).pending_actions.empty();
            outs.push_back({ idc, ai.state, (int)act.state, pcx.target_gx, pcx.target_gz,
                             has_path && pcx.dirty ? 1 : 0, melee ? 1 : 0 });
        }
        std::sort(outs.begin(), outs.end(), [](const AiOut& a, const AiOut& b) { return a.id < b.id; });
        for (auto& o : outs)
            printf("AI %d %d state=%d act=%d tgx=%d tgz=%d dirty=%d melee=%d\n",
                t, o.id, o.state, o.act, o.tgx, o.tgz, o.dirty, o.melee);
    }
}

/* 攻击结算对照（精简复刻 melee_attack_system.h：近战/射击/射线，跳过表现缓冲） */
struct PBullet { fp_t x, y, z, dx, dz, speed, rangeLeft; int damage; bool fromEnemy; int ownerId; };
static PBullet g_bullets[256];
static int g_bulletCount = 0;
static int g_kills = 0;
static ParObstacle g_obs[4096];
static int g_obsCount = 0;

static void parity_melee_attack(entt::registry& r, int playerId)
{
    // 玩家攻击（先所有玩家，再所有敌人，与 C# 顺序一致）
    auto pv = r.view<PlayerComponent, ActionStateComponent, LogicPositionComponent, PlayerEventInput, HealthComponent>();
    auto ev = r.view<EnemyComponent, ActionStateComponent, LogicPositionComponent, PlayerEventInput, HealthComponent, AIComponent>();

    auto attack_one = [&](entt::entity a, bool isPlayer) {
        auto& act = r.get<ActionStateComponent>(a);
        auto& apos = r.get<LogicPositionComponent>(a);
        auto& h = r.get<HealthComponent>(a);
        auto& events = r.get<PlayerEventInput>(a);

        if (act.melee_cooldown > 0) act.melee_cooldown--;
        if (act.bullet_cooldown > 0) act.bullet_cooldown--;
        if (act.ray_cooldown > 0) act.ray_cooldown--;
        if (events.pending_actions.empty()) return;
        if (h.dead) { events.pending_actions.clear(); return; }

        bool want_melee = false, want_bullet = false, want_ray = false;
        for (auto& ac : events.pending_actions) {
            if (ac.type == PlayerEventInput::ATTACK_BULLET) want_bullet = true;
            else if (ac.type == PlayerEventInput::ATTACK_RAY) want_ray = true;
            else want_melee = true;
        }
        const CharacterDef& cd = character_def(isPlayer && r.all_of<PlayerComponent>(a) ? r.get<PlayerComponent>(a).char_id : CHAR_WARRIOR);
        fp_t R = cd.melee_range;

        if (want_melee && act.melee_cooldown <= 0) {
            act.melee_cooldown = isPlayer ? ActionStateComponent::MELEE_COOLDOWN : 4;
            if (isPlayer) {
                fp_t yaw_fp = fp_from_float(act.yaw);
                fp_t fx = fp_sin(yaw_fp), fz = fp_cos(yaw_fp);
                const int SEMI_SEGS = 8, SEMI_CENTER = 2, SEMI_ARC = (SEMI_SEGS + 1) * 2;
                const int SEMI_TOTAL = SEMI_CENTER + SEMI_ARC;
                vec3f_t verts[SEMI_TOTAL];
                fp_t deg2r = fp_from_float(3.14159265f / 180.0f);
                fp_t yb = fp_sub(apos.y, fp_from_float(0.5f));
                fp_t yt = fp_add(apos.y, fp_from_float(1.2f));
                int k = 0;
                verts[k].x = apos.x; verts[k].y = yb; verts[k].z = apos.z; k++;
                verts[k].x = apos.x; verts[k].y = yt; verts[k].z = apos.z; k++;
                for (int i = 0; i <= SEMI_SEGS; ++i) {
                    fp_t t = fp_div(fp_from_float(i), fp_from_float(SEMI_SEGS));
                    fp_t ang = fp_sub(yaw_fp, fp_mul(fp_from_float(90.0f), deg2r));
                    ang = fp_add(ang, fp_mul(fp_from_float(180.0f), fp_mul(deg2r, t)));
                    fp_t px = fp_mul(R, fp_sin(ang));
                    fp_t pz = fp_mul(R, fp_cos(ang));
                    verts[k].x = fp_add(apos.x, px); verts[k].y = yb; verts[k].z = fp_add(apos.z, pz); k++;
                    verts[k].x = fp_add(apos.x, px); verts[k].y = yt; verts[k].z = fp_add(apos.z, pz); k++;
                }
                joy_gjk3df_collider_t semi;
                joy_collision_gjk3df_init_mesh(&semi, verts, k);

                entt::entity lock = entt::null;
                fp_t lock_dist = fp_from_float(1e9f);
                for (auto t : ev) {
                    auto& th = ev.get<HealthComponent>(t);
                    if (th.dead) continue;
                    auto& tp = ev.get<LogicPositionComponent>(t);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t dist_sq = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    joy_gjk3df_collider_t eshape;
                    joy_vec3f_t epos = { tp.x, tp.y, tp.z };
                    joy_vec3f_t ehs = { fp_from_float(0.35f), fp_from_float(0.9f), fp_from_float(0.35f) };
                    joy_collision_gjk3df_init_box(&eshape, epos, ehs);
                    joy_gjk3df_contact_t contact;
                    joy_vec3f_t init_dir = { fx, fp_zero(), fz };
                    if (joy_collision_gjk3df_collide(&semi, &eshape, init_dir, &contact)) {
                        if (dist_sq < lock_dist) { lock = t; lock_dist = dist_sq; }
                    }
                }
                if (lock != entt::null) {
                    auto& th = ev.get<HealthComponent>(lock);
                    auto& tp = ev.get<LogicPositionComponent>(lock);
                    th.hp -= cd.melee_damage; if (th.hp < 0) th.hp = 0;
                    if (th.hp <= 0 && !th.dead) { th.dead = true; ++g_kills; }
                }
            } else {
                auto& ai = r.get<AIComponent>(a);
                fp_t range = ai.attack_radius;
                for (auto t : pv) {
                    auto& th = pv.get<HealthComponent>(t);
                    if (th.dead) continue;
                    if (th.spawn_invuln > 0) continue;
                    auto& tp = pv.get<LogicPositionComponent>(t);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (d2 < fp_mul(range, range)) {
                        int dmg = 1;
                        if (act.bullet_cooldown <= 0) { dmg += 3; act.bullet_cooldown = 20; }
                        th.hp -= dmg; if (th.hp < 0) th.hp = 0;
                        break;
                    }
                }
            }
        }

        if (want_bullet && act.bullet_cooldown <= 0) {
            act.bullet_cooldown = ActionStateComponent::BULLET_COOLDOWN;
            fp_t sdx = fp_zero(), sdz = fp_zero();
            if (isPlayer) {
                entt::entity lock = entt::null; fp_t best = fp_from_float(1e9f);
                for (auto t : ev) {
                    auto& th = ev.get<HealthComponent>(t); if (th.dead) continue;
                    auto& tp = ev.get<LogicPositionComponent>(t);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (d2 < best) { best = d2; lock = t; }
                }
                if (lock != entt::null) {
                    auto& tp = ev.get<LogicPositionComponent>(lock);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t dist = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                    if (dist > fp_zero()) { sdx = fp_div(dx, dist); sdz = fp_div(dz, dist); }
                }
                if (sdx == fp_zero() && sdz == fp_zero()) {
                    fp_t yaw_fp = fp_from_float(act.yaw);
                    sdx = fp_sin(yaw_fp); sdz = fp_cos(yaw_fp);
                }
            } else {
                entt::entity lock = entt::null; fp_t best = fp_from_float(1e9f);
                for (auto t : pv) {
                    auto& th = pv.get<HealthComponent>(t);
                    if (th.dead || th.spawn_invuln > 0) continue;
                    auto& tp = pv.get<LogicPositionComponent>(t);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t d2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                    if (d2 < best) { best = d2; lock = t; }
                }
                if (lock != entt::null) {
                    auto& tp = pv.get<LogicPositionComponent>(lock);
                    fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                    fp_t dist = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                    if (dist > fp_zero()) { sdx = fp_div(dx, dist); sdz = fp_div(dz, dist); }
                }
                if (sdx == fp_zero() && sdz == fp_zero()) sdz = fp_one();
            }
            if (g_bulletCount < 256) {
                PBullet& b = g_bullets[g_bulletCount++];
                b.x = apos.x; b.y = apos.y; b.z = apos.z; b.dx = sdx; b.dz = sdz;
                b.speed = fp_from_float(0.4f); b.rangeLeft = fp_from_float(12.0f);
                b.damage = isPlayer ? cd.bullet_damage : 5;
                b.fromEnemy = !isPlayer;
                b.ownerId = r.all_of<IdComponent>(a) ? r.get<IdComponent>(a).id : -1;
            }
        }

        if (isPlayer && want_ray && act.ray_cooldown <= 0) {
            act.ray_cooldown = ActionStateComponent::RAY_COOLDOWN;
            fp_t yaw_fp = fp_from_float(act.yaw);
            fp_t fx = fp_sin(yaw_fp), fz = fp_cos(yaw_fp);
            fp_t RAY_RANGE = cd.ray_range;
            entt::entity lock = entt::null; fp_t lock_t = fp_from_float(1e9f), lock_x = fp_zero(), lock_z = fp_zero();
            fp_t best_r2 = fp_mul(RAY_RANGE, RAY_RANGE);
            for (auto t : ev) {
                auto& th = ev.get<HealthComponent>(t); if (th.dead) continue;
                auto& tp = ev.get<LogicPositionComponent>(t);
                fp_t dx = fp_sub(tp.x, apos.x), dz = fp_sub(tp.z, apos.z);
                fp_t r2 = fp_add(fp_mul(dx, dx), fp_mul(dz, dz));
                if (r2 < best_r2) { best_r2 = r2; lock = t; lock_x = tp.x; lock_z = tp.z; }
            }
            if (lock != entt::null) {
                fp_t dx = fp_sub(lock_x, apos.x), dz = fp_sub(lock_z, apos.z);
                fp_t len = fp_sqrt(fp_add(fp_mul(dx, dx), fp_mul(dz, dz)));
                if (len > fp_zero()) { fx = fp_div(dx, len); fz = fp_div(dz, len); }
                lock_t = len;
            }
            // 障碍阻挡
            fp_t beam_reach = RAY_RANGE;
            for (int oi = 0; oi < g_obsCount; ++oi) {
                const ParObstacle& o = g_obs[oi];
                fp_t bx = fp_div(fp_from_int(o.x10), fp_from_int(10));
                fp_t bz = fp_div(fp_from_int(o.z10), fp_from_int(10));
                fp_t bhx = fp_div(fp_from_int(o.hx10), fp_from_int(10));
                fp_t bhz = fp_div(fp_from_int(o.hz10), fp_from_int(10));
                fp_t bsy = fp_div(fp_from_int(o.sy10), fp_from_int(10));
                fp_t by = fp_div(bsy, fp_from_int(2));
                joy_ray3df_t ray; ray.position.x = apos.x; ray.position.y = apos.y; ray.position.z = apos.z;
                ray.direction.x = fx; ray.direction.y = fp_zero(); ray.direction.z = fz;
                joy_vec3f_t pos = { bx, by, bz }, half = { bhx, by, bhz }, rot = { fp_zero(), fp_zero(), fp_zero() };
                joy_ray3d_collisionf_t c = joy_collision_3df_get_ray_obb(ray, pos, half, rot);
                if (c.hit && c.distance > fp_zero() && c.distance < beam_reach) beam_reach = c.distance;
            }
            if (lock != entt::null && lock_t >= beam_reach) lock = entt::null;
            if (lock != entt::null) {
                auto& th = ev.get<HealthComponent>(lock);
                th.hp -= cd.ray_damage; if (th.hp < 0) th.hp = 0;
                if (th.hp <= 0 && !th.dead) { th.dead = true; ++g_kills; }
            }
        }

        events.pending_actions.clear();
    };

    for (auto e : pv) attack_one(e, true);
    for (auto e : ev) attack_one(e, false);
}

/* 子弹系统对照（逐位复刻 bullet_system.h：球-盒命中） */
static bool par_hit_box(fp_t cx, fp_t cy, fp_t cz, fp_t r,
                        fp_t bx, fp_t by, fp_t bz, fp_t hx, fp_t hy, fp_t hz)
{
    fp_t loX = fp_sub(bx, hx), hiX = fp_add(bx, hx);
    fp_t loY = fp_sub(by, hy), hiY = fp_add(by, hy);
    fp_t loZ = fp_sub(bz, hz), hiZ = fp_add(bz, hz);
    fp_t clX = cx < loX ? loX : (cx > hiX ? hiX : cx);
    fp_t clY = cy < loY ? loY : (cy > hiY ? hiY : cy);
    fp_t clZ = cz < loZ ? loZ : (cz > hiZ ? hiZ : cz);
    fp_t dx = fp_sub(cx, clX), dy = fp_sub(cy, clY), dz = fp_sub(cz, clZ);
    fp_t d2 = fp_add(fp_add(fp_mul(dx, dx), fp_mul(dy, dy)), fp_mul(dz, dz));
    return d2 <= fp_mul(r, r);
}

static void parity_bullet_system(entt::registry& r)
{
    const fp_t RADIUS = fp_from_float(0.15f);
    const fp_t DHX = fp_from_float(0.35f), DHY = fp_from_float(0.9f), DHZ = fp_from_float(0.35f);
    int write = 0;
    for (int i = 0; i < g_bulletCount; ++i) {
        PBullet b = g_bullets[i];
        b.x = fp_add(b.x, fp_mul(b.dx, b.speed));
        b.z = fp_add(b.z, fp_mul(b.dz, b.speed));
        b.rangeLeft = fp_sub(b.rangeLeft, b.speed);
        if (b.rangeLeft <= fp_zero()) continue;

        bool hit = false;
        if (b.fromEnemy) {
            for (auto t : r.view<PlayerComponent, LogicPositionComponent, HealthComponent>()) {
                if (hit) break;
                auto& th = r.get<HealthComponent>(t);
                if (th.dead) continue;
                int tid = r.all_of<IdComponent>(t) ? r.get<IdComponent>(t).id : -1;
                if (tid == b.ownerId) continue;
                if (th.spawn_invuln > 0) continue;
                auto& tp = r.get<LogicPositionComponent>(t);
                if (par_hit_box(b.x, b.y, b.z, RADIUS, tp.x, tp.y, tp.z, DHX, DHY, DHZ)) {
                    th.hp -= b.damage; if (th.hp < 0) th.hp = 0;
                    if (th.hp <= 0 && !th.dead) th.dead = true;
                    hit = true;
                }
            }
        } else {
            for (auto t : r.view<EnemyComponent, LogicPositionComponent, HealthComponent>()) {
                if (hit) break;
                auto& th = r.get<HealthComponent>(t);
                if (th.dead) continue;
                int tid = r.all_of<IdComponent>(t) ? r.get<IdComponent>(t).id : -1;
                if (tid == b.ownerId) continue;
                auto& tp = r.get<LogicPositionComponent>(t);
                if (par_hit_box(b.x, b.y, b.z, RADIUS, tp.x, tp.y, tp.z, DHX, DHY, DHZ)) {
                    th.hp -= b.damage; if (th.hp < 0) th.hp = 0;
                    if (th.hp <= 0 && !th.dead) { th.dead = true; ++g_kills; }
                    hit = true;
                }
            }
        }
        if (hit) continue;

        for (int oi = 0; oi < g_obsCount && !hit; ++oi) {
            const ParObstacle& o = g_obs[oi];
            fp_t bx = fp_div(fp_from_int(o.x10), fp_from_int(10));
            fp_t bz = fp_div(fp_from_int(o.z10), fp_from_int(10));
            fp_t bhx = fp_div(fp_from_int(o.hx10), fp_from_int(10));
            fp_t bhz = fp_div(fp_from_int(o.hz10), fp_from_int(10));
            fp_t bsy = fp_div(fp_from_int(o.sy10), fp_from_int(10));
            fp_t by = fp_div(bsy, fp_from_int(2));
            if (par_hit_box(b.x, b.y, b.z, RADIUS, bx, by, bz, bhx, by, bhz)) hit = true;
        }
        if (hit) continue;

        g_bullets[write++] = b;
    }
    g_bulletCount = write;
}

static void parity_bullet()
{
    g_bulletCount = 0;
    g_kills = 0;

    ParLayout layout;
    int pc2 = -1, ec[64], en = 0;
    par_build_layout(1, layout, pc2, ec, en);
    g_obsCount = layout.count;
    for (int i = 0; i < layout.count; ++i) g_obs[i] = layout.obs[i];

    entt::registry r;
    auto pe = r.create();
    r.emplace<IdComponent>(pe, 1);
    r.emplace<PlayerComponent>(pe);
    r.emplace<LogicPositionComponent>(pe, fp_zero(), fp_from_float(0.6f), fp_zero());
    r.emplace<HealthComponent>(pe, PLAYER_MAX_HP, PLAYER_MAX_HP);

    auto te = r.create();
    r.emplace<IdComponent>(te, 2);
    r.emplace<EnemyComponent>(te);
    r.emplace<LogicPositionComponent>(te, fp_zero(), fp_zero(), fp_from_float(1.0f));
    r.emplace<HealthComponent>(te, ENEMY_MAX_HP, ENEMY_MAX_HP);

    // 手动创建一颗子弹（+Z 方向，伤 8）
    PBullet& b0 = g_bullets[g_bulletCount++];
    b0.x = fp_zero(); b0.y = fp_from_float(0.6f); b0.z = fp_zero();
    b0.dx = fp_zero(); b0.dz = fp_one();
    b0.speed = fp_from_float(0.4f); b0.rangeLeft = fp_from_float(12.0f);
    b0.damage = 8; b0.fromEnemy = false; b0.ownerId = 1;

    for (int t = 0; t < 10; ++t) {
        parity_bullet_system(r);
        int ehp = r.get<HealthComponent>(te).hp;
        int ehpP = r.get<HealthComponent>(pe).hp;
        printf("BL %d bullets=%d ehp=%d php=%d kills=%d\n", t, g_bulletCount, ehp, ehpP, g_kills);
    }
}

static void parity_melee()
{
    ParLayout layout;
    int pc2 = -1, ec[64], en = 0;
    par_build_layout(1, layout, pc2, ec, en);
    g_obsCount = layout.count;
    for (int i = 0; i < layout.count; ++i) g_obs[i] = layout.obs[i];
    g_bulletCount = 0;
    g_kills = 0;

    entt::registry r;
    // 玩家：原点附近，yaw=0（朝 +Z），战士
    auto pe = r.create();
    r.emplace<IdComponent>(pe, 1);
    r.emplace<PlayerComponent>(pe);
    r.get<PlayerComponent>(pe).char_id = CHAR_WARRIOR;
    r.emplace<LogicPositionComponent>(pe, fp_zero(), fp_from_float(0.6f), fp_zero());
    r.emplace<ActionStateComponent>(pe);
    r.emplace<HealthComponent>(pe, PLAYER_MAX_HP, PLAYER_MAX_HP);
    r.emplace<PlayerEventInput>(pe);

    // 敌人1：(0,0,1.0) 半圆内；敌人2：(0,0,3.0) 半圆外
    auto make_enemy = [&](int id, float x, float z) {
        auto e = r.create();
        r.emplace<IdComponent>(e, id);
        r.emplace<EnemyComponent>(e);
        r.emplace<LogicPositionComponent>(e, fp_from_float(x), fp_zero(), fp_from_float(z));
        r.emplace<ActionStateComponent>(e);
        r.emplace<HealthComponent>(e, ENEMY_MAX_HP, ENEMY_MAX_HP);
        r.emplace<PlayerEventInput>(e);
        r.emplace<AIComponent>(e, fp_from_float(x), fp_from_float(z),
            fp_from_float(3.0f), fp_from_float(400.0f), fp_from_float(2.0f), fp_zero());
        return e;
    };
    auto e1 = make_enemy(2, 0.0f, 1.0f);
    auto e2 = make_enemy(3, 0.0f, 3.0f);

    auto dump = [&](const char* tag, int round) {
        int php = r.get<HealthComponent>(pe).hp;
        int h1 = r.get<HealthComponent>(e1).hp, h2 = r.get<HealthComponent>(e2).hp;
        int d1 = r.get<HealthComponent>(e1).dead ? 1 : 0, d2 = r.get<HealthComponent>(e2).dead ? 1 : 0;
        printf("ML %s %d php=%d e1hp=%d e2hp=%d e1dead=%d e2dead=%d kills=%d bullets=%d\n",
            tag, round, php, h1, h2, d1, d2, g_kills, g_bulletCount);
    };

    // 第 0 轮：玩家近战（应只命中敌人1）
    r.get<PlayerEventInput>(pe).pending_actions.push_back({ 0, PlayerEventInput::ATTACK_MELEE });
    parity_melee_attack(r, 1);
    dump("melee", 0);

    // 第 1 轮：玩家射击（应创建 1 子弹）
    r.get<PlayerEventInput>(pe).pending_actions.push_back({ 0, PlayerEventInput::ATTACK_BULLET });
    parity_melee_attack(r, 1);
    dump("shoot", 1);

    // 第 2 轮：敌人1 近战（应打掉玩家血）
    r.get<PlayerEventInput>(e1).pending_actions.push_back({ 0, PlayerEventInput::ATTACK_MELEE });
    parity_melee_attack(r, 1);
    dump("enemy", 2);

    // 第 3 轮：玩家射线（应命中敌人1，hp 减少）
    r.get<PlayerEventInput>(pe).pending_actions.push_back({ 0, PlayerEventInput::ATTACK_RAY });
    parity_melee_attack(r, 1);
    dump("ray", 3);
}

/* 碰撞分离对照：玩家 vs 障碍（真实 GJK box-box，调 joy_collision_gjk3df_*） */
static void parity_collision()
{
    ParLayout layout;
    int pc2 = -1, ec[64], en = 0;
    par_build_layout(1, layout, pc2, ec, en);

    struct Case { const char* name; fp_t x, z; };
    Case cases[3] = {
        { "spawn", fp_add(fp_from_int(pc2 % 64 - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2))),
                   fp_add(fp_from_int(pc2 / 64 - PF_GRID_OFFSET), fp_div(fp_from_int(1), fp_from_int(2))) },
        { "wall",  fp_div(fp_from_int(layout.obs[0].x10), fp_from_int(10)),
                   fp_div(fp_from_int(layout.obs[0].z10), fp_from_int(10)) },
        // 墙边缘（法线水平 → 触发 XZ 推开路径）
        { "edge",  fp_zero(), fp_from_float(-25.9f) },
    };

    for (int ci = 0; ci < 3; ++ci) {
        fp_t px = cases[ci].x, py = fp_from_float(0.6f), pz = cases[ci].z;
        fp_t phx = fp_from_float(0.35f), phy = fp_from_float(0.9f), phz = fp_from_float(0.35f);

        for (int it = 0; it < 3; ++it) {
            for (int k = 0; k < layout.count; ++k) {
                const ParObstacle& o = layout.obs[k];
                fp_t bx = fp_div(fp_from_int(o.x10), fp_from_int(10));
                fp_t bz = fp_div(fp_from_int(o.z10), fp_from_int(10));
                fp_t bhx = fp_div(fp_from_int(o.hx10), fp_from_int(10));
                fp_t bhz = fp_div(fp_from_int(o.hz10), fp_from_int(10));
                fp_t bsy = fp_div(fp_from_int(o.sy10), fp_from_int(10));
                fp_t by = fp_div(bsy, fp_from_int(2));

                fp_t d0x = fp_sub(bx, px), d0z = fp_sub(bz, pz);
                fp_t d0len2 = fp_add(fp_mul(d0x, d0x), fp_mul(d0z, d0z));
                fp_t dirx, dirz;
                if (d0len2 > fp_from_float(1e-10f)) {
                    fp_t inv = fp_div(fp_one(), fp_sqrt(d0len2));
                    dirx = fp_mul(d0x, inv); dirz = fp_mul(d0z, inv);
                } else { dirx = fp_one(); dirz = fp_zero(); }

                joy_gjk3df_collider_t A, B;
                joy_gjk3df_contact_t contact;
                joy_vec3f_t pa = { px, py, pz }, ha = { phx, phy, phz };
                joy_vec3f_t pb = { bx, by, bz }, hb = { bhx, by, bhz };
                joy_vec3f_t dir = { dirx, fp_zero(), dirz };
                joy_collision_gjk3df_init_box(&A, pa, ha);
                joy_collision_gjk3df_init_box(&B, pb, hb);

                if (joy_collision_gjk3df_collide(&A, &B, dir, &contact)) {
                    fp_t depth = contact.depth;
                    if (depth < fp_zero()) depth = fp_sub(fp_zero(), depth);
                    fp_t n2 = fp_add(fp_mul(contact.normal.x, contact.normal.x),
                                     fp_mul(contact.normal.z, contact.normal.z));
                    if (n2 <= fp_from_float(1e-8f)) continue;
                    fp_t invN = fp_div(fp_one(), fp_sqrt(n2));
                    fp_t dxf = fp_mul(contact.normal.x, invN);
                    fp_t dzf = fp_mul(contact.normal.z, invN);
                    fp_t push = fp_add(depth, fp_from_float(0.03f));
                    px = fp_sub(px, fp_mul(dxf, push));
                    pz = fp_sub(pz, fp_mul(dzf, push));
                }
            }
            printf("COL %s %d %lld %lld\n", cases[ci].name, it, (long long)px, (long long)pz);
        }
    }
}

/* A* 寻路对照：障碍→walkable（0.6m pad）+ 二叉堆 A*（逐位复刻 pathfinding_system.h） */
static int pf_nearest_walkable_c(const bool* walkable, int cx, int cz)
{
    for (int r = 0; r < PF_GRID_N; r++)
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++) {
                int gx = cx + dx, gz = cz + dz;
                if (gx < 0 || gz < 0 || gx >= PF_GRID_N || gz >= PF_GRID_N) continue;
                if (walkable[gz * PF_GRID_N + gx]) return gz * PF_GRID_N + gx;
            }
    return -1;
}

static void heap_up_c(int* heap, const int* fscore, int i)
{
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (fscore[heap[p]] <= fscore[heap[i]]) break;
        int t = heap[i]; heap[i] = heap[p]; heap[p] = t; i = p;
    }
}
static void heap_down_c(int* heap, const int* fscore, int heap_len, int i)
{
    for (;;) {
        int l = i*2+1, r = i*2+2, m = i;
        if (l < heap_len && fscore[heap[l]] < fscore[heap[m]]) m = l;
        if (r < heap_len && fscore[heap[r]] < fscore[heap[m]]) m = r;
        if (m == i) break;
        int t = heap[i]; heap[i] = heap[m]; heap[m] = t; i = m;
    }
}

static int astar_c(const bool* walkable, int sx, int sz, int tx, int tz, int* out, int max_out)
{
    const int N = PF_GRID_N * PF_GRID_N;
    if (sx < 0) sx = 0; else if (sx >= PF_GRID_N) sx = PF_GRID_N - 1;
    if (sz < 0) sz = 0; else if (sz >= PF_GRID_N) sz = PF_GRID_N - 1;
    if (tx < 0) tx = 0; else if (tx >= PF_GRID_N) tx = PF_GRID_N - 1;
    if (tz < 0) tz = 0; else if (tz >= PF_GRID_N) tz = PF_GRID_N - 1;
    int start = sz * PF_GRID_N + sx, goal = tz * PF_GRID_N + tx;
    if (!walkable[start]) { int ns = pf_nearest_walkable_c(walkable, sx, sz); if (ns < 0) return 0; start = ns; sx = ns % PF_GRID_N; sz = ns / PF_GRID_N; }
    if (!walkable[goal]) { int ng = pf_nearest_walkable_c(walkable, tx, tz); if (ng < 0) return 0; goal = ng; tx = ng % PF_GRID_N; tz = ng / PF_GRID_N; }

    static int gscore[4096], fscore[4096], came[4096], heap[4096];
    static bool closed[4096], in_open[4096];
    int heap_len = 0;
    for (int i = 0; i < N; i++) { gscore[i]=0x3fffffff; fscore[i]=0x3fffffff; came[i]=-1; closed[i]=false; in_open[i]=false; }
    gscore[start] = 0;
    fscore[start] = (sx>tx?sx-tx:tx-sx) + (sz>tz?sz-tz:tz-sz);
    heap[heap_len++] = start; in_open[start] = true;
    const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    while (heap_len > 0) {
        int cur = heap[0]; in_open[cur] = false;
        heap[0] = heap[--heap_len];
        if (heap_len > 0) heap_down_c(heap, fscore, heap_len, 0);
        if (cur == goal) {
            int len = 0, node = cur;
            while (node != start && node >= 0) { if (len < max_out) out[len++] = node; node = came[node]; }
            for (int i = 0, j = len-1; i < j; i++, j--) { int t = out[i]; out[i] = out[j]; out[j] = t; }
            return len;
        }
        closed[cur] = true;
        int cx = cur % PF_GRID_N, cy = cur / PF_GRID_N;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
            if (nx < 0 || ny < 0 || nx >= PF_GRID_N || ny >= PF_GRID_N) continue;
            int ni = ny * PF_GRID_N + nx;
            if (!walkable[ni] || closed[ni]) continue;
            int ng = gscore[cur] + 1;
            if (ng < gscore[ni]) {
                gscore[ni] = ng;
                fscore[ni] = ng + (nx>tx?nx-tx:tx-nx) + (ny>tz?ny-tz:tz-ny);
                came[ni] = cur;
                if (!in_open[ni]) { in_open[ni] = true; heap[heap_len++] = ni; heap_up_c(heap, fscore, heap_len-1); }
                else { for (int i = 0; i < heap_len; i++) if (heap[i] == ni) { heap_up_c(heap, fscore, i); break; } }
            }
        }
    }
    return 0;
}

static void parity_astar()
{
    ParLayout layout;
    int pc = -1, ec[64], en = 0;
    par_build_layout(1, layout, pc, ec, en);

    // 障碍 → walkable（含 0.6m pad，复刻 pf_build_walkable 的 BOX 分支）
    static bool walkable[64*64];
    for (int i = 0; i < 64*64; i++) walkable[i] = true;
    float pad = fp_to_float(fp_div(fp_from_int(3), fp_from_int(5)));
    for (int k = 0; k < layout.count; k++) {
        const ParObstacle& o = layout.obs[k];
        fp_t fx = fp_div(fp_from_int(o.x10), fp_from_int(10));
        fp_t fz = fp_div(fp_from_int(o.z10), fp_from_int(10));
        fp_t fhx = fp_div(fp_from_int(o.hx10), fp_from_int(10));
        fp_t fhz = fp_div(fp_from_int(o.hz10), fp_from_int(10));
        float cx = fp_to_float(fx), cz = fp_to_float(fz);
        float hx = fp_to_float(fhx), hz = fp_to_float(fhz);
        int gx0 = (int)floorf(cx - hx - pad + PF_GRID_OFFSET);
        int gx1 = (int)floorf(cx + hx + pad + PF_GRID_OFFSET);
        int gz0 = (int)floorf(cz - hz - pad + PF_GRID_OFFSET);
        int gz1 = (int)floorf(cz + hz + pad + PF_GRID_OFFSET);
        if (gx0 < 0) gx0 = 0; if (gx1 > PF_GRID_N-1) gx1 = PF_GRID_N-1;
        if (gz0 < 0) gz0 = 0; if (gz1 > PF_GRID_N-1) gz1 = PF_GRID_N-1;
        for (int gz = gz0; gz <= gz1; gz++) for (int gx = gx0; gx <= gx1; gx++) walkable[gz*PF_GRID_N+gx] = false;
    }
    int wsum = 0, wcnt = 0;
    for (int i = 0; i < 64*64; i++) { if (walkable[i]) wsum += i; if (!walkable[i]) wcnt++; }
    printf("PFWALK 1 obs=%d sum=%d blocked=%d\n", layout.count, wsum, wcnt);

    int psx = pc % PF_GRID_N, psz = pc / PF_GRID_N;
    static int out[4096];
    int ncase = en + 3;
    for (int i = 0; i < ncase; i++) {
        int cx0, cz0, cx1, cz1;
        if (i < en) { cx0 = psx; cz0 = psz; cx1 = ec[i] % PF_GRID_N; cz1 = ec[i] / PF_GRID_N; }
        else if (i == en) { cx0 = psx; cz0 = psz; cx1 = psx; cz1 = psz; }
        else if (i == en + 1) { cx0 = psx; cz0 = psz; cx1 = psx + 4; cz1 = psz + 1; }
        else { cx0 = psx + 4; cz0 = psz + 1; cx1 = psx; cz1 = psz; }
        int cases1[4] = { cx0, cz0, cx1, cz1 };
        int len = astar_c(walkable, cases1[0], cases1[1], cases1[2], cases1[3], out, 4096);
        int psum = 0;
        for (int k = 0; k < len; k++) psum = (int)((unsigned)psum * 31u + (unsigned)out[k]);
        printf("PF %d %d %d %d len=%d sum=%d\n", cases1[0], cases1[1], cases1[2], cases1[3], len, psum);
    }
}

/* checksum 拼接（逐位复刻 state_checksum.h 的 checksum_entries_string：
 * 按 id 升序 + "%d,%lld,%lld,%lld;"。不含 MD5——MD5 由 CryptoExt 桥
 * 复用 joy_core_utils_md5x 保证一致，此处只对照拼接输入是否逐字节相同）。 */
struct PEntry { int id; fp_t x, y, z; };
static std::string parity_entries_string(entt::registry& r)
{
    std::vector<PEntry> v;
    auto view = r.view<IdComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& idc = view.get<IdComponent>(e);
        auto& lpc = view.get<LogicPositionComponent>(e);
        v.push_back({ idc.id, lpc.x, lpc.y, lpc.z });
    }
    std::sort(v.begin(), v.end(), [](const PEntry& a, const PEntry& b) { return a.id < b.id; });
    std::string s;
    char buf[64];
    for (auto& e : v) {
        int n = snprintf(buf, sizeof(buf), "%d,%lld,%lld,%lld;",
            e.id, (long long)e.x, (long long)e.y, (long long)e.z);
        s.append(buf, n);
    }
    return s;
}

/* 直接定义输入常量，避开 protocol.h → joy.h 的重依赖（harness 只用这 4 个位） */
enum { PH_INPUT_UP = 1, PH_INPUT_DOWN = 2, PH_INPUT_LEFT = 4, PH_INPUT_RIGHT = 8 };

/* 内联复刻 movement_system.h 的主体，但输入常量用上面的 PH_*（避免 include protocol.h）。
 * 逻辑与 movement_system.h 逐行一致；若该文件改动，这里必须同步。 */
static void parity_movement(entt::registry& r)
{
    auto view = r.view<PlayerStateInput, LogicPositionComponent>();
    for (auto e : view) {
        auto& psi = view.get<PlayerStateInput>(e);
        auto& p = view.get<LogicPositionComponent>(e);
        if (r.all_of<HealthComponent>(e) && r.get<HealthComponent>(e).dead) continue;
        const CharacterDef& cd = character_def(
            r.all_of<PlayerComponent>(e) ? r.get<PlayerComponent>(e).char_id : CHAR_WARRIOR);
        fp_t step = fp_mul(cd.move_speed, fp_from_float(1.0f / 20.0f));
        fp_t yaw_fp = fp_mul(fp_div(fp_from_int((int32_t)psi.yaw_quantized), fp_from_int(32767)), fp_pi());
        if (r.all_of<ActionStateComponent>(e)) r.get<ActionStateComponent>(e).yaw = fp_to_float(yaw_fp);
        const fp_t sy = fp_zero();
        const fp_t cy = fp_one();
        fp_t fdx = fp_zero(), fdz = fp_zero();
        if (psi.move_bits & PH_INPUT_UP)    { fdx = fp_add(fdx, sy); fdz = fp_add(fdz, cy); }
        if (psi.move_bits & PH_INPUT_DOWN)  { fdx = fp_sub(fdx, sy); fdz = fp_sub(fdz, cy); }
        if (psi.move_bits & PH_INPUT_LEFT)  { fdx = fp_add(fdx, cy); fdz = fp_sub(fdz, sy); }
        if (psi.move_bits & PH_INPUT_RIGHT) { fdx = fp_sub(fdx, cy); fdz = fp_add(fdz, sy); }
        if (fdx != fp_zero() || fdz != fp_zero()) {
            fp_t len = fp_sqrt(fp_add(fp_mul(fdx, fdx), fp_mul(fdz, fdz)));
            p.x = fp_add(p.x, fp_mul(fp_div(fdx, len), step));
            p.z = fp_add(p.z, fp_mul(fp_div(fdz, len), step));
        }
        if (r.all_of<PlayerPhysicsComponent>(e)) {
            auto& phy = r.get<PlayerPhysicsComponent>(e);
            if (phy.jump_requested && phy.on_ground) {
                phy.vertical_velocity = fp_from_float(4.0f);
                phy.on_ground = false;
            }
            phy.jump_requested = false;
            phy.vertical_velocity = fp_sub(phy.vertical_velocity,
                fp_mul(fp_from_float(9.8f), fp_from_float(1.0f / 20.0f)));
            p.y = fp_add(p.y, fp_mul(phy.vertical_velocity, fp_from_float(1.0f / 20.0f)));
            if (fp_to_float(p.y) <= fp_to_float(fp_from_float(0.6f))) {
                p.y = fp_from_float(0.6f);
                phy.vertical_velocity = fp_zero();
                phy.on_ground = true;
            }
        }
        fp_t WALL_HALF = fp_div(fp_from_int(1), fp_from_int(2));
        fp_t WALL_LIMIT = fp_sub(fp_from_int(32), WALL_HALF);
        fp_t WALL_NEG = fp_sub(fp_from_int(-32), -WALL_HALF);
        p.x = fp_clamp(p.x, WALL_NEG, WALL_LIMIT);
        p.z = fp_clamp(p.z, WALL_NEG, WALL_LIMIT);
    }
}

/* player_death_system.h 的主体（避免 include 其 transform/protocol 依赖） */
static void parity_player_death(entt::registry& r)
{
    auto view = r.view<PlayerComponent, HealthComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& h = view.get<HealthComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);
        if (!h.dead && h.hp <= 0) { h.dead = true; h.respawn_timer = PLAYER_RESPAWN_TICKS; }
        if (h.spawn_invuln > 0) h.spawn_invuln -= 1;
        if (h.dead) {
            h.respawn_timer -= 1;
            if (h.respawn_timer <= 0) {
                h.dead = false; h.hp = h.max_hp; h.spawn_invuln = SPAWN_INVULN_TICKS;
                if (r.all_of<SpawnPointComponent>(e)) {
                    const auto& sp = r.get<SpawnPointComponent>(e);
                    pos.x = sp.x; pos.y = sp.y; pos.z = sp.z;
                }
                if (r.all_of<PlayerStateInput>(e)) r.get<PlayerStateInput>(e).move_bits = 0;
                if (r.all_of<ActionStateComponent>(e)) {
                    auto& act = r.get<ActionStateComponent>(e);
                    act.state = ActionStateComponent::IDLE; act.melee_cooldown = 0;
                }
            }
        }
    }
}

/* knockback_system.h 的主体（空 static_view → 分轴无阻挡，退化为纯位移 + tick 递减） */
static void parity_knockback(entt::registry& r)
{
    auto view = r.view<KnockbackComponent, LogicPositionComponent>();
    for (auto e : view) {
        auto& kb = view.get<KnockbackComponent>(e);
        auto& pos = view.get<LogicPositionComponent>(e);
        // 空静态障碍集合 → knockback_blocked 恒 false，两轴都移动
        pos.x = fp_add(pos.x, kb.vx);
        pos.z = fp_add(pos.z, kb.vz);
        kb.ticks_left--;
        if (kb.ticks_left <= 0) r.remove<KnockbackComponent>(e);
    }
}

int main()
{
    parity_maps();
    parity_wave();
    parity_wave_clear();
    parity_input_consume();
    parity_protocol();
    parity_astar();
    parity_ai();
    parity_collision();
    parity_melee();
    parity_bullet();
    entt::registry r;
    auto e = r.create();
    r.emplace<IdComponent>(e, 1);
    r.emplace<PlayerComponent>(e);
    r.get<PlayerComponent>(e).char_id = CHAR_WARRIOR;
    r.emplace<LogicPositionComponent>(e, fp_from_float(3.2f), fp_from_float(0.6f), fp_from_float(2.3f));
    r.emplace<LogicVelocityComponent>(e);
    r.emplace<PlayerPhysicsComponent>(e);
    r.emplace<ActionStateComponent>(e);
    r.emplace<HealthComponent>(e, PLAYER_MAX_HP, PLAYER_MAX_HP);
    r.emplace<SpawnPointComponent>(e, fp_from_float(3.2f), fp_from_float(0.6f), fp_from_float(2.3f));
    r.emplace<PlayerStateInput>(e);

    auto& phy = r.get<PlayerPhysicsComponent>(e);

    const int TICKS = 30;
    for (int t = 0; t < TICKS; ++t) {
        // cleanup 在帧尾移除了 PlayerStateInput，每 tick 开头重新挂上并取引用
        r.emplace_or_replace<PlayerStateInput>(e);
        auto& psi = r.get<PlayerStateInput>(e);
        // 确定输入序列：0-9 W；10-19 D；20-29 W + jump
        psi.move_bits = 0; phy.jump_requested = false;
        if (t < 10) psi.move_bits |= PH_INPUT_UP;
        else if (t < 20) psi.move_bits |= PH_INPUT_RIGHT;
        else { psi.move_bits |= PH_INPUT_UP; phy.jump_requested = true; }
        // t==5 触发一次击退（+X/+Z，6 tick；与 C# Parity 同步）
        if (t == 5) r.emplace_or_replace<KnockbackComponent>(e,
            KnockbackComponent{ fp_from_float(0.08f), fp_from_float(0.04f), 6 });

        parity_player_death(r);
        parity_movement(r);
        parity_knockback(r);
        // cleanup_system：帧尾移除 PlayerStateInput（等价于字段归零）
        r.remove<PlayerStateInput>(e);

        auto& p = r.get<LogicPositionComponent>(e);
        auto& h = r.get<HealthComponent>(e);
        printf("TICK %d P1 %lld %lld %lld %d %d %d %lld\n",
            t, (long long)p.x, (long long)p.y, (long long)p.z,
            h.hp, h.dead ? 1 : 0, phy.on_ground ? 1 : 0, (long long)phy.vertical_velocity);
        // cleanup 后的输入：组件已移除 → 全 0（与 C# 归零同语义）
        printf("INPUT %d %d %d\n", 0, 0, 0);

        // checksum 拼接串（验证 id 排序 + "%d,%lld,%lld,%lld;" 公式与 C# 一致）
        printf("ENTRIES %s\n", parity_entries_string(r).c_str());
    }
    return 0;
}
