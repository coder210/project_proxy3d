#ifndef ENTITIES_PASS_H
#define ENTITIES_PASS_H

struct Context;

// 实体 pass：绘制所有带有 MeshComponent 的实体（填充 + 描边双 pass）
void entities_pass(Context* ctx, float dt);

// 玩家昵称头顶绘制：独立于场景实体，必须在雾层（fog_pass）之后调用，
// 令昵称始终高于迷雾渲染，不被环境视野遮挡。
void entities_pass_draw_nicknames(Context* ctx);

#endif