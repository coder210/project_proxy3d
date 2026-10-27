#ifndef SCENE_H
#define SCENE_H

#include <joy/joy.h>
#include "../context.h"
#include "scene_manager.h"

// 3D 场景基类（对齐 joy_side_template 的阶段 3 设计，渲染器为 jr3d）。
// 生命周期由 SceneManager 以 std::unique_ptr<Scene> 持有（current_ / pending_）。
// 切换时先调旧场景 Destroy() 清理资源（按钮/网络等），再删除对象本体；
// 因此子类 Destroy() 只做资源清理，不要再 delete this。
//
// 与 2D 版差异：
//  - Render() 无参数，场景通过 ctx_->r3d 取 3D 渲染器；
//  - 额外提供 FixedUpdate(step) 与 fixed_step 配置，用于帧同步锁步逻辑
//    （复刻原 joy C scene 的 scene_set_fixed_update_callback 行为）。
class Scene {
public:
    Scene(Context* ctx, const char* name) : ctx_(ctx), name_(name) {}
    virtual ~Scene() = default;

    const char* name() const { return name_; }
    Context* ctx() const { return ctx_; }

    virtual void Load() {}
    virtual void HandleEvent(const void* ev) {}
    virtual void HandleUiEvent(const void* ev) {}
    virtual void FixedUpdate(float step) {}   // 固定步长（帧同步 lockstep）
    virtual void Update(float dt) {}
    virtual void Render() {}
    virtual void Destroy() {}

    void set_fixed_step(float step) { fixed_step_ = step; }
    float fixed_step() const { return fixed_step_; }

protected:
    Context* ctx_;
    const char* name_;
    float fixed_step_ = 0.0f;
    float fixed_accum_ = 0.0f;

    // SceneManager 需要驱动固定步长累加器
    friend class SceneManager;
};

#endif
