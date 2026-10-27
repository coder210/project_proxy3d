#ifndef FOG_PASS_H
#define FOG_PASS_H

struct Context;

// 迷雾视野（全向精准）：以本地玩家为圆心 360° 视野，墙遮挡处雾在墙前截止、
// 不穿透墙体；可见区不铺雾，可见区外到雾外径铺半透明墨雾。
// 纯客户端表现，不影响寻路/碰撞/帧同步。
void fog_pass(Context* ctx);

// 判定世界坐标 (ex, ez) 上的敌人在本地玩家的全向视野内是否可见（距离 + 墙遮挡）；
// 无本地玩家（服务端/观战）或不可判定时一律视为可见。
bool fog_entity_visible(Context* ctx, float ex, float ez);

#endif