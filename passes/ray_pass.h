#ifndef RAY_PASS_H
#define RAY_PASS_H

struct Context;

// 射线 pass：绘制玩家发射的 hitscan 光束（瞬发直线，击中点截断）。
// 纯渲染层：消费逻辑层射线请求（ctx->ray_effects）生成一段短暂光束，并随生命周期淡出。
// 逻辑 system 不参与；只读 ctx->ray_effects，不参与帧同步判定。
void ray_pass(Context* ctx, float dt);

#endif