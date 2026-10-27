#include "victory_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include "main_menu_scene.h"
#include "level_select_scene.h"
#include "ui_particles.h"
#include "../shared/systems/level_maps.h"

VictoryScene::VictoryScene(Context* ctx) : Scene(ctx, "Victory") {}

void VictoryScene::HandleEvent(const void* ev) {
    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        VictoryScene* self = (VictoryScene*)arg;
        if (self->btn_replay) r3d_button_is_pressed(self->ctx_->r3d, self->btn_replay, true, x, y);
        if (self->btn_menu)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_menu,   true, x, y);
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        VictoryScene* self = (VictoryScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        // 再战一局：回选关页（沿用当前模式）
        if (self->btn_replay) {
            bool clicked = self->btn_replay->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_replay, x, y);
            r3d_button_release(r3d, self->btn_replay, x, y);
            if (clicked) {
                self->ctx_->scene_mgr->change(new LevelSelectScene(self->ctx_, self->ctx_->mode));
                return;
            }
        }
        // 回主菜单
        if (self->btn_menu) {
            bool clicked = self->btn_menu->pressedInside &&
                r3d_button_is_hovered(r3d, self->btn_menu, x, y);
            r3d_button_release(r3d, self->btn_menu, x, y);
            if (clicked) {
                self->ctx_->scene_mgr->change(new MainMenuScene(self->ctx_));
            }
        }
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        VictoryScene* self = (VictoryScene*)arg;
        if (self->btn_replay) r3d_button_is_pressed(self->ctx_->r3d, self->btn_replay, false, x, y);
        if (self->btn_menu)   r3d_button_is_pressed(self->ctx_->r3d, self->btn_menu,   false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void VictoryScene::Load() {
    log_info("[victory scene] start...");

    ui_particles_init(&particles, 480, 320);
    btn_replay = r3d_button_create(70.0f,  250.0f, 160.0f, 50.0f, "再战一局");
    btn_menu   = r3d_button_create(250.0f, 250.0f, 160.0f, 50.0f, "回主菜单");
}

void VictoryScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
}

void VictoryScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "通  关", 196.0f, 70.0f, 40.0f, 480.0f, 320.0f, 20, 120, 60);
    // 子标题：第 N 关 已肃清
    char buf[128];
    snprintf(buf, sizeof(buf), "第 %d 关的所有敌人已被肃清", ctx_->selected_level);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, buf, 130.0f, 132.0f, 18.0f, 480.0f, 320.0f, 60, 60, 70);
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, "恭喜通关！", 198.0f, 168.0f, 16.0f, 480.0f, 320.0f, 110, 110, 118);

    r3d_button_draw(r3d, ctx_->ui_quad, btn_replay, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_menu,   480.0f, 320.0f);

    r3d_end(r3d);
}

void VictoryScene::Destroy() {
    if (btn_replay) r3d_button_destroy(btn_replay);
    if (btn_menu)   r3d_button_destroy(btn_menu);
}
