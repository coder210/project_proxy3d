#ifndef DECOR_PASS_H
#define DECOR_PASS_H
struct Context;
// 地图装饰层（纯渲染，不建实体/无碰撞/不进 sim/寻路，确定性派生自布局 → 各端一致）
void decor_pass(Context* ctx);
#endif