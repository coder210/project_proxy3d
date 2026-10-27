#ifndef SETTINGS_SCENE_3D_H
#define SETTINGS_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class SettingsScene : public Scene {
public:
    SettingsScene(Context* ctx);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    void sync_settings();
    jr3d_combobox_input_p ip_input = NULL;
    jr3d_input_p port_input = NULL;
    jr3d_slider_p bgm_slider = NULL;
    jr3d_slider_p sfx_slider = NULL;
    jr3d_checkbox_p fullscreen_cb = NULL;
    jr3d_button_p btn_back = NULL;
    ui_particles particles{};
    float elapsed = 0.0f;
};

#endif
