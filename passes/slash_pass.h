#ifndef SLASH_PASS_H
#define SLASH_PASS_H

struct Context;

// 攻击刀光 pass：竖向斩击弧（浓墨核心 + 淡墨晕染）+ 地面扫掠弧（淡墨细线），随生命周期展开并淡出。
// 纯渲染层：消费逻辑层挥击请求（ctx->attack_flashes）生成刀光特效实体、推进生命周期并渲染，逻辑 system 不参与。
void slash_pass(Context* ctx, float dt);

#endif