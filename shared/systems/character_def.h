#ifndef CHARACTER_DEF_H
#define CHARACTER_DEF_H
#include <joy/jmath.h>

// 角色定义表（帧同步确定性：参与逻辑的数值均为整数或纯定点有理数，不用浮点转换）
enum CharacterId {
    CHAR_WARRIOR  = 0,
    CHAR_GUNNER   = 1,
    CHAR_ASSASSIN = 2,
    CHAR_COUNT    = 3,
};

struct CharacterDef {
    int         char_id;
    const char* name;
    int         max_hp;
    int         melee_damage;
    fp_t        melee_range;   // 近战半圆盘半径（米，定点）
    int         bullet_damage;
    int         ray_damage;    // 射线（hitscan）单发伤害
    fp_t        ray_range;     // 射线最远射程（米，定点）
    fp_t        move_speed;    // 米/秒（定点）
    // 视觉（仅渲染用，不参与帧同步）
    uint8_t fill_r, fill_g, fill_b;
    uint8_t stroke_r, stroke_g, stroke_b;
};

static const CharacterDef CHARACTER_DEFS[CHAR_COUNT] = {
    // 战士：高血高近战，移速偏慢
    { CHAR_WARRIOR,  "战士", 150, 35, fp_div(fp_from_int(9),  fp_from_int(5)),  8, 25, fp_div(fp_from_int(8),  fp_from_int(1)),  fp_div(fp_from_int(9),  fp_from_int(2)),  190, 60, 50,  30, 30, 35 },
    // 射手：低血高远程，移速中等
    { CHAR_GUNNER,   "射手",  80, 20, fp_div(fp_from_int(3),  fp_from_int(2)), 15, 30, fp_div(fp_from_int(12), fp_from_int(1)), fp_div(fp_from_int(11), fp_from_int(2)),  60, 120, 80, 30, 30, 35 },
    // 刺客：低血高移速，近战爆发
    { CHAR_ASSASSIN, "刺客",  70, 30, fp_div(fp_from_int(8),  fp_from_int(5)), 10, 40, fp_div(fp_from_int(6),  fp_from_int(1)),  fp_div(fp_from_int(13), fp_from_int(2)), 110, 70, 130, 30, 30, 35 },
};

static inline const CharacterDef& character_def(int char_id)
{
    if (char_id < 0 || char_id >= CHAR_COUNT) char_id = CHAR_WARRIOR;
    return CHARACTER_DEFS[char_id];
}

#endif
