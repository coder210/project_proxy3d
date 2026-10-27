#ifndef SPLASH_SCENE_3D_H
#define SPLASH_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"

class SplashScene : public Scene {
public:
    SplashScene(Context* ctx);
    void Load() override;
    void Update(float dt) override;
    void Render() override;
private:
    float elapsed = 0.0f;
    float alpha = 0.0f;
    bool faded_in = false;
    bool fading_out = false;
    int logo_w = 0, logo_h = 0;
    ui_particles particles{};
};

#endif
