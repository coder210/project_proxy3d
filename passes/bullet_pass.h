#ifndef BULLET_PASS_H
#define BULLET_PASS_H

struct Context;

// 子弹渲染 pass：读取逻辑层 BulletComponent（由 melee_attack_system 确定性创建，
// bullet_system 移动/销毁），把它画成沿飞行方向拉长的发光短条。
// 仅客户端消费，服务端不调用；本 pass 不建实体、不参与判定。
void bullet_pass(Context* ctx);

#endif