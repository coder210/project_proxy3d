#pragma once
#include <memory>
#include <SDL3/SDL.h>
#include <joy/joy.h>

class Scene;

// C++ 场景管理器（对齐 joy_side_template 阶段 3）：自管场景生命周期与事件分发，
// 不再转调 joy 的 scene_manager_*（C 版）。差异：Render 接受 jr3d_p（3D 渲染器），
// 由场景内部经 ctx_->r3d 使用；Update 内部按 fixed_step 累加驱动 FixedUpdate。
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    void push(Scene* next);
    void change(Scene* next);

    void setPending(Scene* next);
    void requestTransition(Scene* next, int code = 0);
    void processPending();

    void HandleEvent(const SDL_Event* e);
    void Update(float dt);
    void Render(jr3d_p r3d);

    void reset();
    const char* currentName() const;

private:
    std::unique_ptr<Scene> current_;
    std::unique_ptr<Scene> pending_;
};
