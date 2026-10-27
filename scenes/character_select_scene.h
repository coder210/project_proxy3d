#ifndef CHARACTER_SELECT_SCENE_3D_H
#define CHARACTER_SELECT_SCENE_3D_H
#include <joy/joy.h>
#include "../context.h"
#include "scene.h"
#include "ui_particles.h"
#include "../shared/systems/character_def.h"

class CharacterSelectScene : public Scene {
public:
    CharacterSelectScene(Context* ctx, GameMode mode);
    void HandleEvent(const void* ev) override;
    void Load() override;
    void Update(float dt) override;
    void Render() override;
    void Destroy() override;
private:
    void draw_card(int i);
    GameMode target_mode;
    ui_particles particles{};
    float elapsed = 0.0f;
    jr3d_button_p btn_cards[CHAR_COUNT] = { NULL };
    jr3d_button_p btn_confirm = NULL;
    jr3d_button_p btn_back = NULL;
    jr3d_input_p nickname_input = NULL;
    int selected = 0;
};

#endif
