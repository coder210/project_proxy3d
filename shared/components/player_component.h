#ifndef PLAYER_COMPONENT_H
#define PLAYER_COMPONENT_H

struct PlayerComponent
{
        int conv;
        int char_id = 0;   // 角色类型（CharacterDef 索引）
        char nickname[32] = "";   // 玩家昵称（头顶显示，帧同步两端一致）
};

#endif