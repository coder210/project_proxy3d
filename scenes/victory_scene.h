#ifndef VICTORY_SCENE_3D_H
#define VICTORY_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class VictoryScene : public Scene {
public:
    VictoryScene(Context* ctx);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    jr3d_button_p btn_replay = NULL;
    jr3d_button_p btn_menu = NULL;
    ui_particles particles{};
    float elapsed = 0.0f;
};

#endif
