#include "main_menu_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "loading_scene.h"
#include "ui_particles.h"

MainMenuScene::MainMenuScene(Context* ctx) : Scene(ctx, "Main") {}

void MainMenuScene::HandleEvent(const void* ev) {
    jevent_callbacks_t cbs = {};
    cbs.on_mousepressed = [](float x, float y, int btn, void* arg) {
        (void)btn;
        MainMenuScene* self = (MainMenuScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        if (self->btn_start)    r3d_button_is_pressed(r3d, self->btn_start, true, x, y);
        if (self->btn_online)   r3d_button_is_pressed(r3d, self->btn_online, true, x, y);
        if (self->btn_settings) r3d_button_is_pressed(r3d, self->btn_settings, true, x, y);
        if (self->btn_exit)     r3d_button_is_pressed(r3d, self->btn_exit, true, x, y);
    };
    cbs.on_mousereleased = [](float x, float y, int btn, void* arg) {
        (void)btn;
        MainMenuScene* self = (MainMenuScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        if (self->btn_start)    r3d_button_release(r3d, self->btn_start, x, y);
        if (self->btn_online)   r3d_button_release(r3d, self->btn_online, x, y);
        if (self->btn_settings) r3d_button_release(r3d, self->btn_settings, x, y);
        if (self->btn_exit)     r3d_button_release(r3d, self->btn_exit, x, y);
    };
    cbs.on_mousemoved = [](float x, float y, float dx, float dy, void* arg) {
        (void)dx; (void)dy;
        MainMenuScene* self = (MainMenuScene*)arg;
        jr3d_p r3d = self->ctx_->r3d;
        if (self->btn_start)    r3d_button_is_pressed(r3d, self->btn_start, false, x, y);
        if (self->btn_online)   r3d_button_is_pressed(r3d, self->btn_online, false, x, y);
        if (self->btn_settings) r3d_button_is_pressed(r3d, self->btn_settings, false, x, y);
        if (self->btn_exit)     r3d_button_is_pressed(r3d, self->btn_exit, false, x, y);
    };
    dispatch_event(cbs, ev, this);
}

void MainMenuScene::Load() {
    log_info("[main menu scene] start...");

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_init(&particles, 480, 320);

    // 创建按钮（4个按钮纵向排列，逻辑坐标 480x320，窗口缩放自适应）
    int btn_width = 120, btn_height = 50;
    int btn_x = (480 - btn_width) / 2;
    int start_y = (320 - btn_height * 4 - 60) / 2;
    int gap = 20;

    btn_start = r3d_button_create((float)btn_x, (float)start_y, (float)btn_width, (float)btn_height, "单人游戏");
    r3d_button_set_onclick(btn_start, [](jr3d_button_p btn) {
        (void)btn;
        push_user_event(MENU_EVENT, MENU_EVENT_START);
    });
    btn_online = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap)), (float)btn_width, (float)btn_height, "联机游戏");
    r3d_button_set_onclick(btn_online, [](jr3d_button_p btn) {
        (void)btn;
        push_user_event(MENU_EVENT, MENU_EVENT_ONLINE);
    });
    btn_settings = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap) * 2), (float)btn_width, (float)btn_height, "设置");
    r3d_button_set_onclick(btn_settings, [](jr3d_button_p btn) {
        (void)btn;
        push_user_event(MENU_EVENT, MENU_EVENT_SETTINGS);
    });
    btn_exit = r3d_button_create((float)btn_x, (float)(start_y + (btn_height + gap) * 3), (float)btn_width, (float)btn_height, "退出");
    r3d_button_set_onclick(btn_exit, [](jr3d_button_p btn) {
        (void)btn;
        push_user_event(MENU_EVENT, MENU_EVENT_EXIT);
    });
}

void MainMenuScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);
}

void MainMenuScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 3D 相机用于背景
    vec3_t eye = { 0, 0, 5 };
    vec3_t target = { 0, 0, 0 };
    vec3_t up = { 0, 1, 0 };
    r3d_camera_lookat(r3d, eye, target, up, 60.0f);

    // 背景(白底蓝图风, 与 game_scene 一致)
    vec3_t bg_pos = { 0, 0, 0 };
    vec3_t bg_scale = { (float)winW / (float)winH * 6.0f, 6.0f, 0.1f };
    r3d_draw_mesh(r3d, ctx_->ui_quad, bg_pos, bg_scale, jcolor_t{ 255, 255, 255, 255 });

    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_draw(r3d, ctx_->ui_quad, &particles, 480.0f, 320.0f);

    // 标题（呼吸动画 + 深灰，仿 project_proxy2d main_scene）
    float pulse = 0.8f + 0.2f * sinf(elapsed * 1.8f);
    const char* title = "Project Proxy 3D";
    float title_x = (480 - 14 * (int)strlen(title)) / 2.0f;
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, title, title_x, 28.0f, 24.0f, 480.0f, 320.0f,
        (uint8_t)(60.0f * pulse), (uint8_t)(60.0f * pulse), (uint8_t)(70.0f * pulse));

    // 版本号（底部，深灰）
    const char* ver = "v1.0.0";
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, ver, (float)(480 - 50) / 2.0f, 320.0f - 20.0f, 12.0f, 480.0f, 320.0f,
        60, 60, 70);

    // 按钮
    r3d_button_draw(r3d, ctx_->ui_quad, btn_start, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_online, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_settings, 480.0f, 320.0f);
    r3d_button_draw(r3d, ctx_->ui_quad, btn_exit, 480.0f, 320.0f);

    r3d_end(r3d);
}

void MainMenuScene::Destroy() {
    if (btn_start) r3d_button_destroy(btn_start);
    if (btn_online) r3d_button_destroy(btn_online);
    if (btn_settings) r3d_button_destroy(btn_settings);
    if (btn_exit) r3d_button_destroy(btn_exit);
}
