#ifndef HEALTH_RING_PASS_H
#define HEALTH_RING_PASS_H

struct Context;

// 血环 pass：脚下水墨圆环（浓墨弧段=剩余血量，淡墨背景环=满血轮廓），跟随角色
void health_ring_pass(Context* ctx);

#endif