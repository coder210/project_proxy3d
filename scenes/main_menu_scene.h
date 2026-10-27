#ifndef MAIN_MENU_SCENE_3D_H
#define MAIN_MENU_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class MainMenuScene : public Scene {
public:
    MainMenuScene(Context* ctx);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    ui_particles particles{};
    float elapsed = 0.0f;
    jr3d_button_p btn_start = NULL;
    jr3d_button_p btn_online = NULL;
    jr3d_button_p btn_settings = NULL;
    jr3d_button_p btn_exit = NULL;
};

#endif
