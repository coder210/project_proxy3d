#ifndef GAME_SCENE_3D_H
#define GAME_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"

// GameScene 直接持有原 game_scene 的全部运行时字段（ctx 由 Scene 基类提供 ctx_），
// 不再依赖任何旧 joy C 场景结构。
class GameScene : public Scene {
public:
    GameScene(Context* ctx);
    void Load() override;
    void HandleEvent(const void* ev) override;
    void FixedUpdate(float step) override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    uint32_t fps_measure_time = 0;
    int fps_measure_frame = 0;
    bool victory_started = false;
    float victory_delay = 0.0f;
    uint32_t last_render_ms = 0;
};

#endif
