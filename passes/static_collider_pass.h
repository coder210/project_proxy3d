#ifndef STATIC_COLLIDER_PASS_H
#define STATIC_COLLIDER_PASS_H

struct Context;

// 静态碰撞体 pass：绘制所有带有 StaticMeshComponent 的静态体（填充 + 描边双 pass），
// 支持 roll/yaw 旋转
void static_collider_pass(Context* ctx);

#endif