#ifndef DAMAGE_PASS_H
#define DAMAGE_PASS_H

struct Context;

// 残影飘字渲染 pass：消费 ctx->damage_texts（玩家命中敌人请求），创建世界空间 billboard 文字，
// 向上漂浮、渐隐，并在生命结束后销毁。纯渲染，不参与逻辑判定。
void damage_pass(Context* ctx, float dt);

#endif