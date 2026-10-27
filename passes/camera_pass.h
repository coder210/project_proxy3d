#ifndef CAMERA_PASS_H
#define CAMERA_PASS_H

struct Context;

// 相机 pass：更新跟随目标 + 计算 eye/aim + 清屏 + 布置透视相机。
// 返回 r3d_begin 的结果（false 表示本次帧开始失败，后续 pass 不应执行）。
bool camera_pass(Context* ctx, float dt);

#endif