#ifndef LOADING_SCENE_3D_H
#define LOADING_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class LoadingScene : public Scene {
public:
    LoadingScene(Context* ctx, GameMode mode);
    void Load() override;
    void Update(float dt) override;
    void Render() override;
private:
    GameMode target_mode;
    float elapsed = 0.0f;
    bool loaded = false;
    int dot_count = 0;
    float dot_timer = 0.0f;
    ui_particles particles{};
};

#endif
