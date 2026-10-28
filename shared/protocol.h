#pragma once
/* 冒险游戏网络协议（基于 jmsgpack）
 * Server ↔ Client 帧同步协议定义
 * 枚举、结构体、打包/解包工具函数 */
#include <joy/joy.h>
#include <cstring>
#include <cstdlib>

/* ================================ 常量 ================================ */
/* PIXELS_PER_METER 现由 joy/joy.h 统一提供，不再在此定义。 */

/* 输入位掩码 */
enum {
    INPUT_UP     = 1 << 0,
    INPUT_DOWN   = 1 << 1,
    INPUT_LEFT   = 1 << 2,
    INPUT_RIGHT  = 1 << 3,
    INPUT_ATTACK = 1 << 4,   // J：近战
    INPUT_JUMP   = 1 << 5,
    INPUT_BULLET = 1 << 6,   // K：射击（点按单发，由 player 输入离散化为动作类型 2）
    INPUT_RAY    = 1 << 7,   // L：发射射线（hitscan 瞬时命中，点按单发 → 动作类型 3）
    INPUT_DIR_MASK = INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT,
};

/* C2S 命令 */
enum C2SCmd {
    CMD_UNKNOWN       = 0,
    CMD_PLAYER_JOIN   = 1,
    CMD_PLAYER_LEAVE  = 2,
    CMD_PLAYER_INPUT  = 3,
    CMD_PLAYER_HEART  = 4,
    CMD_LOADING       = 5,
};

/* S2C 命令 */
enum S2CCmd {
    S2C_CMD_NONE        = 0,
    S2C_CMD_LOADING     = 1,
    S2C_CMD_COMMAND     = 2,
    S2C_CMD_MAP_RELOAD  = 3,  // 波次换图：服务端下发新地图障碍布局 + 敌人，客户端据此重建
};

/* S2C 实体类型 */
enum S2CType {
    S2C_TYPE_NORMAL = 0,
    S2C_TYPE_PLAYER = 1,
    S2C_TYPE_ENEMY  = 2,
    S2C_TYPE_PROP   = 3,  // 静态障碍物（墙/箱子）
};

/* ============================ 协议结构体 ============================ */
struct S2CPlayerJoin  { int32_t conv; int64_t position_x, position_y, position_z; int32_t char_id; char nickname[32]; };
struct S2CPlayerLeave { int32_t conv; };
struct S2CPlayerInput { uint32_t client_seq; int32_t conv, dir; int8_t attack; int16_t yaw_quantized; };
struct S2CEntity {
    int32_t id, type;
    int64_t position_x, position_y, position_z, width, height, hp;
    int32_t player_conv;
    int16_t yaw_quantized; // 水平朝向（-180°~180° 映射到 int16）
    int64_t ai_type;       // AI 类型（SpawnDef 索引）
    int32_t char_id;       // 玩家角色类型（CharacterDef 索引）
    char nickname[32];     // 玩家昵称（仅玩家实体有效）
    // AI 运行时状态（仅敌人有意义，客户端接入时恢复，避免持续累积的 AI 状态起点不同步）
    int32_t ai_timer;          // 巡逻唤醒倒计时
    int32_t ai_state;          // 0=巡逻, 1=追击, 2=攻击, 3=返回出生点
    int64_t ai_target_x;       // 当前移动目标（XZ 平面）
    int64_t ai_target_z;
    // PathComponent 运行时状态（仅敌人有意义，防止客户端恢复时新建 PathComponent 的 target_gx=0
    // 导致 set_target 误判目标格变化 → dirty=true → 不必要的路径重算 → 双端移动行为分叉）
    int32_t pf_target_gx;      // 寻路目标格子 X
    int32_t pf_target_gz;      // 寻路目标格子 Z
    int32_t pf_path_len;       // 当前路径长度（0=路径耗尽/未计算）
    int32_t pf_next_idx;       // 当前路径走到的下标
    int32_t pf_recompute_cd;   // 重算冷却剩余帧
};

/* ======================== 辅助函数 ======================== */

/* 从 keycode 提取方向索引 (0=下, 1=左, 2=右, 3=上) */
static inline int input_to_dir(int keycode) {
    if (keycode & INPUT_DOWN)  return 0;
    if (keycode & INPUT_LEFT)  return 1;
    if (keycode & INPUT_RIGHT) return 2;
    if (keycode & INPUT_UP)    return 3;
    return 0;
}
static inline bool has_dir(int keycode) { return (keycode & INPUT_DIR_MASK) != 0; }

/* 从 jmsgpack 对象中提取有符号整数 */
static inline int64_t jmp_int(const jmsgpack_object_t* o) {
    if (jmsgpack_object_type(o) == JMSGPACK_OBJECT_POSITIVE_INTEGER) return (int64_t)jmsgpack_object_u64(o);
    if (jmsgpack_object_type(o) == JMSGPACK_OBJECT_NEGATIVE_INTEGER) return jmsgpack_object_i64(o);
    return 0;
}

/* ======================== msgpack 打包 ======================== */

/* 回写回调：写入 std::string */
static int mp_write_cb(void* data, const char* buf, size_t len) {
    ((std::string*)data)->append(buf, len);
    return 0;
}

/* --- 子对象打包 --- */
static inline void pack_entity(jmsgpack_packer_t* pk, const S2CEntity& e) {
    jmsgpack_pack_array(pk, 22);
    jmsgpack_pack_int(pk, e.id);    jmsgpack_pack_int(pk, e.type);
    jmsgpack_pack_int64(pk, e.position_x); jmsgpack_pack_int64(pk, e.position_y); jmsgpack_pack_int64(pk, e.position_z);
    jmsgpack_pack_int64(pk, e.width);      jmsgpack_pack_int64(pk, e.height);
    jmsgpack_pack_int64(pk, e.hp);  jmsgpack_pack_int(pk, e.player_conv);
    jmsgpack_pack_int(pk, e.yaw_quantized);
    jmsgpack_pack_int64(pk, e.ai_type);
    jmsgpack_pack_int(pk, e.char_id);
    jmsgpack_pack_str(pk, strlen(e.nickname));
    jmsgpack_pack_str_body(pk, e.nickname, strlen(e.nickname));
    jmsgpack_pack_int(pk, e.ai_timer);  jmsgpack_pack_int(pk, e.ai_state);
    jmsgpack_pack_int64(pk, e.ai_target_x); jmsgpack_pack_int64(pk, e.ai_target_z);
    jmsgpack_pack_int(pk, e.pf_target_gx); jmsgpack_pack_int(pk, e.pf_target_gz);
    jmsgpack_pack_int(pk, e.pf_path_len);  jmsgpack_pack_int(pk, e.pf_next_idx);
    jmsgpack_pack_int(pk, e.pf_recompute_cd);
}
static inline void pack_join(jmsgpack_packer_t* pk, const S2CPlayerJoin& j) {
    jmsgpack_pack_array(pk, 6);
    jmsgpack_pack_int(pk, j.conv);
    jmsgpack_pack_int64(pk, j.position_x);
    jmsgpack_pack_int64(pk, j.position_y);
    jmsgpack_pack_int64(pk, j.position_z);
    jmsgpack_pack_int(pk, j.char_id);
    jmsgpack_pack_str(pk, strlen(j.nickname));
    jmsgpack_pack_str_body(pk, j.nickname, strlen(j.nickname));
}
/* 构造 join（含昵称，自动截断到 31 字节） */
static inline S2CPlayerJoin make_join(int32_t conv, int64_t x, int64_t y, int64_t z, int32_t char_id, const char* nickname) {
    S2CPlayerJoin j;
    j.conv = conv;
    j.position_x = x; j.position_y = y; j.position_z = z;
    j.char_id = char_id;
    j.nickname[0] = '\0';
    if (nickname) {
        snprintf(j.nickname, sizeof(j.nickname), "%s", nickname);
    }
    return j;
}
static inline void pack_leave(jmsgpack_packer_t* pk, const S2CPlayerLeave& l) {
    jmsgpack_pack_array(pk, 1);
    jmsgpack_pack_int(pk, l.conv);
}
static inline void pack_input(jmsgpack_packer_t* pk, const S2CPlayerInput& i) {
    jmsgpack_pack_array(pk, 5);
    jmsgpack_pack_int(pk, i.conv);
    jmsgpack_pack_int(pk, i.dir);
    jmsgpack_pack_int(pk, i.attack);
    jmsgpack_pack_int(pk, i.yaw_quantized);
    jmsgpack_pack_int(pk, i.client_seq);
}

/* C2S 打包: [cmd, payload] */
static inline std::string pack_c2s(int cmd, int64_t join_x, int64_t join_y, int64_t join_z,
    int input_dir, int input_attack, int16_t input_yaw, int32_t char_id = 0, const char* nickname = "")
{
    std::string out;
    jmsgpack_packer_t pk;
    jmsgpack_packer_init(&pk, &out, mp_write_cb);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, cmd);
    if (cmd == CMD_PLAYER_JOIN) {
        jmsgpack_pack_array(&pk, 5);
        jmsgpack_pack_int64(&pk, join_x);
        jmsgpack_pack_int64(&pk, join_y);
        jmsgpack_pack_int64(&pk, join_z);
        jmsgpack_pack_int(&pk, char_id);
        jmsgpack_pack_str(&pk, strlen(nickname));
        jmsgpack_pack_str_body(&pk, nickname, strlen(nickname));
    } else if (cmd == CMD_PLAYER_INPUT) {
        jmsgpack_pack_array(&pk, 3);
        jmsgpack_pack_int(&pk, input_dir);
        jmsgpack_pack_int(&pk, input_attack);
        jmsgpack_pack_int(&pk, input_yaw);
    } else {
        jmsgpack_pack_nil(&pk);
    }
    return out;
}

/* S2C 世界快照打包: [world_eid, [entity...]] */
static inline std::string pack_world(int entity_id, const S2CEntity* entities, int count) {
    std::string out;
    jmsgpack_packer_t pk;
    jmsgpack_packer_init(&pk, &out, mp_write_cb);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, entity_id);
    jmsgpack_pack_array(&pk, count);
    for (int i = 0; i < count; i++)
        pack_entity(&pk, entities[i]);
    return out;
}

/* S2C loading 打包: [S2C_CMD_LOADING, [conv, frame_id, [world_eid, [entity...]], seed, map_seed, wave, wave_delay_ticks, level]] */
static inline std::string pack_s2c_loading(int32_t conv, int32_t frame_id, int32_t world_eid,
    const S2CEntity* entities, int ec, int32_t seed, int32_t map_seed,
    int32_t wave, int32_t wave_delay_ticks, int32_t level)
{
    std::string out;
    jmsgpack_packer_t pk;
    jmsgpack_packer_init(&pk, &out, mp_write_cb);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, S2C_CMD_LOADING);
    jmsgpack_pack_array(&pk, 8);
    jmsgpack_pack_int(&pk, conv);
    jmsgpack_pack_int(&pk, frame_id);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, world_eid);
    jmsgpack_pack_array(&pk, ec);
    for (int i = 0; i < ec; i++)
        pack_entity(&pk, entities[i]);
    jmsgpack_pack_int(&pk, seed);
    // 地图 seed：双端换波时用确定性地图布局采样敌人出生点（帧同步一致）
    jmsgpack_pack_int(&pk, map_seed);
    // 当前波次 + 换波待生成剩余延迟（-1=无待生成）。空窗期加入的客户端据此创建
    // WaveAdvanceComponent，与服务端在相同逻辑帧生成敌人，避免漏掉整波敌人。
    jmsgpack_pack_int(&pk, wave);
    jmsgpack_pack_int(&pk, wave_delay_ticks);
    // 当前固定地图关卡（房主/权威端选定，随加入下发给客户端，保证双端用同一关卡生成）
    jmsgpack_pack_int(&pk, level);
    return out;
}

/* S2C map reload 打包（换关信号）: [S2C_CMD_MAP_RELOAD, [frame_id, level]]
 * 只同步换关的触发帧号与目标关卡号，不再下发敌人/障碍实体数组——
 * 双端基于确定性 level_build_layout(level) 各自重建障碍与敌人，保证帧同步一致。
 * frame_id = 权威端决定换关的那个逻辑帧号；客户端须等到本地逻辑帧推进到该帧号才应用。 */
static inline std::string pack_s2c_map_reload(int32_t frame_id, int32_t level)
{
    std::string out;
    jmsgpack_packer_t pk;
    jmsgpack_packer_init(&pk, &out, mp_write_cb);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, S2C_CMD_MAP_RELOAD);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, frame_id);
    jmsgpack_pack_int(&pk, level);
    return out;
}

/* S2C command 打包（服务端用）: [S2C_CMD_COMMAND, [frame_id, joins, leaves, inputs, checksum]] */
static inline std::string pack_s2c(int16_t cmd, int32_t frame_id,
    const S2CPlayerJoin* joins, int jn,
    const S2CPlayerLeave* leaves, int ln,
    const S2CPlayerInput* inputs, int in,
    const char* checksum)
{
    std::string out;
    jmsgpack_packer_t pk;
    jmsgpack_packer_init(&pk, &out, mp_write_cb);
    jmsgpack_pack_array(&pk, 2);
    jmsgpack_pack_int(&pk, cmd);
    if (cmd == S2C_CMD_COMMAND) {
        jmsgpack_pack_array(&pk, 5);
        jmsgpack_pack_int(&pk, frame_id);
        jmsgpack_pack_array(&pk, jn);
        for (int i = 0; i < jn; i++) pack_join(&pk, joins[i]);
        jmsgpack_pack_array(&pk, ln);
        for (int i = 0; i < ln; i++) pack_leave(&pk, leaves[i]);
        jmsgpack_pack_array(&pk, in);
        for (int i = 0; i < in; i++) pack_input(&pk, inputs[i]);
        jmsgpack_pack_str(&pk, checksum ? strlen(checksum) : 0);
        if (checksum) jmsgpack_pack_str_body(&pk, checksum, strlen(checksum));
    } else {
        jmsgpack_pack_nil(&pk);
    }
    return out;
}

/* ======================== msgpack 解包 ======================== */

/* 解包单个实体 */
static inline void unpack_entity(const jmsgpack_object_t* o, S2CEntity* e) {
    if (!o || jmsgpack_object_type(o) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(o) < 12) return;
    e->id         = (int)jmp_int(jmsgpack_object_array_at(o, 0));
    e->type       = (int)jmp_int(jmsgpack_object_array_at(o, 1));
    e->position_x = jmp_int(jmsgpack_object_array_at(o, 2));
    e->position_y = jmp_int(jmsgpack_object_array_at(o, 3));
    e->position_z = jmp_int(jmsgpack_object_array_at(o, 4));
    e->width      = jmp_int(jmsgpack_object_array_at(o, 5));
    e->height     = jmp_int(jmsgpack_object_array_at(o, 6));
    e->hp         = jmp_int(jmsgpack_object_array_at(o, 7));
    e->player_conv = (int)jmp_int(jmsgpack_object_array_at(o, 8));
    e->yaw_quantized = (int16_t)jmp_int(jmsgpack_object_array_at(o, 9));
    e->ai_type    = jmp_int(jmsgpack_object_array_at(o, 10));
    e->char_id    = (int)jmp_int(jmsgpack_object_array_at(o, 11));
    e->nickname[0] = '\0';
    if (jmsgpack_object_array_size(o) >= 13 && jmsgpack_object_type(jmsgpack_object_array_at(o, 12)) == JMSGPACK_OBJECT_STR) {
        size_t nl = jmsgpack_object_str_size(jmsgpack_object_array_at(o, 12));
        if (nl > sizeof(e->nickname) - 1) nl = sizeof(e->nickname) - 1;
        memcpy(e->nickname, jmsgpack_object_str_ptr(jmsgpack_object_array_at(o, 12)), nl);
        e->nickname[nl] = '\0';
    }
    // AI 运行时状态（旧版快照缺这些字段时保持 0，不影响旧客户端）
    e->ai_timer = 0; e->ai_state = 0; e->ai_target_x = 0; e->ai_target_z = 0;
    if (jmsgpack_object_array_size(o) >= 17) {
        e->ai_timer = (int)jmp_int(jmsgpack_object_array_at(o, 13));
        e->ai_state = (int)jmp_int(jmsgpack_object_array_at(o, 14));
        e->ai_target_x = jmp_int(jmsgpack_object_array_at(o, 15));
        e->ai_target_z = jmp_int(jmsgpack_object_array_at(o, 16));
    }
    // PathComponent 状态（旧版快照缺这些字段时保持 0）
    e->pf_target_gx = 0; e->pf_target_gz = 0;
    e->pf_path_len = 0; e->pf_next_idx = 0; e->pf_recompute_cd = 0;
    if (jmsgpack_object_array_size(o) >= 22) {
        e->pf_target_gx = (int)jmp_int(jmsgpack_object_array_at(o, 17));
        e->pf_target_gz = (int)jmp_int(jmsgpack_object_array_at(o, 18));
        e->pf_path_len  = (int)jmp_int(jmsgpack_object_array_at(o, 19));
        e->pf_next_idx  = (int)jmp_int(jmsgpack_object_array_at(o, 20));
        e->pf_recompute_cd = (int)jmp_int(jmsgpack_object_array_at(o, 21));
    }
}

/* 解包 S2C 世界快照: [eid, [entity...]]，返回实体数 */
static inline int unpack_world(const char* data, int len, int* eid, S2CEntity* entities, int max_ec) {
    jmsgpack_unpacked_t r;
    jmsgpack_unpacked_init(&r);
    size_t off = 0;
    if (jmsgpack_unpack_next(&r, data, len, &off) < 0) { jmsgpack_unpacked_destroy(&r); return 0; }
    const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
    if (jmsgpack_object_type(root) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(root) < 2) { jmsgpack_unpacked_destroy(&r); return 0; }
    *eid = (int)jmp_int(jmsgpack_object_array_at(root, 0));
    auto* arr = jmsgpack_object_array_at(root, 1);
    int n = (int)jmsgpack_object_array_size(arr);
    if (n > max_ec) n = max_ec;
    for (int i = 0; i < n; i++)
        unpack_entity(jmsgpack_object_array_at(arr, i), &entities[i]);
    jmsgpack_unpacked_destroy(&r);
    return n;
}

/* 解包 S2C loading 消息: 返回实体数，通过出参返回 conv/frame_id/eid/seed/map_seed/wave/wave_delay_ticks */
static inline int unpack_loading(const char* data, int len,
    int* conv, int* frame_id, int* eid, int* seed, int* map_seed,
    int* wave, int* wave_delay_ticks, int* level,
    S2CEntity* entities, int max_ec)
{
    jmsgpack_unpacked_t r;
    jmsgpack_unpacked_init(&r);
    size_t off = 0;
    if (jmsgpack_unpack_next(&r, data, len, &off) < 0) { jmsgpack_unpacked_destroy(&r); return -1; }
    const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
    auto* pl = jmsgpack_object_array_at(root, 1);
    if (jmsgpack_object_type(pl) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(pl) < 5) { jmsgpack_unpacked_destroy(&r); return -1; }
    if (conv)     *conv     = (int)jmp_int(jmsgpack_object_array_at(pl, 0));
    if (frame_id) *frame_id = (int)jmp_int(jmsgpack_object_array_at(pl, 1));
    auto* w = jmsgpack_object_array_at(pl, 2);
    if (jmsgpack_object_type(w) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(w) < 2) { jmsgpack_unpacked_destroy(&r); return -1; }
    if (eid) *eid = (int)jmp_int(jmsgpack_object_array_at(w, 0));
    if (seed) *seed = (int)jmp_int(jmsgpack_object_array_at(pl, 3));
    if (map_seed) *map_seed = (int)jmp_int(jmsgpack_object_array_at(pl, 4));
    if (wave) *wave = (jmsgpack_object_array_size(pl) >= 7) ? (int)jmp_int(jmsgpack_object_array_at(pl, 5)) : 0;
    if (wave_delay_ticks) *wave_delay_ticks = (jmsgpack_object_array_size(pl) >= 7) ? (int)jmp_int(jmsgpack_object_array_at(pl, 6)) : -1;
    if (level) *level = (jmsgpack_object_array_size(pl) >= 8) ? (int)jmp_int(jmsgpack_object_array_at(pl, 7)) : 1;
    auto* arr = jmsgpack_object_array_at(w, 1);
    int n = (int)jmsgpack_object_array_size(arr);
    if (n > max_ec) n = max_ec;
    for (int i = 0; i < n; i++)
        unpack_entity(jmsgpack_object_array_at(arr, i), &entities[i]);
    jmsgpack_unpacked_destroy(&r);
    return n;
}

/* 解包 S2C map reload 消息（换关信号）: 返回 1 成功，出参 frame_id / level */
static inline int unpack_map_reload(const char* data, int len, int* frame_id, int* level)
{
    jmsgpack_unpacked_t r;
    jmsgpack_unpacked_init(&r);
    size_t off = 0;
    if (jmsgpack_unpack_next(&r, data, len, &off) < 0) { jmsgpack_unpacked_destroy(&r); return 0; }
    const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
    if (jmsgpack_object_type(root) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(root) < 2) { jmsgpack_unpacked_destroy(&r); return 0; }
    auto* pl = jmsgpack_object_array_at(root, 1);
    if (jmsgpack_object_type(pl) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(pl) < 2) { jmsgpack_unpacked_destroy(&r); return 0; }
    if (frame_id) *frame_id = (int)jmp_int(jmsgpack_object_array_at(pl, 0));
    if (level) *level = (int)jmp_int(jmsgpack_object_array_at(pl, 1));
    jmsgpack_unpacked_destroy(&r);
    return 1;
}

/* 解包 S2C command 消息（通用版，服务端/客户端均可用）
 * 返回 1 成功，0 失败。
 * 通过出参返回 frame_id / joins / leaves / inputs / checksum */
static inline int unpack_command(const char* data, int len,
    int* frame_id,
    S2CPlayerJoin* joins, int max_jn, int* jn,
    S2CPlayerLeave* leaves, int max_ln, int* ln,
    S2CPlayerInput* inputs, int max_in, int* in,
    char* checksum_out = nullptr, int checksum_cap = 0)
{
    if (jn)  memset(joins,  0, max_jn * sizeof(S2CPlayerJoin));
    if (ln)  memset(leaves, 0, max_ln * sizeof(S2CPlayerLeave));
    if (in)  memset(inputs, 0, max_in * sizeof(S2CPlayerInput));
    if (jn)  *jn = 0;
    if (ln)  *ln = 0;
    if (in)  *in = 0;

    jmsgpack_unpacked_t r;
    jmsgpack_unpacked_init(&r);
    size_t off = 0;
    if (jmsgpack_unpack_next(&r, data, len, &off) < 0) { jmsgpack_unpacked_destroy(&r); return 0; }
    const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
    if (jmsgpack_object_type(root) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(root) < 2) { jmsgpack_unpacked_destroy(&r); return 0; }
    auto* pl = jmsgpack_object_array_at(root, 1);
    if (jmsgpack_object_type(pl) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(pl) < 5) { jmsgpack_unpacked_destroy(&r); return 0; }

    if (frame_id) *frame_id = (int)jmp_int(jmsgpack_object_array_at(pl, 0));

    auto read_arr = [&](int idx, auto* out, int max, int& cnt) {
        if (idx >= (int)jmsgpack_object_array_size(pl)) return;
        auto* a = jmsgpack_object_array_at(pl, idx);
        if (jmsgpack_object_type(a) != JMSGPACK_OBJECT_ARRAY) return;
        int n = (int)jmsgpack_object_array_size(a);
        if (n > max) n = max;
        cnt = n;
        for (int i = 0; i < n; i++) {
            auto* o = jmsgpack_object_array_at(a, i);
            auto& d = out[i];
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, S2CPlayerJoin>) {
                if (jmsgpack_object_type(o) == JMSGPACK_OBJECT_ARRAY && jmsgpack_object_array_size(o) >= 5) {
                    d.conv = (int)jmp_int(jmsgpack_object_array_at(o, 0));
                    d.position_x = jmp_int(jmsgpack_object_array_at(o, 1));
                    d.position_y = jmp_int(jmsgpack_object_array_at(o, 2));
                    d.position_z = jmp_int(jmsgpack_object_array_at(o, 3));
                    d.char_id = (int)jmp_int(jmsgpack_object_array_at(o, 4));
                    d.nickname[0] = '\0';
                    if (jmsgpack_object_array_size(o) >= 6 && jmsgpack_object_type(jmsgpack_object_array_at(o, 5)) == JMSGPACK_OBJECT_STR) {
                        size_t nl = jmsgpack_object_str_size(jmsgpack_object_array_at(o, 5));
                        if (nl > sizeof(d.nickname) - 1) nl = sizeof(d.nickname) - 1;
                        memcpy(d.nickname, jmsgpack_object_str_ptr(jmsgpack_object_array_at(o, 5)), nl);
                        d.nickname[nl] = '\0';
                    }
                }
            } else if constexpr (std::is_same_v<T, S2CPlayerLeave>) {
                if (jmsgpack_object_type(o) == JMSGPACK_OBJECT_ARRAY && jmsgpack_object_array_size(o) >= 1) {
                    d.conv = (int)jmp_int(jmsgpack_object_array_at(o, 0));
                }
            } else if constexpr (std::is_same_v<T, S2CPlayerInput>) {
                if (jmsgpack_object_type(o) == JMSGPACK_OBJECT_ARRAY && jmsgpack_object_array_size(o) >= 5) {
                    d.conv = (int)jmp_int(jmsgpack_object_array_at(o, 0));
                    d.dir = (int)jmp_int(jmsgpack_object_array_at(o, 1));
                    d.attack = (int8_t)jmp_int(jmsgpack_object_array_at(o, 2));
                    d.yaw_quantized = (int16_t)jmp_int(jmsgpack_object_array_at(o, 3));
                    d.client_seq = (uint32_t)jmp_int(jmsgpack_object_array_at(o, 4));
                }
            }
        }
    };

    read_arr(1, joins,  max_jn, *jn);
    read_arr(2, leaves, max_ln, *ln);
    read_arr(3, inputs, max_in, *in);

    // checksum 字符串（索引 4，服务端权威值，客户端用于帧同步校验）
    if (checksum_out && checksum_cap > 0) {
        checksum_out[0] = '\0';
        if (jmsgpack_object_array_size(pl) >= 5) {
            auto* cs = jmsgpack_object_array_at(pl, 4);
            if (jmsgpack_object_type(cs) == JMSGPACK_OBJECT_STR) {
                size_t cl = jmsgpack_object_str_size(cs);
                if (cl > (size_t)(checksum_cap - 1)) cl = (size_t)(checksum_cap - 1);
                memcpy(checksum_out, jmsgpack_object_str_ptr(cs), cl);
                checksum_out[cl] = '\0';
            }
        }
    }

    jmsgpack_unpacked_destroy(&r);
    return 1;
}

/* 解包 C2S 消息: [cmd, payload]
 * 返回 1 成功，0 失败。
 * 通过出参返回 cmd / join_x, join_y, join_z, join_char_id, join_nickname / input_dir, input_attack, input_yaw */
static inline int unpack_c2s(const char* data, int len,
    int* cmd, int64_t* join_x, int64_t* join_y, int64_t* join_z,
    int* input_dir, int* input_attack, int16_t* input_yaw, int32_t* join_char_id = nullptr,
    char* join_nickname = nullptr, int nickname_cap = 0)
{
    jmsgpack_unpacked_t r;
    jmsgpack_unpacked_init(&r);
    size_t off = 0;
    if (jmsgpack_unpack_next(&r, data, len, &off) < 0) { jmsgpack_unpacked_destroy(&r); return 0; }
    const jmsgpack_object_t* root = jmsgpack_unpacked_data(&r);
    if (jmsgpack_object_type(root) != JMSGPACK_OBJECT_ARRAY || jmsgpack_object_array_size(root) < 2) { jmsgpack_unpacked_destroy(&r); return 0; }
    *cmd = (int)jmp_int(jmsgpack_object_array_at(root, 0));
    auto* payload = jmsgpack_object_array_at(root, 1);
    if (jmsgpack_object_type(payload) == JMSGPACK_OBJECT_ARRAY) {
        switch (*cmd) {
        case CMD_PLAYER_JOIN:
            if (jmsgpack_object_array_size(payload) >= 3) {
                *join_x = jmp_int(jmsgpack_object_array_at(payload, 0));
                *join_y = jmp_int(jmsgpack_object_array_at(payload, 1));
                *join_z = jmp_int(jmsgpack_object_array_at(payload, 2));
            }
            if (join_char_id && jmsgpack_object_array_size(payload) >= 4) {
                *join_char_id = (int32_t)jmp_int(jmsgpack_object_array_at(payload, 3));
            }
            if (join_nickname && nickname_cap > 0 && jmsgpack_object_array_size(payload) >= 5 &&
                jmsgpack_object_type(jmsgpack_object_array_at(payload, 4)) == JMSGPACK_OBJECT_STR) {
                size_t nl = jmsgpack_object_str_size(jmsgpack_object_array_at(payload, 4));
                if (nl > (size_t)(nickname_cap - 1)) nl = (size_t)(nickname_cap - 1);
                memcpy(join_nickname, jmsgpack_object_str_ptr(jmsgpack_object_array_at(payload, 4)), nl);
                join_nickname[nl] = '\0';
            }
            break;
        case CMD_PLAYER_INPUT:
            if (jmsgpack_object_array_size(payload) >= 3) {
                *input_dir = (int)jmp_int(jmsgpack_object_array_at(payload, 0));
                *input_attack = (int)jmp_int(jmsgpack_object_array_at(payload, 1));
                *input_yaw = (int16_t)jmp_int(jmsgpack_object_array_at(payload, 2));
            }
            break;
        }
    }
    jmsgpack_unpacked_destroy(&r);
    return 1;
}


