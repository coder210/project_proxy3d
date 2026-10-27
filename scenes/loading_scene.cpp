#include "loading_scene.h"
#include <joy/joy.h>
#include <stdio.h>
#include "game_scene.h"
#include "ui_particles.h"

LoadingScene::LoadingScene(Context* ctx, GameMode mode)
    : Scene(ctx, "Loading"), target_mode(mode) {}

void LoadingScene::Load() {
    // 背景粒子(仿 project_proxy2d main_scene)
    ui_particles_init(&particles, 480, 320);
    log_info("[loading scene] loading...");
}

void LoadingScene::Update(float dt) {
    elapsed += dt;
    ui_particles_update(&particles, dt, 480, 320);

    // 加点动画：每 0.4s 切换一个点
    dot_timer += dt;
    if (dot_timer >= 0.4f) {
        dot_count = (dot_count + 1) % 4;
        dot_timer = 0;
    }

    // 0.5s 后准备游戏场景（模拟"加载"过程）
    if (!loaded && elapsed >= 0.5f) {
        reset_context_for_mode(ctx_, target_mode);
        loaded = true;
    }

    // 1.5s 后切换到游戏场景
    if (loaded && elapsed >= 1.5f) {
        ctx_->scene_mgr->setPending(new GameScene(ctx_));
        push_user_event(TRANSITION_EVENT, TRANSITION_LOADING_DONE);
    }
}

void LoadingScene::Render() {
    jr3d_p r3d = ctx_->r3d;

    int winW, winH;
    r3d_get_window_size(r3d, &winW, &winH);

    if (!r3d_begin(r3d)) return;

    // 背景
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

    // 加载文字（居中显示，逻辑坐标 480x320，窗口缩放自适应）
    char text[32];
    int dc = dot_count;
    if (dc == 0) snprintf(text, sizeof(text), "加载中");
    else if (dc == 1) snprintf(text, sizeof(text), "加载中.");
    else if (dc == 2) snprintf(text, sizeof(text), "加载中..");
    else snprintf(text, sizeof(text), "加载中...");
    r3d_font_draw_2d_color(r3d, ctx_->ui_quad, text, (float)(480 - 120) / 2.0f, 320.0f / 2.0f - 12, 24.0f, 480.0f, 320.0f, 60, 60, 70);

    r3d_end(r3d);
}
