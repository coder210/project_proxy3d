#ifndef ONLINE_SCENE_3D_H
#define ONLINE_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class OnlineScene : public Scene {
public:
    OnlineScene(Context* ctx);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    jr3d_label_p label_title = NULL;
    jr3d_button_p btn_host = NULL;
    jr3d_button_p btn_client = NULL;
    jr3d_button_p btn_server = NULL;
    jr3d_button_p btn_back = NULL;
    ui_particles particles{};
    float elapsed = 0.0f;
};

#endif
