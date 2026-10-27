#ifndef LEVEL_SELECT_SCENE_3D_H
#define LEVEL_SELECT_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"
#include "../shared/systems/level_maps.h"

class LevelSelectScene : public Scene {
public:
    LevelSelectScene(Context* ctx, GameMode mode);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    void draw_level(int i);
    GameMode target_mode;
    int selected = 0;
    int enemy_cnt[LEVEL_COUNT] = { 0 };
    jr3d_button_p btn_levels[LEVEL_COUNT] = { NULL };
    jr3d_button_p btn_confirm = NULL;
    jr3d_button_p btn_back = NULL;
    ui_particles particles{};
    float elapsed = 0.0f;
};

#endif
