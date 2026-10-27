#ifndef INTERPOLATE_POSITION_PASS_H
#define INTERPOLATE_POSITION_PASS_H

struct Context;

// 位置插值 pass：将逻辑帧定点位置平滑插值到渲染 Transform。
// 纯呈现层，不属于 ECS 逻辑 system，由 update 每帧直接调用。
void interpolate_position_pass(Context* ctx, float dt);

#endif