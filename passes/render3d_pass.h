#ifndef RENDER3D_PASS_H
#define RENDER3D_PASS_H

struct Context;

// 渲染编排 pass：按固定顺序串行执行各子 pass，形成完整一帧。
// 由场景 on_render 直接调用（非 ECS system）。
void render3d_pass(Context* ctx, float dt);

#endif